// comdeftest.cpp - the compiler COM support classes of comdef.h, the
// same test on Windows (MinGW's headers and comsupp library) and on the
// portable layer (its own headers):
//   - _bstr_t: from wide and narrow strings, shared copies, the narrow
//     form, concatenation, comparison, copy, Attach, Detach, GetAddress,
//     GetBSTR and Assign;
//   - _variant_t: the types its constructors give, extraction with
//     conversion, ChangeType, comparison, Attach and Detach, SetString, a
//     conversion that fails raising _com_error, and vtMissing;
//   - _com_ptr_t: from pointers of other interfaces (queried), compared
//     by identity, operator&, Attach, Detach, a null one raising
//     E_POINTER, from a _variant_t, and CreateInstance by CLSID and by
//     its string from a class object the test registers;
//   - _com_error: ErrorMessage, the dispatch exception codes, and the
//     error object _com_issue_errorex finds;
//   - _com_dispatch_method, _com_dispatch_propget and _com_dispatch_propput
//     (and the _raw_ forms) against an IDispatch of the test's own.
// Exits with 1 on a failure.
#include <windows.h>
#include <comdef.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

#ifdef _WIN32
// MinGW's comsupp library has what comdef.h's classes call.
__asm__(".section .drectve,\"yn\"\n"
        ".ascii \" -defaultlib:comsuppw -defaultlib:ole32 -defaultlib:oleaut32 -defaultlib:uuid\"\n"
        ".text");
#endif

#ifdef __MINGW32__
// MinGW's comutil.h declares vtMissing, which its comsupp library lacks.
_variant_t vtMissing(DISP_E_PARAMNOTFOUND, VT_ERROR);
#endif

