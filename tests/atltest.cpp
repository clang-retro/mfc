// atltest.cpp - drives the ATL server in atltest-dll as COM clients do:
//   - DllRegisterServer runs its .rgs script (with %MODULE% and a
//     replacement of its own) and the old-style DECLARE_REGISTRY, and
//     DllUnregisterServer takes the keys away again;
//   - CoCreateInstance makes its objects through their class factories:
//     calls through the vtable and through IDispatch (the type library is
//     written here and registered), rich errors, events through a
//     connection point to a sink made here with ATL, tear-offs,
//     aggregation, a singleton, IProvideClassInfo2;
//   - once everything is released the DLL may unload;
// and checks the ATL pieces a client uses itself: CComObject,
// CComObjectStack, CComEnum, CComPtr, CComBSTR and CComVariant; and the
// module's threads (CAtlAutoThreadModule and
// DECLARE_CLASSFACTORY_AUTO_THREAD), CComGITPtr, a service map,
// ICollectionOnSTLImpl, and the registrar as IRegistrar, per machine and
// per user.
#include <atlbase.h>
#include <atlcom.h>
#include <stdio.h>
#include <vector>
#include "atltest.h"

CComModule _Module;

static int g_nFailures;
#define CHECK(cond)                                           \
  do {                                                        \
    if (!(cond)) {                                            \
      printf("FAIL line %d: %s\n", __LINE__, #cond);          \
      g_nFailures++;                                          \
    }                                                         \
  } while (0)

#ifdef _UNICODE
static const WCHAR kDllName[] = L"atltest-dll-u.dll";
#else
static const WCHAR kDllName[] = L"atltest-dll-a.dll";
#endif

// ---------------------------------------------------------------------
// The type library: IAtlThing (dual), _IAtlThingEvents and the coclass.

static HRESULT AddFunc(ICreateTypeInfo* pcti, UINT index, MEMBERID memid, INVOKEKIND invkind,
                       FUNCKIND funckind, VARTYPE vtRet, LPCWSTR lpszName, UINT cParams,
                       ELEMDESC* params, LPCWSTR* paramNames) {
  FUNCDESC fd = {};
  fd.memid = memid;
  fd.funckind = funckind;
  fd.invkind = invkind;
  fd.callconv = CC_STDCALL;
  fd.cParams = (SHORT)cParams;
  fd.lprgelemdescParam = params;
  fd.elemdescFunc.tdesc.vt = vtRet;
  fd.oVft = (SHORT)((7 + index) * sizeof(void*));
  HRESULT hr = pcti->AddFuncDesc(index, &fd);
  if (FAILED(hr)) return hr;
  // A property put names only the function.
  LPOLESTR names[4] = {(LPOLESTR)lpszName};
  UINT n = 1;
  if (invkind != INVOKE_PROPERTYPUT)
    for (UINT i = 0; i < cParams; i++) names[n++] = (LPOLESTR)paramNames[i];
  return pcti->SetFuncAndParamNames(index, names, n);
}

static ITypeInfo* DispatchTypeInfo() {
  ITypeLib* pStdLib = NULL;
  ITypeInfo* pDispInfo = NULL;
  if (SUCCEEDED(::LoadTypeLib(L"stdole2.tlb", &pStdLib))) {
    pStdLib->GetTypeInfoOfGuid(IID_IDispatch, &pDispInfo);
    pStdLib->Release();
  }
  return pDispInfo;
}

static BOOL WriteTypeLib(LPCWSTR lpszFile) {
  CComPtr<ICreateTypeLib2> spctl;
  if (FAILED(::CreateTypeLib2(SYS_WIN32, lpszFile, &spctl))) return FALSE;
  spctl->SetGuid(LIBID_AtlTestLib);
  spctl->SetVersion(1, 0);
  spctl->SetName((LPOLESTR)L"AtlTestLib");
  spctl->SetLcid(LOCALE_NEUTRAL);
  CComPtr<ITypeInfo> spDisp;
  spDisp.p = DispatchTypeInfo();
  if (spDisp == NULL) return FALSE;

  // interface IAtlThing : IDispatch, dual.
  CComPtr<ICreateTypeInfo> spThing;
  if (FAILED(spctl->CreateTypeInfo((LPOLESTR)L"IAtlThing", TKIND_INTERFACE, &spThing)))
    return FALSE;
  spThing->SetGuid(IID_IAtlThing);
  HREFTYPE hRef = 0;
  if (FAILED(spThing->AddRefTypeInfo(spDisp, &hRef)) || FAILED(spThing->AddImplType(0, hRef)))
    return FALSE;
  spThing->SetTypeFlags(TYPEFLAG_FDUAL | TYPEFLAG_FOLEAUTOMATION);
  TYPEDESC tdLong = {}, tdBstr = {};
  tdLong.vt = VT_I4;
  tdBstr.vt = VT_BSTR;
  ELEMDESC retLong = {}, retBstr = {}, inLong = {}, ab[2] = {};
  retLong.tdesc.vt = VT_PTR;
  retLong.tdesc.lptdesc = &tdLong;
  retLong.paramdesc.wParamFlags = PARAMFLAG_FOUT | PARAMFLAG_FRETVAL;
  retBstr.tdesc.vt = VT_PTR;
  retBstr.tdesc.lptdesc = &tdBstr;
  retBstr.paramdesc.wParamFlags = PARAMFLAG_FOUT | PARAMFLAG_FRETVAL;
  inLong.tdesc.vt = VT_I4;
  inLong.paramdesc.wParamFlags = PARAMFLAG_FIN;
  ab[0] = inLong;
  ab[1] = retLong;
  LPCWSTR nVal[] = {L"pVal"}, nNew[] = {L"newVal"}, nAdd[] = {L"a", L"pResult"},
          nKind[] = {L"nKind"}, nN[] = {L"n"};
  HRESULT hr = AddFunc(spThing, 0, 1, INVOKE_PROPERTYGET, FUNC_PUREVIRTUAL, VT_HRESULT,
                       L"Value", 1, &retLong, nVal);
  if (SUCCEEDED(hr))
    hr = AddFunc(spThing, 1, 1, INVOKE_PROPERTYPUT, FUNC_PUREVIRTUAL, VT_HRESULT, L"Value", 1,
                 &inLong, nNew);
  if (SUCCEEDED(hr))
    hr = AddFunc(spThing, 2, 2, INVOKE_FUNC, FUNC_PUREVIRTUAL, VT_HRESULT, L"Add", 2, ab, nAdd);
  if (SUCCEEDED(hr))
    hr = AddFunc(spThing, 3, 3, INVOKE_FUNC, FUNC_PUREVIRTUAL, VT_HRESULT, L"Fail", 1, &inLong,
                 nKind);
  if (SUCCEEDED(hr))
    hr = AddFunc(spThing, 4, 4, INVOKE_FUNC, FUNC_PUREVIRTUAL, VT_HRESULT, L"Fire", 1, &inLong,
                 nN);
  if (SUCCEEDED(hr))
    hr = AddFunc(spThing, 5, 5, INVOKE_PROPERTYGET, FUNC_PUREVIRTUAL, VT_HRESULT, L"Name", 1,
                 &retBstr, nVal);
  if (FAILED(hr)) {
    printf("note: type library functions: %08lx\n", (unsigned long)hr);
    return FALSE;
  }
  spThing->LayOut();

  // dispinterface _IAtlThingEvents { [id(1)] void Fired(long n); }
  CComPtr<ICreateTypeInfo> spEvents;
  if (FAILED(spctl->CreateTypeInfo((LPOLESTR)L"_IAtlThingEvents", TKIND_DISPATCH, &spEvents)))
    return FALSE;
  spEvents->SetGuid(DIID__IAtlThingEvents);
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
  LPOLESTR firedNames[] = {(LPOLESTR)L"Fired", (LPOLESTR)L"n"};
  if (FAILED(spEvents->AddFuncDesc(0, &fd)) ||
      FAILED(spEvents->SetFuncAndParamNames(0, firedNames, 2)))
    return FALSE;
  spEvents->LayOut();

  // coclass AtlThing { [default] interface IAtlThing; [default, source] dispinterface ...; }
  CComPtr<ICreateTypeInfo> spClass;
  if (FAILED(spctl->CreateTypeInfo((LPOLESTR)L"AtlThing", TKIND_COCLASS, &spClass)))
    return FALSE;
  spClass->SetGuid(CLSID_AtlThing);
  spClass->SetTypeFlags(TYPEFLAG_FCANCREATE);
  CComQIPtr<ITypeInfo> spThingInfo(spThing.p);
  CComQIPtr<ITypeInfo> spEventsInfo(spEvents.p);
  if (spThingInfo == NULL || spEventsInfo == NULL) return FALSE;
  if (FAILED(spClass->AddRefTypeInfo(spThingInfo, &hRef)) ||
      FAILED(spClass->AddImplType(0, hRef)) ||
      FAILED(spClass->SetImplTypeFlags(0, IMPLTYPEFLAG_FDEFAULT)) ||
      FAILED(spClass->AddRefTypeInfo(spEventsInfo, &hRef)) ||
      FAILED(spClass->AddImplType(1, hRef)) ||
      FAILED(spClass->SetImplTypeFlags(1, IMPLTYPEFLAG_FDEFAULT | IMPLTYPEFLAG_FSOURCE)))
    return FALSE;
  spClass->LayOut();
  hr = spctl->SaveAllChanges();
  if (FAILED(hr)) printf("note: SaveAllChanges: %08lx\n", (unsigned long)hr);
  return SUCCEEDED(hr);
}

// ---------------------------------------------------------------------
// The registry.

static BOOL ReadKey(LPCWSTR lpszKey, LPCWSTR lpszValueName, WCHAR* pBuf, DWORD cch) {
  pBuf[0] = 0;
  HKEY hKey;
  if (::RegOpenKeyExW(HKEY_CLASSES_ROOT, lpszKey, 0, KEY_READ, &hKey) != ERROR_SUCCESS)
    return FALSE;
  DWORD cb = cch * sizeof(WCHAR), type = 0;
  LONG l = ::RegQueryValueExW(hKey, lpszValueName, NULL, &type, (LPBYTE)pBuf, &cb);
  ::RegCloseKey(hKey);
  return l == ERROR_SUCCESS;
}

static BOOL KeyExists(LPCWSTR lpszKey) {
  HKEY hKey;
  if (::RegOpenKeyExW(HKEY_CLASSES_ROOT, lpszKey, 0, KEY_READ, &hKey) != ERROR_SUCCESS)
    return FALSE;
  ::RegCloseKey(hKey);
  return TRUE;
}

static BOOL EndsWith(LPCWSTR s, LPCWSTR suffix) {
  size_t n = wcslen(s), m = wcslen(suffix);
  return n >= m && lstrcmpiW(s + n - m, suffix) == 0;
}

// ---------------------------------------------------------------------
// The client's own ATL objects.

// The event sink, an IDispatch that answers for _IAtlThingEvents.
class ATL_NO_VTABLE CSink : public CComObjectRootEx<CComSingleThreadModel>, public IDispatch {
public:
  long m_nLast;
  int m_nCalls;
  CSink() : m_nLast(-1), m_nCalls(0) {}

  BEGIN_COM_MAP(CSink)
    COM_INTERFACE_ENTRY(IDispatch)
    COM_INTERFACE_ENTRY_IID(DIID__IAtlThingEvents, IDispatch)
  END_COM_MAP()

  STDMETHOD(GetTypeInfoCount)(UINT* p) override {
    *p = 0;
    return S_OK;
  }
  STDMETHOD(GetTypeInfo)(UINT, LCID, ITypeInfo**) override { return E_NOTIMPL; }
  STDMETHOD(GetIDsOfNames)(REFIID, LPOLESTR*, UINT, LCID, DISPID*) override { return E_NOTIMPL; }
  STDMETHOD(Invoke)(DISPID id, REFIID, LCID, WORD, DISPPARAMS* p, VARIANT*, EXCEPINFO*,
                    UINT*) override {
    if (id != 1 || p->cArgs != 1) return DISP_E_MEMBERNOTFOUND;
    CComVariant v;
    if (FAILED(v.ChangeType(VT_I4, &p->rgvarg[0]))) return DISP_E_TYPEMISMATCH;
    m_nLast = v.lVal;
    m_nCalls++;
    return S_OK;
  }
};

// An object with no event interface, to be refused.
class ATL_NO_VTABLE CNotASink : public CComObjectRootEx<CComSingleThreadModel>,
                                public IAtlRare {
public:
  BEGIN_COM_MAP(CNotASink)
    COM_INTERFACE_ENTRY(IAtlRare)
  END_COM_MAP()
  STDMETHOD(Magic)(long* p) override {
    *p = 0;
    return S_OK;
  }
};

static int g_nStackDestroyed;
class ATL_NO_VTABLE CStackThing : public CComObjectRootEx<CComSingleThreadModel>,
                                  public IAtlRare {
public:
  long m_n;
  BEGIN_COM_MAP(CStackThing)
    COM_INTERFACE_ENTRY(IAtlRare)
  END_COM_MAP()
  HRESULT FinalConstruct() {
    m_n = 5;
    return S_OK;
  }
  void FinalRelease() { g_nStackDestroyed++; }
  STDMETHOD(Magic)(long* p) override {
    *p = m_n;
    return S_OK;
  }
};

// The outer object of an aggregate: it answers for IAtlThing with the
// inner object's.
struct COuter : public IUnknown {
  LONG m_ref = 1;
  IUnknown* m_pInner = NULL;
  STDMETHOD(QueryInterface)(REFIID riid, void** ppv) override {
    if (InlineIsEqualGUID(riid, IID_IUnknown)) {
      *ppv = static_cast<IUnknown*>(this);
      AddRef();
      return S_OK;
    }
    if (InlineIsEqualGUID(riid, IID_IAtlThing) && m_pInner != NULL)
      return m_pInner->QueryInterface(riid, ppv);
    *ppv = NULL;
    return E_NOINTERFACE;
  }
  STDMETHOD_(ULONG, AddRef)() override { return ++m_ref; }
  STDMETHOD_(ULONG, Release)() override { return --m_ref; }
};

// ---------------------------------------------------------------------
// The module's threads, the global interface table, services and
// collections.

// A collection of numbers, with a service of its own.
struct IAtlItems : public IUnknown {
  STDMETHOD(get_Count)(long* pCount) = 0;
  STDMETHOD(get_Item)(long Index, VARIANT* pvar) = 0;
  STDMETHOD(get__NewEnum)(IUnknown** ppUnk) = 0;
};
__CRT_UUID_DECL(IAtlItems, 0x5d1f4a07, 0x93b2, 0x4c5e, 0x8a, 0x61, 0x2f, 0x07, 0xc4, 0x9e, 0x3b, 0x18)
#define IID_IAtlItems __uuidof(IAtlItems)
// {6E2A5B18-A4C3-4D6F-9B72-30F8D5AF4C29}
static const GUID SID_SAtlItems = {
    0x6e2a5b18, 0xa4c3, 0x4d6f, {0x9b, 0x72, 0x30, 0xf8, 0xd5, 0xaf, 0x4c, 0x29}};
// {7F3B6C29-B5D4-4E70-AC83-41F9E6B05D3A}, a service nobody has.
static const GUID SID_SNobody = {
    0x7f3b6c29, 0xb5d4, 0x4e70, {0xac, 0x83, 0x41, 0xf9, 0xe6, 0xb0, 0x5d, 0x3a}};

typedef std::vector<CComVariant> AtlItemVector;
typedef CComEnumOnSTL<IEnumVARIANT, &IID_IEnumVARIANT, VARIANT, _Copy<VARIANT>, AtlItemVector>
    AtlItemEnum;

static DWORD g_dwItemsThread;
class ATL_NO_VTABLE CItems
    : public CComObjectRootEx<CComMultiThreadModel>,
      public ICollectionOnSTLImpl<IAtlItems, AtlItemVector, VARIANT, _Copy<VARIANT>, AtlItemEnum>,
      public IServiceProviderImpl<CItems> {
public:
  DWORD m_dwThread;
  CComPtr<IUnknown> m_spNext;  // whose services come after ours

  // Any thread may call it: it says so with the free-threaded marshaler.
  CComPtr<IUnknown> m_spMarshaler;
  DECLARE_GET_CONTROLLING_UNKNOWN()
  DECLARE_PROTECT_FINAL_CONSTRUCT()

  BEGIN_COM_MAP(CItems)
    COM_INTERFACE_ENTRY(IAtlItems)
    COM_INTERFACE_ENTRY(IServiceProvider)
    COM_INTERFACE_ENTRY_AGGREGATE(IID_IMarshal, m_spMarshaler.p)
  END_COM_MAP()
  BEGIN_SERVICE_MAP(CItems)
    SERVICE_ENTRY(SID_SAtlItems)
    SERVICE_ENTRY_CHAIN(m_spNext)
  END_SERVICE_MAP()

  HRESULT FinalConstruct() {
    m_dwThread = g_dwItemsThread = ::GetCurrentThreadId();
    for (long i = 1; i <= 3; i++) m_coll.push_back(CComVariant(i * 100));
    return ::CoCreateFreeThreadedMarshaler(GetControllingUnknown(), &m_spMarshaler.p);
  }
  void FinalRelease() { m_spMarshaler.Release(); }
};

// The object of the auto-thread factory.
class ATL_NO_VTABLE CThreadItems : public CItems {
public:
  DECLARE_CLASSFACTORY_AUTO_THREAD()
};

struct GitData {
  DWORD dwCookie;
  long nCount;
  HRESULT hr;
};
static DWORD WINAPI GitThread(LPVOID pv) {
  GitData* d = (GitData*)pv;
  ::CoInitialize(NULL);
  CComGITPtr<IAtlItems> git(d->dwCookie);
  CComPtr<IAtlItems> sp;
  d->hr = git.CopyTo(&sp);
  if (sp != NULL) sp->get_Count(&d->nCount);
  git.Detach();  // the cookie stays the main thread's
  sp.Release();
  ::CoUninitialize();
  return 0;
}

static void TestModulePieces() {
  // The collection: Count, Item from 1, _NewEnum.
  CComObject<CItems>* pItems = NULL;
  CHECK(SUCCEEDED(CComObject<CItems>::CreateInstance(&pItems)));
  CComPtr<IAtlItems> spItems;
  pItems->QueryInterface(IID_IAtlItems, (void**)&spItems);
  long n = 0;
  CHECK(spItems->get_Count(&n) == S_OK && n == 3);
  CComVariant v;
  CHECK(spItems->get_Item(2, &v) == S_OK && v.vt == VT_I4 && v.lVal == 200);
  CHECK(spItems->get_Item(0, &v) == E_INVALIDARG);
  CHECK(spItems->get_Item(4, &v) == E_INVALIDARG);
  {
    CComPtr<IUnknown> spUnk;
    CHECK(spItems->get__NewEnum(&spUnk) == S_OK && spUnk != NULL);
    CComQIPtr<IEnumVARIANT> spEnum(spUnk);
    CHECK(spEnum != NULL);
    VARIANT got[4];
    ULONG nGot = 0;
    CHECK(spEnum != NULL && spEnum->Next(4, got, &nGot) == S_FALSE && nGot == 3);
    CHECK(got[0].lVal == 100 && got[2].lVal == 300);
  }

  // The service map: its own service, then the chain's, then none.
  CComObject<CItems>* pNext = NULL;
  CComObject<CItems>::CreateInstance(&pNext);
  pNext->AddRef();
  pItems->m_spNext = pNext->GetUnknown();
  {
    CComQIPtr<IServiceProvider> spSP(spItems);
    CHECK(spSP != NULL);
    CComPtr<IAtlItems> spGot;
    CHECK(spSP->QueryService(SID_SAtlItems, IID_IAtlItems, (void**)&spGot) == S_OK);
    CHECK(spGot.p == spItems.p);
    void* pv = (void*)1;
    CHECK(spSP->QueryService(SID_SNobody, IID_IAtlItems, &pv) == E_NOINTERFACE && pv == NULL);
    // The second one's own map has no chain: asked for itself through ours.
    pItems->m_spNext.Release();
    pNext->m_spNext = spItems;
    CComQIPtr<IServiceProvider> spSP2(pNext->GetUnknown());
    spGot.Release();
    CHECK(spSP2->QueryService(SID_SAtlItems, IID_IAtlItems, (void**)&spGot) == S_OK);
    CHECK(spGot.p == static_cast<IAtlItems*>(pNext));
    pNext->m_spNext.Release();
  }
  pNext->Release();

  // CComGITPtr: the cookie taken to another thread, the object had there.
  {
    CComGITPtr<IAtlItems> git(spItems);
    CHECK(git.GetCookie() != 0);
    GitData d = {git.GetCookie(), 0, E_FAIL};
    HANDLE h = ::CreateThread(NULL, 0, GitThread, &d, 0, NULL);
    ::WaitForSingleObject(h, INFINITE);
    ::CloseHandle(h);
    CHECK(d.hr == S_OK && d.nCount == 3);
    CComPtr<IAtlItems> sp;
    CHECK(git.CopyTo(&sp) == S_OK && sp.p == spItems.p);
    DWORD dw = git.Detach();
    CComGITPtr<IAtlItems> git2(dw);
    CHECK(git2.Revoke() == S_OK && git2.GetCookie() == 0);
    sp.Release();
    CComPtr<IAtlItems> sp2;
    CHECK(FAILED(git2.CopyTo(&sp2)));
  }
  spItems.Release();

  // The auto-thread factory: without a pool the object is made here...
  CComObject<CComClassFactoryAutoThread>* pCF = NULL;
  CHECK(SUCCEEDED(CComObject<CComClassFactoryAutoThread>::CreateInstance(&pCF)));
  CComPtr<IClassFactory> spCF;
  pCF->QueryInterface(IID_IClassFactory, (void**)&spCF);
  pCF->SetVoid((void*)&CComCreator<CComObject<CThreadItems> >::CreateInstance);
  {
    CComPtr<IAtlItems> sp;
    CHECK(spCF->CreateInstance(NULL, IID_IAtlItems, (void**)&sp) == S_OK && sp != NULL);
    CHECK(g_dwItemsThread == ::GetCurrentThreadId());
  }
  // ...and with one, in its threads, taken in turn.
  {
    CAtlAutoThreadModule pool(2);
    CHECK(pool.m_nThreads == 2);
    CComPtr<IAtlItems> sp1, sp2, sp3;
    CHECK(spCF->CreateInstance(NULL, IID_IAtlItems, (void**)&sp1) == S_OK && sp1 != NULL);
    DWORD dw1 = g_dwItemsThread;
    CHECK(spCF->CreateInstance(NULL, IID_IAtlItems, (void**)&sp2) == S_OK && sp2 != NULL);
    DWORD dw2 = g_dwItemsThread;
    CHECK(spCF->CreateInstance(NULL, IID_IAtlItems, (void**)&sp3) == S_OK && sp3 != NULL);
    DWORD dw3 = g_dwItemsThread;
    CHECK(dw1 != ::GetCurrentThreadId() && dw2 != ::GetCurrentThreadId());
    CHECK(dw1 != dw2 && dw3 == dw1);
    CHECK(pool.FindApartment(dw1) != NULL && pool.FindApartment(::GetCurrentThreadId()) == NULL);
    long nCount = 0;
    CHECK(sp2 != NULL && sp2->get_Count(&nCount) == S_OK && nCount == 3);
    // An aggregate is made here, as a plain factory would.
    void* pv = (void*)1;
    CHECK(spCF->CreateInstance((IUnknown*)1, IID_IAtlItems, &pv) == CLASS_E_NOAGGREGATION && pv == NULL);
  }
  CHECK(_pAtlAutoThreadModule == NULL);
  spCF.Release();
  CHECK(_Module.GetLockCount() == 0);
}

// The registrar as IRegistrar, and registration per user.
static void TestRegistrar() {
  CRegObject ro;
  CComPtr<IRegistrar> spReg;
  CHECK(ro.QueryInterface(IID_IRegistrar, (void**)&spReg) == S_OK && spReg != NULL);
  CHECK(spReg->AddReplacement(L"WHAT", L"Tested") == S_OK);
  static const WCHAR kScript[] =
      L"HKCU { NoRemove Software { ForceRemove AtlTestRegistrar = s '%WHAT%' { val Kind = d 7 } } }";
  CHECK(spReg->StringRegister(kScript) == S_OK);
  CRegKey key;
  CHECK(key.Open(HKEY_CURRENT_USER, TEXT("Software\\AtlTestRegistrar"), KEY_READ) == ERROR_SUCCESS);
  DWORD dw = 0;
  CHECK(key.QueryDWORDValue(TEXT("Kind"), dw) == ERROR_SUCCESS && dw == 7);
  TCHAR sz[32];
  ULONG cch = 32;
  CHECK(key.QueryStringValue(NULL, sz, &cch) == ERROR_SUCCESS && lstrcmp(sz, TEXT("Tested")) == 0);
  key.Close();
  CHECK(spReg->StringUnregister(kScript) == S_OK);
  CHECK(key.Open(HKEY_CURRENT_USER, TEXT("Software\\AtlTestRegistrar"), KEY_READ) != ERROR_SUCCESS);

  // Per user, HKCR is the user's own classes.
  bool bPerUser = true;
  CHECK(AtlGetPerUserRegistration(&bPerUser) == S_OK && !bPerUser);
  CHECK(AtlSetPerUserRegistration(true) == S_OK);
  static const WCHAR kClasses[] = L"HKCR { ForceRemove AtlTest.PerUser = s 'Per user' }";
  CHECK(spReg->StringRegister(kClasses) == S_OK);
  CHECK(key.Open(HKEY_CURRENT_USER, TEXT("Software\\Classes\\AtlTest.PerUser"), KEY_READ) ==
        ERROR_SUCCESS);
  key.Close();
  CHECK(spReg->StringUnregister(kClasses) == S_OK);
  CHECK(key.Open(HKEY_CURRENT_USER, TEXT("Software\\Classes\\AtlTest.PerUser"), KEY_READ) !=
        ERROR_SUCCESS);
  AtlSetPerUserRegistration(false);
  spReg.Release();
}

// ---------------------------------------------------------------------

static void TestClientPieces() {
  // CComObject: made with a count of 0, gone at the last Release.
  CComObject<CSink>* pSink = NULL;
  CHECK(SUCCEEDED(CComObject<CSink>::CreateInstance(&pSink)));
  CHECK(pSink != NULL && pSink->m_dwRef == 0);
  LONG nLocks = _Module.GetLockCount();
  CHECK(nLocks == 1);  // the object keeps the module
  {
    CComPtr<IDispatch> spDisp;
    CHECK(SUCCEEDED(pSink->QueryInterface(&spDisp)));
    CComPtr<IUnknown> spUnk;
    CHECK(SUCCEEDED(spDisp->QueryInterface(IID_IUnknown, (void**)&spUnk)));
    CHECK(spUnk.p == static_cast<IUnknown*>(static_cast<IDispatch*>(pSink)));
    IUnknown* pNo = (IUnknown*)1;
    CHECK(spDisp->QueryInterface(IID_IAtlRare, (void**)&pNo) == E_NOINTERFACE && pNo == NULL);
  }
  CHECK(_Module.GetLockCount() == 0);

  // CComObjectStack: no counting; FinalRelease at the end of the block.
  {
    CComObjectStack<CStackThing> st;
    CHECK(SUCCEEDED(st.m_hResFinalConstruct));
    long n = 0;
    st.Magic(&n);
    CHECK(n == 5);
    CHECK(st.AddRef() == 0);
  }
  CHECK(g_nStackDestroyed == 1);

  // CComEnum over VARIANTs, copied.
  typedef CComObject<CComEnum<IEnumVARIANT, &IID_IEnumVARIANT, VARIANT, _Copy<VARIANT> > > EnumVar;
  VARIANT items[3];
  for (int i = 0; i < 3; i++) {
    VariantInit(&items[i]);
    items[i].vt = VT_I4;
    items[i].lVal = 10 * (i + 1);
  }
  EnumVar* pEnum = NULL;
  CHECK(SUCCEEDED(EnumVar::CreateInstance(&pEnum)));
  if (pEnum != NULL) {
    CHECK(SUCCEEDED(pEnum->Init(items, items + 3, NULL, AtlFlagCopy)));
    CComPtr<IEnumVARIANT> spEnum;
    pEnum->QueryInterface(IID_IEnumVARIANT, (void**)&spEnum);
    VARIANT got[2];
    ULONG n = 0;
    CHECK(spEnum->Next(2, got, &n) == S_OK && n == 2);
    CHECK(got[0].lVal == 10 && got[1].lVal == 20);
    CComPtr<IEnumVARIANT> spClone;
    CHECK(SUCCEEDED(spEnum->Clone(&spClone)));
    CHECK(spEnum->Next(2, got, &n) == S_FALSE && n == 1 && got[0].lVal == 30);
    CHECK(spClone != NULL && spClone->Next(1, got, &n) == S_OK && got[0].lVal == 30);
    spEnum->Reset();
    CHECK(spEnum->Skip(2) == S_OK);
    CHECK(spEnum->Skip(5) == S_FALSE);
    spEnum.Release();
    // The clone keeps the items alive after the first goes.
    spClone->Reset();
    CHECK(spClone->Next(1, got, &n) == S_OK && got[0].lVal == 10);
  }

  // CComBSTR and CComVariant.
  CComBSTR b(L"abc");
  b += L"def";
  CHECK(b.Length() == 6 && b == L"abcdef");
  CComVariant v(42L);
  CHECK(SUCCEEDED(v.ChangeType(VT_BSTR)) && v.vt == VT_BSTR && wcscmp(v.bstrVal, L"42") == 0);
}

static void TestServer(LPCWSTR lpszDll) {
  HMODULE hDll = ::LoadLibraryW(lpszDll);
  CHECK(hDll != NULL);
  if (hDll == NULL) return;
  typedef HRESULT(STDAPICALLTYPE * PFNREG)();
  typedef LONG(WINAPI * PFNLIVE)();
  PFNREG pfnRegister = (PFNREG)::GetProcAddress(hDll, "DllRegisterServer");
  PFNREG pfnUnregister = (PFNREG)::GetProcAddress(hDll, "DllUnregisterServer");
  PFNREG pfnCanUnload = (PFNREG)::GetProcAddress(hDll, "DllCanUnloadNow");
  PFNLIVE pfnLive = (PFNLIVE)::GetProcAddress(hDll, "AtlTestLiveObjects");
  CHECK(pfnRegister != NULL && pfnUnregister != NULL && pfnCanUnload != NULL && pfnLive != NULL);
  if (pfnRegister == NULL || pfnUnregister == NULL || pfnCanUnload == NULL || pfnLive == NULL)
    return;

  // Registration: the .rgs script.
  HRESULT hr = pfnRegister();
  CHECK(hr == S_OK);
  if (hr != S_OK) printf("note: DllRegisterServer: %08lx\n", (unsigned long)hr);
  WCHAR sz[MAX_PATH];
  CHECK(ReadKey(L"AtlTest.Thing", NULL, sz, MAX_PATH) && wcscmp(sz, L"ATL test thing") == 0);
  CHECK(ReadKey(L"AtlTest.Thing\\CurVer", NULL, sz, MAX_PATH) &&
        wcscmp(sz, L"AtlTest.Thing.1") == 0);
  CHECK(ReadKey(L"AtlTest.Thing.1\\CLSID", NULL, sz, MAX_PATH) &&
        wcscmp(sz, L"{1DF77E1F-DA45-421A-BABF-24B927659DF9}") == 0);
  static const WCHAR kThingKey[] = L"CLSID\\{1DF77E1F-DA45-421A-BABF-24B927659DF9}";
  CHECK(ReadKey(kThingKey, NULL, sz, MAX_PATH) && wcscmp(sz, L"ATL test thing") == 0);
  CHECK(ReadKey(L"CLSID\\{1DF77E1F-DA45-421A-BABF-24B927659DF9}\\InprocServer32", NULL, sz,
                MAX_PATH) && EndsWith(sz, kDllName));
  CHECK(ReadKey(L"CLSID\\{1DF77E1F-DA45-421A-BABF-24B927659DF9}\\InprocServer32",
                L"ThreadingModel", sz, MAX_PATH) && wcscmp(sz, L"Apartment") == 0);
  CHECK(ReadKey(L"CLSID\\{1DF77E1F-DA45-421A-BABF-24B927659DF9}\\TypeLib", NULL, sz, MAX_PATH) &&
        wcscmp(sz, L"{B0378114-0AD3-4EBE-AC9F-E195A42D4E79}") == 0);
  {
    HKEY hKey;
    DWORD dw = 0, cb = sizeof dw, type = 0;
    CHECK(::RegOpenKeyExW(HKEY_CLASSES_ROOT, kThingKey, 0, KEY_READ, &hKey) == ERROR_SUCCESS &&
          ::RegQueryValueExW(hKey, L"Flags", NULL, &type, (LPBYTE)&dw, &cb) == ERROR_SUCCESS &&
          type == REG_DWORD && dw == 7);
    ::RegCloseKey(hKey);
  }
  // ... and DECLARE_REGISTRY's keys.
  CHECK(ReadKey(L"AtlTest.Single\\CLSID", NULL, sz, MAX_PATH) &&
        wcscmp(sz, L"{857BF9B8-3ED8-4DEF-9290-1A92ACBCDD34}") == 0);
  CHECK(ReadKey(L"CLSID\\{857BF9B8-3ED8-4DEF-9290-1A92ACBCDD34}", NULL, sz, MAX_PATH) &&
        wcscmp(sz, L"ATL test singleton") == 0);
  CHECK(ReadKey(L"CLSID\\{857BF9B8-3ED8-4DEF-9290-1A92ACBCDD34}\\InprocServer32", NULL, sz,
                MAX_PATH) && EndsWith(sz, kDllName));
  CHECK(ReadKey(L"CLSID\\{857BF9B8-3ED8-4DEF-9290-1A92ACBCDD34}\\ProgID", NULL, sz, MAX_PATH) &&
        wcscmp(sz, L"AtlTest.Single.1") == 0);
  CLSID clsid;
  CHECK(SUCCEEDED(::CLSIDFromProgID(L"AtlTest.Thing", &clsid)) &&
        InlineIsEqualGUID(clsid, CLSID_AtlThing));

  {
    // Calls through the vtable.
    CComPtr<IAtlThing> spThing;
    hr = spThing.CoCreateInstance(CLSID_AtlThing);
    CHECK(SUCCEEDED(hr) && spThing != NULL);
    if (spThing == NULL) {
      printf("note: CoCreateInstance: %08lx\n", (unsigned long)hr);
      pfnUnregister();
      return;
    }
    CHECK(pfnLive() == 1);
    CHECK(pfnCanUnload() == S_FALSE);
    long n = 0;
    CHECK(SUCCEEDED(spThing->put_Value(5)));
    CHECK(SUCCEEDED(spThing->Add(3, &n)) && n == 8);
    CComBSTR name;
    CHECK(SUCCEEDED(spThing->get_Name(&name)) && name == L"Thing 8");

    // ... and through IDispatch, by the type library.
    CComQIPtr<IDispatch> spDisp(spThing.p);
    CHECK(spDisp != NULL);
    UINT nInfo = 0;
    CHECK(spDisp->GetTypeInfoCount(&nInfo) == S_OK && nInfo == 1);
    DISPID dispid = 0;
    LPOLESTR lpszAdd = (LPOLESTR)L"Add";
    hr = spDisp->GetIDsOfNames(IID_NULL, &lpszAdd, 1, LOCALE_USER_DEFAULT, &dispid);
    CHECK(hr == S_OK && dispid == 2);
    if (hr != S_OK) printf("note: GetIDsOfNames: %08lx\n", (unsigned long)hr);
    CComVariant arg(4L), result;
    DISPPARAMS dp = {&arg, NULL, 1, 0};
    hr = spDisp->Invoke(2, IID_NULL, LOCALE_USER_DEFAULT, DISPATCH_METHOD, &dp, &result, NULL,
                        NULL);
    CHECK(hr == S_OK && result.vt == VT_I4 && result.lVal == 12);
    if (hr != S_OK) printf("note: Invoke Add: %08lx\n", (unsigned long)hr);
    DISPPARAMS none = {NULL, NULL, 0, 0};
    result.Clear();
    CHECK(SUCCEEDED(spDisp->Invoke(1, IID_NULL, LOCALE_USER_DEFAULT, DISPATCH_PROPERTYGET, &none,
                                   &result, NULL, NULL)) &&
          result.vt == VT_I4 && result.lVal == 12);
    CComVariant newVal(20L);
    DISPID put = DISPID_PROPERTYPUT;
    DISPPARAMS dpPut = {&newVal, &put, 1, 1};
    CHECK(SUCCEEDED(spDisp->Invoke(1, IID_NULL, LOCALE_USER_DEFAULT, DISPATCH_PROPERTYPUT, &dpPut,
                                   NULL, NULL, NULL)));
    CHECK(SUCCEEDED(spThing->get_Value(&n)) && n == 20);

    // Rich errors.
    CComQIPtr<ISupportErrorInfo> spSEI(spThing.p);
    CHECK(spSEI != NULL && spSEI->InterfaceSupportsErrorInfo(IID_IAtlThing) == S_OK &&
          spSEI->InterfaceSupportsErrorInfo(IID_IDispatch) == S_FALSE);
    CHECK(spThing->Fail(0) == E_FAIL);
    CComPtr<IErrorInfo> spErr;
    CHECK(::GetErrorInfo(0, &spErr) == S_OK && spErr != NULL);
    if (spErr != NULL) {
      CComBSTR desc, source;
      GUID guid;
      spErr->GetDescription(&desc);
      spErr->GetSource(&source);
      spErr->GetGUID(&guid);
      CHECK(desc == L"thing failed");
      CHECK(source == L"AtlTest.Thing.1");
      CHECK(InlineIsEqualGUID(guid, IID_IAtlThing));
      spErr.Release();
    }
    CHECK(spThing->Fail(1) == MAKE_HRESULT(SEVERITY_ERROR, FACILITY_ITF, IDS_FAIL));
    CHECK(::GetErrorInfo(0, &spErr) == S_OK && spErr != NULL);
    if (spErr != NULL) {
      CComBSTR desc;
      spErr->GetDescription(&desc);
      CHECK(desc == L"thing failed by id");
      spErr.Release();
    }
    CHECK(spThing->Fail(2) == DISP_E_EXCEPTION);
    CHECK(::GetErrorInfo(0, &spErr) == S_OK && spErr != NULL);
    if (spErr != NULL) {
      CComBSTR desc;
      spErr->GetDescription(&desc);
      CHECK(desc == L"narrow failure");
      spErr.Release();
    }
    // Through IDispatch, the error comes back as an exception.
    CComVariant kind(0L);
    DISPPARAMS dpFail = {&kind, NULL, 1, 0};
    EXCEPINFO ei = {};
    hr = spDisp->Invoke(3, IID_NULL, LOCALE_USER_DEFAULT, DISPATCH_METHOD, &dpFail, NULL, &ei,
                        NULL);
    CHECK(hr == DISP_E_EXCEPTION);
    CHECK(ei.scode == E_FAIL);
    CHECK(ei.bstrDescription != NULL && wcscmp(ei.bstrDescription, L"thing failed") == 0);
    ::SysFreeString(ei.bstrDescription);
    ::SysFreeString(ei.bstrSource);
    ::SysFreeString(ei.bstrHelpFile);

    // IProvideClassInfo2.
    CComQIPtr<IProvideClassInfo2> spPCI(spThing.p);
    CHECK(spPCI != NULL);
    if (spPCI != NULL) {
      GUID guid;
      CHECK(spPCI->GetGUID(GUIDKIND_DEFAULT_SOURCE_DISP_IID, &guid) == S_OK &&
            InlineIsEqualGUID(guid, DIID__IAtlThingEvents));
      CComPtr<ITypeInfo> spInfo;
      CHECK(spPCI->GetClassInfo(&spInfo) == S_OK && spInfo != NULL);
      TYPEATTR* pAttr = NULL;
      if (spInfo != NULL && SUCCEEDED(spInfo->GetTypeAttr(&pAttr))) {
        CHECK(pAttr->typekind == TKIND_COCLASS && InlineIsEqualGUID(pAttr->guid, CLSID_AtlThing));
        spInfo->ReleaseTypeAttr(pAttr);
      }
    }

    // Events.
    CComObject<CSink>* pSink = NULL;
    CComObject<CSink>::CreateInstance(&pSink);
    CComPtr<IUnknown> spSink(pSink->GetUnknown());
    DWORD dwCookie = 0;
    CHECK(AtlAdvise(spThing, spSink, DIID__IAtlThingEvents, &dwCookie) == S_OK && dwCookie != 0);
    CHECK(SUCCEEDED(spThing->Fire(7)));
    CHECK(pSink->m_nCalls == 1 && pSink->m_nLast == 7);
    CComQIPtr<IConnectionPointContainer> spCPC(spThing.p);
    CHECK(spCPC != NULL);
    if (spCPC != NULL) {
      CComPtr<IConnectionPoint> spCP;
      CHECK(spCPC->FindConnectionPoint(DIID__IAtlThingEvents, &spCP) == S_OK);
      IConnectionPoint* pNone = NULL;
      CHECK(spCPC->FindConnectionPoint(IID_IAtlRare, &pNone) == CONNECT_E_NOCONNECTION);
      if (spCP != NULL) {
        IID iid;
        CHECK(spCP->GetConnectionInterface(&iid) == S_OK &&
              InlineIsEqualGUID(iid, DIID__IAtlThingEvents));
        CComPtr<IConnectionPointContainer> spBack;
        CHECK(spCP->GetConnectionPointContainer(&spBack) == S_OK && spBack.IsEqualObject(spThing));
        // A sink without the interface is refused.
        CComObject<CNotASink>* pNot = NULL;
        CComObject<CNotASink>::CreateInstance(&pNot);
        CComPtr<IUnknown> spNot(pNot->GetUnknown());
        DWORD dw2 = 0;
        CHECK(spCP->Advise(spNot, &dw2) == CONNECT_E_CANNOTCONNECT);
        // The connections.
        CComPtr<IEnumConnections> spEnum;
        CHECK(spCP->EnumConnections(&spEnum) == S_OK);
        if (spEnum != NULL) {
          CONNECTDATA cd[2];
          ULONG nGot = 0;
          CHECK(spEnum->Next(2, cd, &nGot) == S_FALSE && nGot == 1);
          if (nGot == 1) {
            CHECK(cd[0].dwCookie == dwCookie);
            CHECK(cd[0].pUnk != NULL);
            cd[0].pUnk->Release();
          }
        }
        CHECK(spCP->Unadvise(dwCookie + 100) == CONNECT_E_NOCONNECTION);
      }
      CComPtr<IEnumConnectionPoints> spEnumCP;
      CHECK(spCPC->EnumConnectionPoints(&spEnumCP) == S_OK);
      if (spEnumCP != NULL) {
        IConnectionPoint* cps[2] = {};
        ULONG nGot = 0;
        CHECK(spEnumCP->Next(2, cps, &nGot) == S_FALSE && nGot == 1);
        for (ULONG i = 0; i < nGot; i++) cps[i]->Release();
      }
    }
    CHECK(AtlUnadvise(spThing, DIID__IAtlThingEvents, dwCookie) == S_OK);
    CHECK(SUCCEEDED(spThing->Fire(9)));
    CHECK(pSink->m_nCalls == 1);

    // Tear-offs: a new object each time, whose IUnknown is the owner's.
    CComPtr<IAtlRare> spRare1, spRare2;
    CHECK(SUCCEEDED(spThing->QueryInterface(IID_IAtlRare, (void**)&spRare1)));
    CHECK(SUCCEEDED(spThing->QueryInterface(IID_IAtlRare, (void**)&spRare2)));
    CHECK(spRare1 != NULL && spRare2 != NULL && spRare1.p != spRare2.p);
    CHECK(pfnLive() == 3);
    if (spRare1 != NULL) {
      CHECK(SUCCEEDED(spRare1->Magic(&n)) && n == 62);
      CHECK(spRare1.IsEqualObject(spThing));
      CComPtr<IAtlThing> spBack;
      CHECK(SUCCEEDED(spRare1->QueryInterface(IID_IAtlThing, (void**)&spBack)) &&
            spBack.p == spThing.p);
    }
    spRare1.Release();
    spRare2.Release();
    CHECK(pfnLive() == 1);
    // A cached tear-off: the same one each time.
    CComPtr<IAtlCached> spCached1, spCached2;
    CHECK(SUCCEEDED(spThing->QueryInterface(IID_IAtlCached, (void**)&spCached1)));
    CHECK(SUCCEEDED(spThing->QueryInterface(IID_IAtlCached, (void**)&spCached2)));
    CHECK(spCached1 != NULL && spCached1.p == spCached2.p);
    if (spCached1 != NULL) {
      CHECK(SUCCEEDED(spCached1->Magic(&n)) && n == 1020);
      CHECK(spCached1.IsEqualObject(spThing));
    }
  }
  CHECK(pfnLive() == 0);

  // Aggregation: the inner object's interfaces answer to the outer.
  {
    COuter outer;
    IUnknown* pInner = NULL;
    hr = ::CoCreateInstance(CLSID_AtlThing, &outer, CLSCTX_INPROC_SERVER, IID_IUnknown,
                            (void**)&pInner);
    CHECK(hr == S_OK && pInner != NULL);
    IAtlThing* pNo = NULL;
    CHECK(::CoCreateInstance(CLSID_AtlThing, &outer, CLSCTX_INPROC_SERVER, IID_IAtlThing,
                             (void**)&pNo) == CLASS_E_NOAGGREGATION);
    if (pInner != NULL) {
      outer.m_pInner = pInner;
      IAtlThing* pThing = NULL;
      CHECK(SUCCEEDED(outer.QueryInterface(IID_IAtlThing, (void**)&pThing)));
      if (pThing != NULL) {
        long n = 0;
        pThing->put_Value(2);
        CHECK(SUCCEEDED(pThing->Add(1, &n)) && n == 3);
        IUnknown* pUnk = NULL;
        CHECK(SUCCEEDED(pThing->QueryInterface(IID_IUnknown, (void**)&pUnk)) && pUnk == &outer);
        if (pUnk != NULL) pUnk->Release();
        // The outer object does not answer for IDispatch.
        IDispatch* pDisp = NULL;
        CHECK(pThing->QueryInterface(IID_IDispatch, (void**)&pDisp) == E_NOINTERFACE);
        CHECK(outer.m_ref == 2);
        pThing->Release();
      }
      CHECK(outer.m_ref == 1);
      CHECK(pfnLive() == 1);
      pInner->Release();
      CHECK(pfnLive() == 0);
    }
    // The singleton does not aggregate.
    pInner = NULL;
    CHECK(::CoCreateInstance(CLSID_AtlSingle, &outer, CLSCTX_INPROC_SERVER, IID_IUnknown,
                             (void**)&pInner) == CLASS_E_NOAGGREGATION);
  }

  // The singleton: one object for everyone.
  {
    CComPtr<IAtlRare> spA, spB;
    CHECK(SUCCEEDED(spA.CoCreateInstance(CLSID_AtlSingle)));
    CHECK(SUCCEEDED(spB.CoCreateInstance(CLSID_AtlSingle)));
    CHECK(spA != NULL && spA.p == spB.p);
    long n = 0;
    if (spA != NULL) spA->Magic(&n);
    if (spB != NULL) spB->Magic(&n);
    CHECK(n == 2);
    // ... which does not keep the DLL loaded by itself.
  }
  CoFreeUnusedLibraries();
  CHECK(pfnCanUnload() == S_OK);

  // Unregistration: the keys go, but not HKCR\CLSID (NoRemove).
  CHECK(pfnUnregister() == S_OK);
  // A NoRemove key keeps its parent, without the rest.
  CHECK(KeyExists(L"AtlTest.Thing\\Kept"));
  CHECK(!KeyExists(L"AtlTest.Thing\\CLSID"));
  ::RegDeleteKeyW(HKEY_CLASSES_ROOT, L"AtlTest.Thing\\Kept");
  ::RegDeleteKeyW(HKEY_CLASSES_ROOT, L"AtlTest.Thing");
  CHECK(!KeyExists(L"AtlTest.Thing.1"));
  CHECK(!KeyExists(kThingKey));
  CHECK(!KeyExists(L"AtlTest.Single"));
  CHECK(!KeyExists(L"CLSID\\{857BF9B8-3ED8-4DEF-9290-1A92ACBCDD34}"));
  CHECK(KeyExists(L"CLSID"));
  CHECK(FAILED(::CLSIDFromProgID(L"AtlTest.Thing", &clsid)));
  ::FreeLibrary(hDll);
}

int main() {
  ::CoInitialize(NULL);
  TestClientPieces();
  TestModulePieces();
  TestRegistrar();

  // The type library, registered for the server's IDispatchImpl.
  WCHAR szTlb[MAX_PATH];
  ::GetTempPathW(MAX_PATH, szTlb);
  lstrcatW(szTlb, L"atltest.tlb");
  ::DeleteFileW(szTlb);
  BOOL bTlb = WriteTypeLib(szTlb);
  CHECK(bTlb);
  CComPtr<ITypeLib> spLib;
  CHECK(bTlb && SUCCEEDED(::LoadTypeLib(szTlb, &spLib)));
  if (spLib != NULL) CHECK(SUCCEEDED(::RegisterTypeLib(spLib, szTlb, NULL)));

  // The server, beside this program.
  WCHAR szDll[MAX_PATH];
  DWORD n = ::GetModuleFileNameW(NULL, szDll, MAX_PATH);
  while (n > 0 && szDll[n - 1] != L'\\' && szDll[n - 1] != L'/') n--;
  szDll[n] = 0;
  lstrcatW(szDll, kDllName);
  TestServer(szDll);

  if (spLib != NULL) {
    ::UnRegisterTypeLib(LIBID_AtlTestLib, 1, 0, LOCALE_NEUTRAL, SYS_WIN32);
    spLib.Release();
  }
  ::DeleteFileW(szTlb);
  ::CoUninitialize();
  if (g_nFailures == 0) printf("ALL PASSED\n");
  return g_nFailures != 0;
}
