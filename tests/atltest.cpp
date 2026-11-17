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
// CComObjectStack, CComEnum, CComPtr, CComBSTR and CComVariant.
#include <atlbase.h>
#include <atlcom.h>
#include <stdio.h>
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