static int g_nFailures;
#define CHECK(cond)                                  \
  do {                                               \
    if (!(cond)) {                                   \
      printf("FAIL line %d: %s\n", __LINE__, #cond); \
      g_nFailures++;                                 \
    }                                                \
  } while (0)

// {6B0E3A52-4D1C-4F27-9C3E-0B5A7D2E9F11}
static const CLSID CLSID_TestObject = {0x6b0e3a52, 0x4d1c, 0x4f27, {0x9c, 0x3e, 0x0b, 0x5a, 0x7d, 0x2e, 0x9f, 0x11}};
static const wchar_t kClsidString[] = L"{6B0E3A52-4D1C-4F27-9C3E-0B5A7D2E9F11}";

enum { DISPID_TADD = 1, DISPID_TVALUE = 2, DISPID_TFAIL = 3, DISPID_TPEER = 4, DISPID_TSCALE = 5 };

// An object with IDispatch (Add, Value, Fail, Peer by reference, Scale),
// IPersist and ISupportErrorInfo.
class CTestObject : public IDispatch, public IPersist, public ISupportErrorInfo {
public:
  CTestObject() : m_nRef(1), m_pPeer(NULL), m_wLastPutFlags(0) { VariantInit(&m_value); }
  virtual ~CTestObject() {
    VariantClear(&m_value);
    if (m_pPeer) m_pPeer->Release();
  }
  STDMETHOD(QueryInterface)(REFIID riid, void** ppv) {
    *ppv = NULL;
    if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_IDispatch))
      *ppv = static_cast<IDispatch*>(this);
    else if (IsEqualIID(riid, IID_IPersist))
      *ppv = static_cast<IPersist*>(this);
    else if (IsEqualIID(riid, IID_ISupportErrorInfo))
      *ppv = static_cast<ISupportErrorInfo*>(this);
    else
      return E_NOINTERFACE;
    AddRef();
    return S_OK;
  }
  STDMETHOD_(ULONG, AddRef)() { return ++m_nRef; }
  STDMETHOD_(ULONG, Release)() {
    ULONG n = --m_nRef;
    if (n == 0) delete this;
    return n;
  }
  STDMETHOD(GetTypeInfoCount)(UINT* p) {
    *p = 0;
    return S_OK;
  }
  STDMETHOD(GetTypeInfo)(UINT, LCID, ITypeInfo**) { return E_NOTIMPL; }
  STDMETHOD(GetIDsOfNames)(REFIID, LPOLESTR*, UINT, LCID, DISPID*) { return E_NOTIMPL; }
  STDMETHOD(Invoke)(DISPID id, REFIID, LCID, WORD wFlags, DISPPARAMS* dp, VARIANT* pResult, EXCEPINFO* pei,
                    UINT*) {
    switch (id) {
      case DISPID_TADD: {
        // Add(long n, BSTR s, VARIANT v): s + n + v as a string.
        if (!(wFlags & DISPATCH_METHOD) || dp->cArgs != 3) return DISP_E_BADPARAMCOUNT;
        if (dp->rgvarg[2].vt != VT_I4 || dp->rgvarg[1].vt != VT_BSTR) return DISP_E_TYPEMISMATCH;
        VARIANT v;
        VariantInit(&v);
        if (FAILED(VariantChangeType(&v, &dp->rgvarg[0], 0, VT_BSTR))) return DISP_E_TYPEMISMATCH;
        wchar_t sz[128];
        swprintf(sz, 128, L"%ls%d%ls", dp->rgvarg[1].bstrVal, (int)dp->rgvarg[2].lVal, v.bstrVal);
        VariantClear(&v);
        if (pResult) {
          pResult->vt = VT_BSTR;
          pResult->bstrVal = SysAllocString(sz);
        }
        return S_OK;
      }
      case DISPID_TVALUE:
        if (wFlags & DISPATCH_PROPERTYGET) {
          if (pResult) VariantCopy(pResult, &m_value);
          return S_OK;
        }
        if (wFlags & DISPATCH_PROPERTYPUT) {
          if (dp->cArgs != 1 || dp->cNamedArgs != 1 || dp->rgdispidNamedArgs[0] != DISPID_PROPERTYPUT)
            return DISP_E_PARAMNOTOPTIONAL;
          m_wLastPutFlags = wFlags;
          return VariantCopy(&m_value, &dp->rgvarg[0]);
        }
        return DISP_E_MEMBERNOTFOUND;
      case DISPID_TFAIL:
        if (pei) {
          memset(pei, 0, sizeof(*pei));
          pei->bstrSource = SysAllocString(L"TestObject");
          pei->bstrDescription = SysAllocString(L"It failed on purpose");
          pei->dwHelpContext = 77;
          pei->scode = E_FAIL;
        }
        return DISP_E_EXCEPTION;
      case DISPID_TPEER:
        // Only by reference.
        if (wFlags & DISPATCH_PROPERTYPUTREF) {
          if (dp->cArgs != 1 || dp->rgvarg[0].vt != VT_DISPATCH) return DISP_E_TYPEMISMATCH;
          m_wLastPutFlags = wFlags;
          if (m_pPeer) m_pPeer->Release();
          m_pPeer = dp->rgvarg[0].pdispVal;
          if (m_pPeer) m_pPeer->AddRef();
          return S_OK;
        }
        if (wFlags & DISPATCH_PROPERTYGET) {
          if (pResult) {
            pResult->vt = VT_DISPATCH;
            pResult->pdispVal = m_pPeer;
            if (m_pPeer) m_pPeer->AddRef();
          }
          return S_OK;
        }
        return DISP_E_MEMBERNOTFOUND;
      case DISPID_TSCALE:
        // Scale(double, short*): the double times *p, into *p as well.
        if (dp->cArgs != 2 || dp->rgvarg[0].vt != (VT_I2 | VT_BYREF) || dp->rgvarg[1].vt != VT_R8)
          return DISP_E_TYPEMISMATCH;
        *dp->rgvarg[0].piVal = (SHORT)(dp->rgvarg[1].dblVal * *dp->rgvarg[0].piVal);
        if (pResult) {
          pResult->vt = VT_R8;
          pResult->dblVal = *dp->rgvarg[0].piVal;
        }
        return S_OK;
    }
    return DISP_E_MEMBERNOTFOUND;
  }
  STDMETHOD(GetClassID)(CLSID* p) {
    *p = CLSID_TestObject;
    return S_OK;
  }
  STDMETHOD(InterfaceSupportsErrorInfo)(REFIID riid) {
    return IsEqualIID(riid, IID_IDispatch) ? S_OK : S_FALSE;
  }

  ULONG m_nRef;
  VARIANT m_value;
  IDispatch* m_pPeer;
  WORD m_wLastPutFlags;
};

class CTestFactory : public IClassFactory {
public:
  STDMETHOD(QueryInterface)(REFIID riid, void** ppv) {
    if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_IClassFactory)) {
      *ppv = this;
      return S_OK;
    }
    *ppv = NULL;
    return E_NOINTERFACE;
  }
  STDMETHOD_(ULONG, AddRef)() { return 2; }
  STDMETHOD_(ULONG, Release)() { return 1; }
  STDMETHOD(CreateInstance)(IUnknown* pOuter, REFIID riid, void** ppv) {
    if (pOuter) return CLASS_E_NOAGGREGATION;
    CTestObject* p = new CTestObject;
    HRESULT hr = p->QueryInterface(riid, ppv);
    p->Release();
    return hr;
  }
  STDMETHOD(LockServer)(BOOL) { return S_OK; }
};

static void TestBstr() {
  _bstr_t empty;
  CHECK(!empty);
  CHECK(empty.length() == 0);
  CHECK((const wchar_t*)empty == NULL);
  _bstr_t a(L"Hello");
  CHECK(!!a);
  CHECK(a.length() == 5);
  CHECK(wcscmp(a, L"Hello") == 0);
  CHECK(strcmp((const char*)a, "Hello") == 0);
  // Copies share one BSTR.
  _bstr_t b = a;
  CHECK((const wchar_t*)b == (const wchar_t*)a);
  _bstr_t c("World");
  CHECK(wcscmp(c, L"World") == 0);
  _bstr_t d = a + L", " + c;
  CHECK(wcscmp(d, L"Hello, World") == 0);
  CHECK(d.length() == 12);
  CHECK(strcmp((const char*)d, "Hello, World") == 0);
  a += "!";
  CHECK(wcscmp(a, L"Hello!") == 0);
  CHECK(wcscmp(b, L"Hello") == 0);
  _bstr_t e = "x" + c;
  CHECK(wcscmp(e, L"xWorld") == 0);
  // Comparison.
  CHECK(b == _bstr_t("Hello"));
  CHECK(b != c);
  CHECK(b < c && c > b && b <= b && c >= b);
  CHECK(_bstr_t(L"abc") < _bstr_t(L"abcd"));
  // No string at all is less than an empty one.
  CHECK(empty != _bstr_t(L"") && empty < _bstr_t(L""));
  CHECK(empty == _bstr_t());
  // copy, Attach, Detach, GetAddress, GetBSTR, Assign.
  BSTR copy = b.copy();
  CHECK(copy != (BSTR)(const wchar_t*)b && wcscmp(copy, L"Hello") == 0);
  CHECK(b.copy(false) == (BSTR)(const wchar_t*)b);
  _bstr_t f;
  f.Attach(copy);
  CHECK((BSTR)(const wchar_t*)f == copy);
  BSTR det = f.Detach();
  CHECK(det == copy && !f);
  SysFreeString(det);
  _bstr_t g(L"old");
  BSTR* pb = g.GetAddress();
  CHECK(*pb == NULL);
  *pb = SysAllocString(L"out");
  CHECK(wcscmp(g, L"out") == 0);
  // GetBSTR is the string itself, shared with copies.
  _bstr_t h(L"out");
  BSTR& rb = h.GetBSTR();
  rb[0] = L'p';
  CHECK(wcscmp(h, L"put") == 0);
  BSTR src = SysAllocString(L"assigned");
  h.Assign(src);
  CHECK(wcscmp(h, L"assigned") == 0 && (BSTR)(const wchar_t*)h != src);
  SysFreeString(src);
  // From a BSTR taken (fCopy false) or copied, and from a variant.
  _bstr_t i(SysAllocString(L"taken"), false);
  CHECK(wcscmp(i, L"taken") == 0);
  _variant_t vn((long)42);
  _bstr_t j(vn);
  CHECK(wcscmp(j, L"42") == 0);
  // Characters outside ASCII survive the round trip in UTF-16/32.
  _bstr_t k(L"caf\x00e9");
  CHECK(k.length() == 4 && ((const wchar_t*)k)[3] == 0x00e9);
}

static void TestVariant() {
  _variant_t v;
  CHECK(v.vt == VT_EMPTY);
  CHECK(_variant_t((short)3).vt == VT_I2);
  CHECK(_variant_t((long)3).vt == VT_I4);
  CHECK(_variant_t((long)DISP_E_PARAMNOTFOUND, VT_ERROR).vt == VT_ERROR);
  CHECK(_variant_t(1.5f).vt == VT_R4);
  CHECK(_variant_t(2.5).vt == VT_R8);
  CHECK(_variant_t(2.5, VT_DATE).vt == VT_DATE);
  CHECK(_variant_t(true).vt == VT_BOOL && _variant_t(true).boolVal == VARIANT_TRUE);
  CHECK(_variant_t((BYTE)7).vt == VT_UI1);
  CHECK(_variant_t((unsigned short)7).vt == VT_UI2);
  CHECK(_variant_t((unsigned long)7).vt == VT_UI4);
  CHECK(_variant_t((LONGLONG)7).vt == VT_I8);
  CHECK(_variant_t((ULONGLONG)7).vt == VT_UI8);
  CHECK(_variant_t(L"wide").vt == VT_BSTR);
  CHECK(_variant_t("narrow").vt == VT_BSTR);
  CHECK(wcscmp(_variant_t("narrow").bstrVal, L"narrow") == 0);
  CHECK(_variant_t(_bstr_t(L"b")).vt == VT_BSTR);
  CY cy;
  cy.int64 = 12345;
  CHECK(_variant_t(cy).vt == VT_CY);
  DECIMAL dec;
  memset(&dec, 0, sizeof(dec));
  dec.Lo32 = 99;
  _variant_t vdec(dec);
  CHECK(vdec.vt == VT_DECIMAL && vdec.decVal.Lo32 == 99);

  // Extraction converts.
  _variant_t s(L"123");
  CHECK((long)s == 123);
  CHECK((short)s == 123);
  CHECK((double)s == 123.0);
  CHECK((BYTE)s == 123);
  _variant_t n((long)7);
  CHECK(wcscmp((const wchar_t*)(_bstr_t)n, L"7") == 0);
  CHECK((bool)n);
  CHECK((float)_variant_t(2.5) == 2.5f);
  CHECK((LONGLONG)_variant_t((long)-5) == -5);
  CHECK(((CY)_variant_t((long)2)).int64 == 20000);
  // ChangeType in place and from another.
  _variant_t ct(L"77");
  ct.ChangeType(VT_I4);
  CHECK(ct.vt == VT_I4 && ct.lVal == 77);
  _variant_t ct2;
  ct2.ChangeType(VT_BSTR, &ct);
  CHECK(ct2.vt == VT_BSTR && wcscmp(ct2.bstrVal, L"77") == 0);
  // Assignment keeps a matching type, else takes the new one.
  _variant_t as(L"x");
  as = (long)5;
  CHECK(as.vt == VT_I4 && as.lVal == 5);
  as = (short)6;
  CHECK(as.vt == VT_I2 && as.iVal == 6);
  as = L"str";
  CHECK(as.vt == VT_BSTR && wcscmp(as.bstrVal, L"str") == 0);
  as.SetString("set");
  CHECK(as.vt == VT_BSTR && wcscmp(as.bstrVal, L"set") == 0);
  as = 1.25;
  CHECK(as.vt == VT_R8 && as.dblVal == 1.25);
  as = true;
  CHECK(as.vt == VT_BOOL);
  // Comparison is of type and value.
  CHECK(_variant_t(L"a") == _variant_t(L"a"));
  CHECK(_variant_t(L"a") != _variant_t(L"b"));
  CHECK(_variant_t((long)1) != _variant_t((short)1));
  CHECK(_variant_t((long)1) == _variant_t((long)1));
  // Attach, Detach, Clear, copies.
  VARIANT raw;
  VariantInit(&raw);
  raw.vt = VT_BSTR;
  raw.bstrVal = SysAllocString(L"raw");
  _variant_t at;
  at.Attach(raw);
  CHECK(raw.vt == VT_EMPTY && at.vt == VT_BSTR);
  VARIANT out = at.Detach();
  CHECK(at.vt == VT_EMPTY && out.vt == VT_BSTR && wcscmp(out.bstrVal, L"raw") == 0);
  _variant_t taken(out, false);
  CHECK(out.vt == VT_EMPTY && taken.vt == VT_BSTR);
  _variant_t cp(taken);
  CHECK(cp.bstrVal != taken.bstrVal && wcscmp(cp.bstrVal, L"raw") == 0);
  cp.Clear();
  CHECK(cp.vt == VT_EMPTY);
  // A conversion that cannot be made raises _com_error.
  bool bThrown = false;
  try {
    _variant_t bad(L"not a number");
    long l = bad;
    (void)l;
  } catch (const _com_error& e) {
    bThrown = true;
    CHECK(e.Error() == DISP_E_TYPEMISMATCH);
  }
  CHECK(bThrown);
  // vtMissing: the argument left out.
  CHECK(vtMissing.vt == VT_ERROR && vtMissing.scode == DISP_E_PARAMNOTFOUND);
}

static void TestPtr() {
  CTestObject* pObj = new CTestObject;
  {
    IDispatchPtr spDisp(static_cast<IDispatch*>(pObj));
    CHECK(pObj->m_nRef == 2);
    // Another interface's pointer is queried for.
    IPersistPtr spPersist(spDisp);
    CHECK(spPersist != NULL);
    CHECK(pObj->m_nRef == 3);
    CLSID clsid;
    CHECK(spPersist->GetClassID(&clsid) == S_OK && IsEqualCLSID(clsid, CLSID_TestObject));
    // The same object through other interfaces is equal.
    CHECK(spPersist == spDisp);
    IUnknownPtr spUnk = spPersist;
    CHECK(spUnk == spDisp);
    CHECK(spUnk.GetInterfacePtr() == static_cast<IUnknown*>(static_cast<IDispatch*>(pObj)));
    // An interface it does not have: empty, no error.
    IStreamPtr spStream(spDisp);
    CHECK(spStream == NULL);
    CHECK(!spStream);
    // Detach and Attach.
    IDispatch* pRaw = spDisp.Detach();
    CHECK(spDisp == NULL && pObj->m_nRef == 4);
    spDisp.Attach(pRaw);
    CHECK(pObj->m_nRef == 4);
    // operator& releases and gives the address for an [out] pointer.
    IDispatchPtr spOut(spDisp);
    CHECK(pObj->m_nRef == 5);
    IDispatch** ppOut = &spOut;
    CHECK(*ppOut == NULL && pObj->m_nRef == 4);
    spDisp->QueryInterface(IID_IDispatch, (void**)ppOut);
    CHECK(spOut == spDisp && pObj->m_nRef == 5);
    // From a variant.
    _variant_t var(static_cast<IDispatch*>(pObj));
    IPersistPtr spFromVar(var);
    CHECK(spFromVar != NULL);
    // Assigning NULL.
    spFromVar = NULL;
    CHECK(spFromVar == NULL);
  }
  CHECK(pObj->m_nRef == 1);
  pObj->Release();

  // A null pointer's -> raises E_POINTER.
  bool bThrown = false;
  try {
    IDispatchPtr spNull;
    UINT n;
    spNull->GetTypeInfoCount(&n);
  } catch (const _com_error& e) {
    bThrown = true;
    CHECK(e.Error() == E_POINTER);
  }
  CHECK(bThrown);

  // CreateInstance from the class object the test registered.
  IDispatchPtr spNew;
  CHECK(spNew.CreateInstance(CLSID_TestObject, NULL, CLSCTX_INPROC_SERVER | CLSCTX_LOCAL_SERVER) == S_OK);
  CHECK(spNew != NULL);
  IPersistPtr spByString;
  CHECK(spByString.CreateInstance(kClsidString, NULL, CLSCTX_INPROC_SERVER | CLSCTX_LOCAL_SERVER) == S_OK);
  CHECK(spByString != NULL && spByString != spNew);
  IPersistPtr spCtor(CLSID_TestObject, NULL, CLSCTX_INPROC_SERVER | CLSCTX_LOCAL_SERVER);
  CHECK(spCtor != NULL);
  // A class that is not there.
  IDispatchPtr spNone;
  CHECK(FAILED(spNone.CreateInstance(L"{00000000-1111-2222-3333-444444444444}", NULL,
                                     CLSCTX_INPROC_SERVER | CLSCTX_LOCAL_SERVER)));
  CHECK(spNone == NULL);
}

static void TestError() {
  _com_error e(E_NOINTERFACE);
  CHECK(e.Error() == E_NOINTERFACE);
  CHECK(e.WCode() == 0);
  CHECK(e.ErrorMessage() != NULL && e.ErrorMessage()[0] != 0);
  CHECK(e.ErrorInfo() == NULL);
  CHECK(!e.Description());
  HRESULT hrW = _com_error::WCodeToHRESULT(5);
  CHECK(_com_error::HRESULTToWCode(hrW) == 5);
  _com_error eW(hrW);
  CHECK(eW.WCode() == 5);
  const TCHAR* pszW = eW.ErrorMessage();
  CHECK(pszW != NULL && pszW[0] != 0);
  _com_error eCopy = eW;
  CHECK(eCopy.Error() == hrW);

  // _com_issue_errorex finds the thread's error object when the object
  // says it gives them.
  CTestObject* pObj = new CTestObject;
  ICreateErrorInfo* pcei = NULL;
  CHECK(CreateErrorInfo(&pcei) == S_OK);
  pcei->SetDescription((LPOLESTR)L"Described");
  pcei->SetSource((LPOLESTR)L"Src");
  IErrorInfo* pei = NULL;
  pcei->QueryInterface(IID_IErrorInfo, (void**)&pei);
  pcei->Release();
  SetErrorInfo(0, pei);
  pei->Release();
  bool bThrown = false;
  try {
    _com_issue_errorex(E_FAIL, static_cast<IDispatch*>(pObj), IID_IDispatch);
  } catch (const _com_error& err) {
    bThrown = true;
    CHECK(err.Error() == E_FAIL);
    CHECK(err.Description() == _bstr_t(L"Described"));
    CHECK(err.Source() == _bstr_t(L"Src"));
  }
  CHECK(bThrown);
  pObj->Release();
}

static void TestDispatch() {
  CTestObject* pObj = new CTestObject;
  IDispatch* pDisp = static_cast<IDispatch*>(pObj);
  // A method with a long, a BSTR and a VARIANT (by its address).
  BSTR bstrResult = NULL;
  _bstr_t bstrIn(L"n=");
  _variant_t varIn(L"!");
  HRESULT hr = _com_dispatch_method(pDisp, DISPID_TADD, DISPATCH_METHOD, VT_BSTR, &bstrResult, L"\x0003\x0008\x000c",
                                    (long)12, (BSTR)bstrIn, &varIn);
  CHECK(hr == S_OK);
  CHECK(bstrResult != NULL && wcscmp(bstrResult, L"n=12!") == 0);
  SysFreeString(bstrResult);
  // The result converted to the type asked for.
  VARIANT varResult;
  VariantInit(&varResult);
  hr = _com_dispatch_method(pDisp, DISPID_TADD, DISPATCH_METHOD, VT_VARIANT, &varResult, L"\x0003\x0008\x000c",
                            (long)1, (BSTR)bstrIn, &varIn);
  CHECK(hr == S_OK && varResult.vt == VT_BSTR);
  VariantClear(&varResult);
  _variant_t varNum((long)5);
  LONG lResult = 0;
  _bstr_t bstrEmpty(L"");
  hr = _com_dispatch_method(pDisp, DISPID_TADD, DISPATCH_METHOD, VT_I4, &lResult, L"\x0003\x0008\x000c", (long)4,
                            (BSTR)bstrEmpty, &varNum);
  CHECK(hr == S_OK && lResult == 45);
  // A double by value and a short by reference.
  SHORT sVal = 3;
  double dResult = 0;
  hr = _com_dispatch_method(pDisp, DISPID_TSCALE, DISPATCH_METHOD, VT_R8, &dResult, L"\x0005\x4002", 2.0, &sVal);
  CHECK(hr == S_OK && sVal == 6 && dResult == 6.0);

  // Properties: put by value, get back in another type.
  hr = _com_dispatch_propput(pDisp, DISPID_TVALUE, VT_I4, (long)321);
  CHECK(hr == S_OK && pObj->m_value.vt == VT_I4 && pObj->m_value.lVal == 321);
  CHECK(pObj->m_wLastPutFlags == DISPATCH_PROPERTYPUT);
  BSTR bstrProp = NULL;
  hr = _com_dispatch_propget(pDisp, DISPID_TVALUE, VT_BSTR, &bstrProp);
  CHECK(hr == S_OK && bstrProp != NULL && wcscmp(bstrProp, L"321") == 0);
  SysFreeString(bstrProp);
  hr = _com_dispatch_propput(pDisp, DISPID_TVALUE, VT_BSTR, (BSTR)bstrIn);
  CHECK(hr == S_OK && pObj->m_value.vt == VT_BSTR);
  hr = _com_dispatch_raw_propput(pDisp, DISPID_TVALUE, VT_R8, 0.5);
  CHECK(hr == S_OK && pObj->m_value.vt == VT_R8 && pObj->m_value.dblVal == 0.5);
  double dProp = 0;
  hr = _com_dispatch_raw_propget(pDisp, DISPID_TVALUE, VT_R8, &dProp);
  CHECK(hr == S_OK && dProp == 0.5);
  // An object property: by reference.
  CTestObject* pPeer = new CTestObject;
  hr = _com_dispatch_propput(pDisp, DISPID_TPEER, VT_DISPATCH, static_cast<IDispatch*>(pPeer));
  CHECK(hr == S_OK && pObj->m_pPeer == static_cast<IDispatch*>(pPeer));
  CHECK(pObj->m_wLastPutFlags == DISPATCH_PROPERTYPUTREF);
  IDispatch* pGot = NULL;
  hr = _com_dispatch_propget(pDisp, DISPID_TPEER, VT_DISPATCH, &pGot);
  CHECK(hr == S_OK && pGot == static_cast<IDispatch*>(pPeer));
  if (pGot) pGot->Release();
  pPeer->Release();

  // A dispatch exception: _com_error with its description.
  bool bThrown = false;
  try {
    _com_dispatch_method(pDisp, DISPID_TFAIL, DISPATCH_METHOD, VT_EMPTY, NULL, NULL);
  } catch (const _com_error& e) {
    bThrown = true;
    CHECK(e.Error() == E_FAIL);
    CHECK(e.Description() == _bstr_t(L"It failed on purpose"));
    CHECK(e.Source() == _bstr_t(L"TestObject"));
    CHECK(e.HelpContext() == 77);
  }
  CHECK(bThrown);
  // The raw form returns it and leaves the error object.
  SetErrorInfo(0, NULL);
  hr = _com_dispatch_raw_method(pDisp, DISPID_TFAIL, DISPATCH_METHOD, VT_EMPTY, NULL, NULL);
  CHECK(hr == E_FAIL);
  IErrorInfo* pei = NULL;
  CHECK(GetErrorInfo(0, &pei) == S_OK && pei != NULL);
  if (pei) {
    BSTR bstrDesc = NULL;
    pei->GetDescription(&bstrDesc);
    CHECK(bstrDesc != NULL && wcscmp(bstrDesc, L"It failed on purpose") == 0);
    SysFreeString(bstrDesc);
    pei->Release();
  }
  // A member that is not there.
  hr = _com_dispatch_raw_method(pDisp, 99, DISPATCH_METHOD, VT_EMPTY, NULL, NULL);
  CHECK(hr == DISP_E_MEMBERNOTFOUND);
  bThrown = false;
  try {
    _com_dispatch_method(pDisp, 99, DISPATCH_METHOD, VT_EMPTY, NULL, NULL);
  } catch (const _com_error& e) {
    bThrown = true;
    CHECK(e.Error() == DISP_E_MEMBERNOTFOUND);
  }
  CHECK(bThrown);
  pObj->Release();
}

int main() {
  if (FAILED(CoInitialize(NULL))) {
    printf("CoInitialize failed\n");
    return 1;
  }
  static CTestFactory factory;
  DWORD dwReg = 0;
  if (CoRegisterClassObject(CLSID_TestObject, &factory, CLSCTX_INPROC_SERVER | CLSCTX_LOCAL_SERVER,
                            REGCLS_MULTIPLEUSE, &dwReg) != S_OK)
    printf("CoRegisterClassObject failed\n");
  static const struct {
    const char* name;
    void (*fn)();
  } tests[] = {{"bstr", TestBstr}, {"variant", TestVariant}, {"ptr", TestPtr}, {"error", TestError},
               {"dispatch", TestDispatch}};
  for (const auto& t : tests) {
    try {
      t.fn();
    } catch (const _com_error& e) {
      printf("%s: uncaught _com_error 0x%08lx\n", t.name, (unsigned long)(DWORD)e.Error());
      g_nFailures++;
    }
  }
  CoRevokeClassObject(dwReg);
  CoUninitialize();
  if (g_nFailures) {
    printf("%d failure(s)\n", g_nFailures);
    return 1;
  }
  printf("comdeftest: ok\n");
  return 0;
}
