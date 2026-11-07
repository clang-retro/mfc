// autotest.cpp - console tests for automation servers: dispatch maps
// driven through IDispatch (and through COleDispatchDriver), interface
// maps, aggregation, COleObjectFactory in the same process, the registry
// entries it writes, and a call to this program started again by COM as
// a local server.
#include <afx.h>
#include <afxwin.h>
#include <afxdisp.h>
#include <stdio.h>

static int g_failures = 0;
#define CHECK(cond)                                                   \
  do {                                                                \
    if (!(cond)) {                                                    \
      printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);          \
      g_failures++;                                                   \
    }                                                                 \
  } while (0)

// ---------------------------------------------------------------------
// An automation object.

class CCalc : public CCmdTarget {
  DECLARE_DYNCREATE(CCalc)
  DECLARE_OLECREATE(CCalc)
public:
  static int s_nAlive;
  long m_nCount;
  CString m_strTitle;
  BOOL m_bEnabled;
  int m_nEnabledChanged;
  COleVariant m_varTag;
  double m_dTotal;
  double m_dItems[4];

  CCalc() : m_nCount(0), m_bEnabled(FALSE), m_nEnabledChanged(0), m_dTotal(0) {
    for (int i = 0; i < 4; i++) m_dItems[i] = 0;
    EnableAutomation();
    EnableAggregation();
    AfxOleLockApp();
    s_nAlive++;
  }
  virtual ~CCalc() {
    s_nAlive--;
    AfxOleUnlockApp();
  }

  long Add(long a, long b) { return a + b; }
  BSTR Concat(LPCTSTR a, LPCTSTR b) { return (CString(a) + b).AllocSysString(); }
  double Scale(double d, short n) { return d * n; }
  BOOL Not(BOOL b) { return !b; }
  void Swap(long* a, long* b) {
    long t = *a;
    *a = *b;
    *b = t;
  }
  void Bang(BSTR* p) {
    CString s(*p);
    s += _T("!");
    ::SysFreeString(*p);
    *p = s.AllocSysString();
  }
  void Flip(BOOL* pb) { *pb = !*pb; }
  VARIANT Echo(const VARIANT& v) {
    VARIANT r;
    ::VariantInit(&r);
    ::VariantCopy(&r, const_cast<VARIANT*>(&v));
    return r;
  }
  void Fail() { AfxThrowOleDispatchException(1234, _T("It failed")); }
  LPDISPATCH Self() { return GetIDispatch(TRUE); }
  CY Money(CY c) {
    c.int64 *= 2;
    return c;
  }
  long Answer() const { return 42; }
  long ProcessId() { return (long)::GetCurrentProcessId(); }
  double GetTotal() { return m_dTotal; }
  void SetTotal(double d) { m_dTotal = d; }
  double GetItem(short i) {
    if (i < 0 || i >= 4) AfxThrowOleDispatchException(1, _T("Bad index"));
    return m_dItems[i];
  }
  void SetItem(short i, double d) {
    if (i < 0 || i >= 4) AfxThrowOleDispatchException(1, _T("Bad index"));
    m_dItems[i] = d;
  }
  BSTR GetVersion() { return ::SysAllocString(L"1.0"); }
  void OnEnabledChanged() { m_nEnabledChanged++; }

  DECLARE_DISPATCH_MAP()
};
int CCalc::s_nAlive = 0;

IMPLEMENT_DYNCREATE(CCalc, CCmdTarget)

BEGIN_DISPATCH_MAP(CCalc, CCmdTarget)
  DISP_PROPERTY(CCalc, "Count", m_nCount, VT_I4)
  DISP_PROPERTY(CCalc, "Title", m_strTitle, VT_BSTR)
  DISP_PROPERTY_NOTIFY(CCalc, "Enabled", m_bEnabled, OnEnabledChanged, VT_BOOL)
  DISP_PROPERTY(CCalc, "Tag", m_varTag, VT_VARIANT)
  DISP_PROPERTY_EX(CCalc, "Total", GetTotal, SetTotal, VT_R8)
  DISP_PROPERTY_PARAM(CCalc, "Item", GetItem, SetItem, VT_R8, VTS_I2)
  DISP_FUNCTION(CCalc, "Add", Add, VT_I4, VTS_I4 VTS_I4)
  DISP_FUNCTION(CCalc, "Concat", Concat, VT_BSTR, VTS_BSTR VTS_BSTR)
  DISP_FUNCTION(CCalc, "Scale", Scale, VT_R8, VTS_R8 VTS_I2)
  DISP_FUNCTION(CCalc, "Not", Not, VT_BOOL, VTS_BOOL)
  DISP_FUNCTION(CCalc, "Swap", Swap, VT_EMPTY, VTS_PI4 VTS_PI4)
  DISP_FUNCTION(CCalc, "Bang", Bang, VT_EMPTY, VTS_PBSTR)
  DISP_FUNCTION(CCalc, "Flip", Flip, VT_EMPTY, VTS_PBOOL)
  DISP_FUNCTION(CCalc, "Echo", Echo, VT_VARIANT, VTS_VARIANT)
  DISP_FUNCTION(CCalc, "Fail", Fail, VT_EMPTY, VTS_NONE)
  DISP_FUNCTION(CCalc, "Self", Self, VT_DISPATCH, VTS_NONE)
  DISP_FUNCTION(CCalc, "Money", Money, VT_CY, VTS_CY)
  DISP_FUNCTION_ID(CCalc, "Answer", 42, Answer, VT_I4, VTS_NONE)
  DISP_FUNCTION(CCalc, "ProcessId", ProcessId, VT_I4, VTS_NONE)
  DISP_PROPERTY_EX(CCalc, "Version", GetVersion, SetNotSupported, VT_BSTR)
  DISP_DEFVALUE(CCalc, "Total")
END_DISPATCH_MAP()

// {6C1D4E2A-8B7F-4C3D-9A51-2E6F0B9D7C11}
IMPLEMENT_OLECREATE(CCalc, "MfcTest.Calc", 0x6c1d4e2a, 0x8b7f, 0x4c3d, 0x9a, 0x51, 0x2e, 0x6f, 0x0b,
                    0x9d, 0x7c, 0x11)
static const CLSID CLSID_Calc = {0x6c1d4e2a, 0x8b7f, 0x4c3d,
                                 {0x9a, 0x51, 0x2e, 0x6f, 0x0b, 0x9d, 0x7c, 0x11}};

// A derived class adds to the map; the base's members get new IDs.
class CSciCalc : public CCalc {
  DECLARE_DYNCREATE(CSciCalc)
public:
  double Square(double d) { return d * d; }
  DECLARE_DISPATCH_MAP()
};
IMPLEMENT_DYNCREATE(CSciCalc, CCalc)
BEGIN_DISPATCH_MAP(CSciCalc, CCalc)
  DISP_FUNCTION(CSciCalc, "Square", Square, VT_R8, VTS_R8)
END_DISPATCH_MAP()

// ---------------------------------------------------------------------
// Helpers for calling through IDispatch directly.

static DISPID IdOf(LPDISPATCH p, LPCWSTR pszName) {
  DISPID id = DISPID_UNKNOWN;
  LPOLESTR psz = const_cast<LPOLESTR>(pszName);
  if (FAILED(p->GetIDsOfNames(IID_NULL, &psz, 1, 0, &id))) return DISPID_UNKNOWN;
  return id;
}

// Calls with the arguments given first to last.
static HRESULT Call(LPDISPATCH p, DISPID id, WORD wFlags, VARIANT* pResult, int nArgs = 0,
                    VARIANT* pArgs = NULL, UINT* puArgErr = NULL, EXCEPINFO* pInfo = NULL) {
  VARIANT rev[8];
  for (int i = 0; i < nArgs; i++) rev[i] = pArgs[nArgs - 1 - i];
  DISPID idPut = DISPID_PROPERTYPUT;
  DISPPARAMS dp = {nArgs ? rev : NULL, NULL, (UINT)nArgs, 0};
  if (wFlags & DISPATCH_PROPERTYPUT) {
    dp.rgdispidNamedArgs = &idPut;
    dp.cNamedArgs = 1;
  }
  EXCEPINFO info;
  memset(&info, 0, sizeof(info));
  UINT uArgErr = (UINT)-1;
  if (pResult != NULL) ::VariantInit(pResult);
  HRESULT hr = p->Invoke(id, IID_NULL, 0, wFlags, &dp, pResult, pInfo ? pInfo : &info,
                         puArgErr ? puArgErr : &uArgErr);
  if (pInfo == NULL) {
    ::SysFreeString(info.bstrSource);
    ::SysFreeString(info.bstrDescription);
    ::SysFreeString(info.bstrHelpFile);
  }
  return hr;
}

static VARIANT I4(long l) {
  VARIANT v;
  v.vt = VT_I4;
  v.lVal = l;
  return v;
}

static void TestDispatch() {
  CCalc* pCalc = new CCalc;
  LPDISPATCH p = pCalc->GetIDispatch(FALSE);
  CHECK(p != NULL);
  CHECK(CCmdTarget::FromIDispatch(p) == pCalc);
  CHECK(CCmdTarget::FromIDispatch(NULL) == NULL);

  // IDs by position, and an explicit one.
  CHECK(IdOf(p, L"Count") == 1);
  CHECK(IdOf(p, L"count") == 1);
  CHECK(IdOf(p, L"Add") == 7);
  CHECK(IdOf(p, L"Answer") == 42);
  CHECK(IdOf(p, L"NoSuchThing") == DISPID_UNKNOWN);
  UINT nInfo = 99;
  CHECK(p->GetTypeInfoCount(&nInfo) == S_OK && nInfo == 0);

  VARIANT r;
  VARIANT args[4];

  // A method.
  args[0] = I4(2);
  args[1] = I4(3);
  CHECK(Call(p, IdOf(p, L"Add"), DISPATCH_METHOD, &r, 2, args) == S_OK);
  CHECK(r.vt == VT_I4 && r.lVal == 5);
  // Arguments are converted: a string and a double to long.
  args[0].vt = VT_BSTR;
  args[0].bstrVal = ::SysAllocString(L"40");
  args[1].vt = VT_R8;
  args[1].dblVal = 2.0;
  CHECK(Call(p, IdOf(p, L"Add"), DISPATCH_METHOD, &r, 2, args) == S_OK);
  CHECK(r.vt == VT_I4 && r.lVal == 42);
  ::SysFreeString(args[0].bstrVal);
  // The wrong number of arguments, and one that does not convert.
  CHECK(Call(p, IdOf(p, L"Add"), DISPATCH_METHOD, &r, 1, args) == DISP_E_BADPARAMCOUNT);
  args[0] = I4(1);
  args[1].vt = VT_BSTR;
  args[1].bstrVal = ::SysAllocString(L"many");
  UINT uArgErr = 99;
  CHECK(Call(p, IdOf(p, L"Add"), DISPATCH_METHOD, &r, 2, args, &uArgErr) == DISP_E_TYPEMISMATCH);
  CHECK(uArgErr == 0);  // rgvarg index: the last argument comes first
  ::SysFreeString(args[1].bstrVal);
  // A method is not a property to put.
  args[0] = I4(1);
  CHECK(Call(p, IdOf(p, L"Add"), DISPATCH_PROPERTYPUT, NULL, 1, args) == DISP_E_MEMBERNOTFOUND);
  CHECK(Call(p, 9999, DISPATCH_METHOD, &r) == DISP_E_MEMBERNOTFOUND);

  // Strings in and out (LPCTSTR parameters in either build).
  args[0].vt = VT_BSTR;
  args[0].bstrVal = ::SysAllocString(L"foo");
  args[1].vt = VT_BSTR;
  args[1].bstrVal = ::SysAllocString(L"bar");
  CHECK(Call(p, IdOf(p, L"Concat"), DISPATCH_METHOD, &r, 2, args) == S_OK);
  CHECK(r.vt == VT_BSTR && wcscmp(r.bstrVal, L"foobar") == 0);
  ::VariantClear(&r);
  ::SysFreeString(args[0].bstrVal);
  ::SysFreeString(args[1].bstrVal);

  args[0].vt = VT_R8;
  args[0].dblVal = 1.5;
  args[1].vt = VT_I2;
  args[1].iVal = 3;
  CHECK(Call(p, IdOf(p, L"Scale"), DISPATCH_METHOD, &r, 2, args) == S_OK);
  CHECK(r.vt == VT_R8 && r.dblVal == 4.5);

  args[0].vt = VT_BOOL;
  args[0].boolVal = VARIANT_FALSE;
  CHECK(Call(p, IdOf(p, L"Not"), DISPATCH_METHOD, &r, 1, args) == S_OK);
  CHECK(r.vt == VT_BOOL && r.boolVal == VARIANT_TRUE);

  CY cy;
  cy.int64 = 12345;  // 1.2345
  args[0].vt = VT_CY;
  args[0].cyVal = cy;
  CHECK(Call(p, IdOf(p, L"Money"), DISPATCH_METHOD, &r, 1, args) == S_OK);
  CHECK(r.vt == VT_CY && r.cyVal.int64 == 24690);

  // By reference, written back.
  long a = 1, b = 2;
  args[0].vt = VT_BYREF | VT_I4;
  args[0].plVal = &a;
  args[1].vt = VT_BYREF | VT_I4;
  args[1].plVal = &b;
  CHECK(Call(p, IdOf(p, L"Swap"), DISPATCH_METHOD, NULL, 2, args) == S_OK);
  CHECK(a == 2 && b == 1);
  // A by-reference VARIANT holding a short.
  VARIANT vShort;
  vShort.vt = VT_I2;
  vShort.iVal = 7;
  args[0].vt = VT_BYREF | VT_VARIANT;
  args[0].pvarVal = &vShort;
  args[1].vt = VT_BYREF | VT_I4;
  args[1].plVal = &b;
  CHECK(Call(p, IdOf(p, L"Swap"), DISPATCH_METHOD, NULL, 2, args) == S_OK);
  CHECK(vShort.vt == VT_I2 && vShort.iVal == 1 && b == 7);
  BSTR bstr = ::SysAllocString(L"hey");
  args[0].vt = VT_BYREF | VT_BSTR;
  args[0].pbstrVal = &bstr;
  CHECK(Call(p, IdOf(p, L"Bang"), DISPATCH_METHOD, NULL, 1, args) == S_OK);
  CHECK(wcscmp(bstr, L"hey!") == 0);
  ::SysFreeString(bstr);
  VARIANT_BOOL vb = VARIANT_TRUE;
  args[0].vt = VT_BYREF | VT_BOOL;
  args[0].pboolVal = &vb;
  CHECK(Call(p, IdOf(p, L"Flip"), DISPATCH_METHOD, NULL, 1, args) == S_OK);
  CHECK(vb == VARIANT_FALSE);
  // A by-value argument to a by-reference parameter is not written back.
  args[0] = I4(5);
  args[1] = I4(6);
  CHECK(Call(p, IdOf(p, L"Swap"), DISPATCH_METHOD, NULL, 2, args) == S_OK);
  CHECK(args[0].lVal == 5 && args[1].lVal == 6);

  // A VARIANT parameter and result.
  args[0].vt = VT_BSTR;
  args[0].bstrVal = ::SysAllocString(L"echo");
  CHECK(Call(p, IdOf(p, L"Echo"), DISPATCH_METHOD, &r, 1, args) == S_OK);
  CHECK(r.vt == VT_BSTR && wcscmp(r.bstrVal, L"echo") == 0);
  ::VariantClear(&r);
  ::SysFreeString(args[0].bstrVal);

  // An exception becomes DISP_E_EXCEPTION with its EXCEPINFO.
  EXCEPINFO info;
  memset(&info, 0, sizeof(info));
  CHECK(Call(p, IdOf(p, L"Fail"), DISPATCH_METHOD, NULL, 0, NULL, NULL, &info) ==
        DISP_E_EXCEPTION);
  CHECK(info.wCode == 1234);
  CHECK(info.bstrDescription != NULL && wcscmp(info.bstrDescription, L"It failed") == 0);
  ::SysFreeString(info.bstrSource);
  ::SysFreeString(info.bstrDescription);
  ::SysFreeString(info.bstrHelpFile);

  // An interface result holds a reference.
  long nRef = pCalc->m_dwRef;
  CHECK(Call(p, IdOf(p, L"Self"), DISPATCH_METHOD, &r) == S_OK);
  CHECK(r.vt == VT_DISPATCH && r.pdispVal == p);
  CHECK(pCalc->m_dwRef == nRef + 1);
  ::VariantClear(&r);
  CHECK(pCalc->m_dwRef == nRef);

  // A const member function with an explicit ID; method calls may be
  // made as property gets.
  CHECK(Call(p, 42, DISPATCH_PROPERTYGET, &r) == S_OK);
  CHECK(r.vt == VT_I4 && r.lVal == 42);

  // Member variable properties.
  args[0] = I4(17);
  CHECK(Call(p, IdOf(p, L"Count"), DISPATCH_PROPERTYPUT, NULL, 1, args) == S_OK);
  CHECK(pCalc->m_nCount == 17);
  pCalc->m_nCount = 18;
  CHECK(Call(p, IdOf(p, L"Count"), DISPATCH_PROPERTYGET, &r) == S_OK);
  CHECK(r.vt == VT_I4 && r.lVal == 18);
  args[0].vt = VT_BSTR;
  args[0].bstrVal = ::SysAllocString(L"Sums");
  CHECK(Call(p, IdOf(p, L"Title"), DISPATCH_PROPERTYPUT, NULL, 1, args) == S_OK);
  ::SysFreeString(args[0].bstrVal);
  CHECK(pCalc->m_strTitle == _T("Sums"));
  CHECK(Call(p, IdOf(p, L"Title"), DISPATCH_PROPERTYGET, &r) == S_OK);
  CHECK(r.vt == VT_BSTR && wcscmp(r.bstrVal, L"Sums") == 0);
  ::VariantClear(&r);
  args[0] = I4(1);  // converted to VT_BOOL
  CHECK(Call(p, IdOf(p, L"Enabled"), DISPATCH_PROPERTYPUT, NULL, 1, args) == S_OK);
  CHECK(pCalc->m_bEnabled == TRUE && pCalc->m_nEnabledChanged == 1);
  CHECK(Call(p, IdOf(p, L"Enabled"), DISPATCH_PROPERTYGET, &r) == S_OK);
  CHECK(r.vt == VT_BOOL && r.boolVal == VARIANT_TRUE);
  args[0].vt = VT_R4;
  args[0].fltVal = 2.5f;
  CHECK(Call(p, IdOf(p, L"Tag"), DISPATCH_PROPERTYPUT, NULL, 1, args) == S_OK);
  CHECK(pCalc->m_varTag.vt == VT_R4 && pCalc->m_varTag.fltVal == 2.5f);
  CHECK(Call(p, IdOf(p, L"Tag"), DISPATCH_PROPERTYGET, &r) == S_OK);
  CHECK(r.vt == VT_R4 && r.fltVal == 2.5f);

  // Get and set functions; the default value is Total.
  args[0].vt = VT_R8;
  args[0].dblVal = 9.25;
  CHECK(Call(p, IdOf(p, L"Total"), DISPATCH_PROPERTYPUT, NULL, 1, args) == S_OK);
  CHECK(pCalc->m_dTotal == 9.25);
  CHECK(Call(p, DISPID_VALUE, DISPATCH_PROPERTYGET, &r) == S_OK);
  CHECK(r.vt == VT_R8 && r.dblVal == 9.25);
  // A read-only property.
  CHECK(Call(p, IdOf(p, L"Version"), DISPATCH_PROPERTYGET, &r) == S_OK);
  CHECK(r.vt == VT_BSTR && wcscmp(r.bstrVal, L"1.0") == 0);
  ::VariantClear(&r);
  args[0].vt = VT_BSTR;
  args[0].bstrVal = ::SysAllocString(L"2.0");
  memset(&info, 0, sizeof(info));
  CHECK(Call(p, IdOf(p, L"Version"), DISPATCH_PROPERTYPUT, NULL, 1, args, NULL, &info) ==
        DISP_E_EXCEPTION);
  CHECK(info.scode == CTL_E_SETNOTSUPPORTED);
  ::SysFreeString(args[0].bstrVal);
  ::SysFreeString(info.bstrSource);
  ::SysFreeString(info.bstrDescription);

  // A property with a parameter.
  args[0].vt = VT_I2;
  args[0].iVal = 2;
  args[1].vt = VT_R8;
  args[1].dblVal = 0.5;
  CHECK(Call(p, IdOf(p, L"Item"), DISPATCH_PROPERTYPUT, NULL, 2, args) == S_OK);
  CHECK(pCalc->m_dItems[2] == 0.5);
  CHECK(Call(p, IdOf(p, L"Item"), DISPATCH_PROPERTYGET, &r, 1, args) == S_OK);
  CHECK(r.vt == VT_R8 && r.dblVal == 0.5);
  args[0].iVal = 9;
  CHECK(Call(p, IdOf(p, L"Item"), DISPATCH_PROPERTYGET, &r, 1, args) == DISP_E_EXCEPTION);

  // The same object through COleDispatchDriver.
  {
    COleDispatchDriver drv(p, FALSE);
    long l = 0;
    static const BYTE parmsAdd[] = VTS_I4 VTS_I4;
    drv.InvokeHelper(IdOf(p, L"Add"), DISPATCH_METHOD, VT_I4, &l, parmsAdd, 20L, 22L);
    CHECK(l == 42);
    CString str;
    static const BYTE parmsConcat[] = VTS_BSTR VTS_BSTR;
    drv.InvokeHelper(IdOf(p, L"Concat"), DISPATCH_METHOD, VT_BSTR, &str, parmsConcat, _T("a"),
                     _T("b"));
    CHECK(str == _T("ab"));
    drv.SetProperty(IdOf(p, L"Count"), VT_I4, 99L);
    CHECK(pCalc->m_nCount == 99);
    double d = 0;
    drv.GetProperty(DISPID_VALUE, VT_R8, &d);
    CHECK(d == 9.25);
    BOOL bCaught = FALSE;
    try {
      drv.InvokeHelper(IdOf(p, L"Fail"), DISPATCH_METHOD, VT_EMPTY, NULL, NULL);
    } catch (COleDispatchException* e) {
      bCaught = e->m_wCode == 1234 && e->m_strDescription == _T("It failed");
      e->Delete();
    }
    CHECK(bCaught);
  }

  // The last release deletes the object.
  int nAlive = CCalc::s_nAlive;
  p->Release();
  CHECK(CCalc::s_nAlive == nAlive - 1);

  // In a derived class the base's entries are one level up.
  CSciCalc* pSci = new CSciCalc;
  p = pSci->GetIDispatch(FALSE);
  CHECK(IdOf(p, L"Square") == 1);
  CHECK(IdOf(p, L"Add") == MAKELONG(7, 1));
  args[0].vt = VT_R8;
  args[0].dblVal = 3;
  CHECK(Call(p, 1, DISPATCH_METHOD, &r, 1, args) == S_OK);
  CHECK(r.vt == VT_R8 && r.dblVal == 9);
  args[0] = I4(1);
  args[1] = I4(1);
  CHECK(Call(p, MAKELONG(7, 1), DISPATCH_METHOD, &r, 2, args) == S_OK);
  CHECK(r.vt == VT_I4 && r.lVal == 2);
  CHECK(Call(p, 42, DISPATCH_METHOD, &r) == S_OK && r.lVal == 42);
  p->Release();

  // Without EnableAutomation there is no IDispatch.
  CCmdTarget plain;
  CHECK(plain.GetIDispatch(FALSE) == NULL);
  LPVOID pv = (LPVOID)1;
  CHECK(plain.InternalQueryInterface(&IID_IDispatch, &pv) == (DWORD)E_NOINTERFACE && pv == NULL);
}

// ---------------------------------------------------------------------
// Interface maps

class CPersistent : public CCmdTarget {
public:
  CPersistent() { EnableAutomation(); }
  BEGIN_INTERFACE_PART(Persist, IPersist)
    STDMETHOD(GetClassID)(CLSID* pClassID);
  END_INTERFACE_PART(Persist)
  DECLARE_INTERFACE_MAP()
};

BEGIN_INTERFACE_MAP(CPersistent, CCmdTarget)
  INTERFACE_PART(CPersistent, IID_IPersist, Persist)
END_INTERFACE_MAP()

STDMETHODIMP_(ULONG) CPersistent::XPersist::AddRef() {
  METHOD_PROLOGUE(CPersistent, Persist)
  return pThis->ExternalAddRef();
}
STDMETHODIMP_(ULONG) CPersistent::XPersist::Release() {
  METHOD_PROLOGUE(CPersistent, Persist)
  return pThis->ExternalRelease();
}
STDMETHODIMP CPersistent::XPersist::QueryInterface(REFIID iid, LPVOID* ppv) {
  METHOD_PROLOGUE(CPersistent, Persist)
  return (HRESULT)pThis->ExternalQueryInterface(&iid, ppv);
}
STDMETHODIMP CPersistent::XPersist::GetClassID(CLSID* pClassID) {
  *pClassID = CLSID_Calc;
  return S_OK;
}

static void TestInterfaceMap() {
  CPersistent* pObj = new CPersistent;
  LPDISPATCH pDisp = pObj->GetIDispatch(FALSE);
  IPersist* pPersist = NULL;
  CHECK(pDisp->QueryInterface(IID_IPersist, (void**)&pPersist) == S_OK);
  CHECK(pPersist == (IPersist*)&pObj->m_xPersist);
  CLSID clsid;
  CHECK(pPersist->GetClassID(&clsid) == S_OK && ::IsEqualCLSID(clsid, CLSID_Calc));
  // IUnknown is the same from every interface: the map's first.
  IUnknown* pUnk1 = NULL;
  IUnknown* pUnk2 = NULL;
  CHECK(pDisp->QueryInterface(IID_IUnknown, (void**)&pUnk1) == S_OK);
  CHECK(pPersist->QueryInterface(IID_IUnknown, (void**)&pUnk2) == S_OK);
  CHECK(pUnk1 == pUnk2 && pUnk1 == (IUnknown*)pPersist);
  IDispatch* pDisp2 = NULL;
  CHECK(pPersist->QueryInterface(IID_IDispatch, (void**)&pDisp2) == S_OK && pDisp2 == pDisp);
  IStream* pStream = (IStream*)1;
  CHECK(pPersist->QueryInterface(IID_IStream, (void**)&pStream) == E_NOINTERFACE &&
        pStream == NULL);
  CHECK(pObj->m_dwRef == 5);
  pUnk1->Release();
  pUnk2->Release();
  pDisp2->Release();
  pPersist->Release();
  CHECK(pObj->m_dwRef == 1);
  pObj->ExternalRelease();
}

// ---------------------------------------------------------------------
// Aggregation: an outer object written by hand holds a CCalc.

class COuter : public IUnknown {
public:
  LONG m_nRef = 1;
  IUnknown* m_pInner = NULL;
  STDMETHOD(QueryInterface)(REFIID iid, void** ppv) {
    if (::IsEqualIID(iid, IID_IUnknown)) {
      *ppv = this;
      AddRef();
      return S_OK;
    }
    return m_pInner->QueryInterface(iid, ppv);
  }
  STDMETHOD_(ULONG, AddRef)() { return (ULONG)++m_nRef; }
  STDMETHOD_(ULONG, Release)() { return (ULONG)--m_nRef; }
};

static void TestFactoryInProcess() {
  CHECK(CCalc::factory.Register());
  CHECK(CCalc::factory.IsRegistered());
  int nAlive = CCalc::s_nAlive;
  IDispatch* p = NULL;
  HRESULT hr = ::CoCreateInstance(CLSID_Calc, NULL, CLSCTX_LOCAL_SERVER, IID_IDispatch, (void**)&p);
  CHECK(hr == S_OK && p != NULL);
  if (p != NULL) {
    CHECK(CCalc::s_nAlive == nAlive + 1);
    CHECK(!AfxOleCanExitApp());
    CCmdTarget* pTarget = CCmdTarget::FromIDispatch(p);
    CHECK(pTarget != NULL && pTarget->IsKindOf(RUNTIME_CLASS(CCalc)));
    if (pTarget != NULL) CHECK(pTarget->m_dwRef == 1);
    VARIANT r;
    CHECK(Call(p, 42, DISPATCH_METHOD, &r) == S_OK && r.lVal == 42);
    p->Release();
    CHECK(CCalc::s_nAlive == nAlive);
  }

  // Through IClassFactory: aggregation.
  IClassFactory* pFactory = NULL;
  CHECK(::CoGetClassObject(CLSID_Calc, CLSCTX_LOCAL_SERVER, NULL, IID_IClassFactory,
                           (void**)&pFactory) == S_OK);
  if (pFactory != NULL) {
    COuter outer;
    IDispatch* pDisp = NULL;
    // An aggregated object gives its IUnknown only.
    CHECK(pFactory->CreateInstance(&outer, IID_IDispatch, (void**)&pDisp) ==
          CLASS_E_NOAGGREGATION);
    CHECK(pFactory->CreateInstance(&outer, IID_IUnknown, (void**)&outer.m_pInner) == S_OK);
    CHECK(outer.m_pInner != NULL);
    if (outer.m_pInner != NULL) {
      CHECK(outer.QueryInterface(IID_IDispatch, (void**)&pDisp) == S_OK);
      // The inner object's interfaces count references on the outer one.
      CHECK(outer.m_nRef == 2);
      IUnknown* pUnk = NULL;
      CHECK(pDisp->QueryInterface(IID_IUnknown, (void**)&pUnk) == S_OK && pUnk == &outer);
      pUnk->Release();
      VARIANT args[2] = {I4(4), I4(5)};
      VARIANT r;
      CHECK(Call(pDisp, IdOf(pDisp, L"Add"), DISPATCH_METHOD, &r, 2, args) == S_OK &&
            r.lVal == 9);
      pDisp->Release();
      CHECK(outer.m_nRef == 1);
      CHECK(CCalc::s_nAlive == nAlive + 1);
      outer.m_pInner->Release();
      CHECK(CCalc::s_nAlive == nAlive);
    }
    IClassFactory2* pFactory2 = NULL;
    CHECK(pFactory->QueryInterface(IID_IClassFactory2, (void**)&pFactory2) == S_OK);
    if (pFactory2 != NULL) {
      LICINFO li;
      CHECK(pFactory2->GetLicInfo(&li) == S_OK && li.fLicVerified && !li.fRuntimeKeyAvail);
      pFactory2->Release();
    }
    pFactory->Release();
  }

  CCalc::factory.Revoke();
  CHECK(!CCalc::factory.IsRegistered());
  CHECK(::CoGetClassObject(CLSID_Calc, CLSCTX_LOCAL_SERVER, NULL, IID_IClassFactory,
                           (void**)&pFactory) != S_OK);
  CHECK(AfxOleCanExitApp());
}

static CString RegValue(LPCTSTR lpszKey) {
  TCHAR sz[MAX_PATH * 2];
  LONG cb = sizeof(sz);
  if (::RegQueryValue(HKEY_CLASSES_ROOT, lpszKey, sz, &cb) != ERROR_SUCCESS) return _T("<none>");
  return sz;
}

static const TCHAR szClsidKey[] = _T("CLSID\\{6C1D4E2A-8B7F-4C3D-9A51-2E6F0B9D7C11}");

static void TestRegistry() {
  CHECK(COleObjectFactory::UpdateRegistryAll(TRUE));
  CHECK(RegValue(_T("MfcTest.Calc\\CLSID")) == _T("{6C1D4E2A-8B7F-4C3D-9A51-2E6F0B9D7C11}"));
  CHECK(RegValue(CString(szClsidKey) + _T("\\ProgID")) == _T("MfcTest.Calc"));
  CString strServer = RegValue(CString(szClsidKey) + _T("\\LocalServer32"));
  printf("LocalServer32: %s\n", (LPCSTR)CStringA(strServer));
  CHECK(strServer.Right(12) == _T(" /Automation"));
  CLSID clsid;
  CHECK(::CLSIDFromProgID(L"MfcTest.Calc", &clsid) == S_OK && ::IsEqualCLSID(clsid, CLSID_Calc));
}

// COM starts this program again to make the object, and it closes when
// the last reference goes.
static void TestLocalServer() {
  IDispatch* p = NULL;
  HRESULT hr = ::CoCreateInstance(CLSID_Calc, NULL, CLSCTX_LOCAL_SERVER, IID_IDispatch, (void**)&p);
  printf("local server: %08lx\n", (unsigned long)hr);
  CHECK(hr == S_OK && p != NULL);
  if (p == NULL) return;
  VARIANT r;
  CHECK(Call(p, IdOf(p, L"ProcessId"), DISPATCH_METHOD, &r) == S_OK && r.vt == VT_I4);
  DWORD dwPid = (DWORD)r.lVal;
  CHECK(dwPid != ::GetCurrentProcessId());
  VARIANT args[2];
  args[0].vt = VT_BSTR;
  args[0].bstrVal = ::SysAllocString(L"cross");
  args[1].vt = VT_BSTR;
  args[1].bstrVal = ::SysAllocString(L"-process");
  CHECK(Call(p, IdOf(p, L"Concat"), DISPATCH_METHOD, &r, 2, args) == S_OK);
  CHECK(r.vt == VT_BSTR && wcscmp(r.bstrVal, L"cross-process") == 0);
  ::VariantClear(&r);
  ::SysFreeString(args[0].bstrVal);
  ::SysFreeString(args[1].bstrVal);
  long a = 3, b = 4;
  args[0].vt = VT_BYREF | VT_I4;
  args[0].plVal = &a;
  args[1].vt = VT_BYREF | VT_I4;
  args[1].plVal = &b;
  CHECK(Call(p, IdOf(p, L"Swap"), DISPATCH_METHOD, NULL, 2, args) == S_OK);
  CHECK(a == 4 && b == 3);
  EXCEPINFO info;
  memset(&info, 0, sizeof(info));
  CHECK(Call(p, IdOf(p, L"Fail"), DISPATCH_METHOD, NULL, 0, NULL, NULL, &info) ==
        DISP_E_EXCEPTION);
  CHECK(info.wCode == 1234);
  ::SysFreeString(info.bstrSource);
  ::SysFreeString(info.bstrDescription);
  ::SysFreeString(info.bstrHelpFile);

  HANDLE hProcess = ::OpenProcess(SYNCHRONIZE, FALSE, dwPid);
  p->Release();
  if (hProcess != NULL) {
    CHECK(::WaitForSingleObject(hProcess, 15000) == WAIT_OBJECT_0);
    ::CloseHandle(hProcess);
  }
}

static int RunServer() {
  // As a wizard's InitInstance does for /Automation.
  ::OleInitialize(NULL);
  CHECK(!AfxOleGetUserCtrl());
  COleObjectFactory::RegisterAll();
  DWORD dwStart = ::GetTickCount();
  MSG msg;
  BOOL bQuit = FALSE;
  while (!bQuit && ::GetTickCount() - dwStart < 30000) {
    while (::PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
      if (msg.message == WM_QUIT) bQuit = TRUE;
      ::DispatchMessage(&msg);
    }
    if (!bQuit) ::MsgWaitForMultipleObjects(0, NULL, FALSE, 50, QS_ALLINPUT);
  }
  COleObjectFactory::RevokeAll();
  ::OleUninitialize();
  return bQuit ? 0 : 3;
}

int main(int argc, char** argv) {
  for (int i = 1; i < argc; i++) {
    if (lstrcmpiA(argv[i], "/Automation") == 0 || lstrcmpiA(argv[i], "-Embedding") == 0) {
      // Parse the way CWinApp does, so that the user control flag is clear.
      CCommandLineInfo info;
      info.ParseParam(_T("Automation"), TRUE, TRUE);
      return RunServer();
    }
  }
  if (FAILED(::OleInitialize(NULL))) {
    printf("FAIL: OleInitialize\n");
    return 1;
  }
  TestDispatch();
  TestInterfaceMap();
  TestFactoryInProcess();
  TestRegistry();
  TestLocalServer();
  CHECK(COleObjectFactory::UpdateRegistryAll(FALSE));
  CHECK(RegValue(CString(szClsidKey) + _T("\\ProgID")) == _T("<none>"));
  ::OleUninitialize();
  if (g_failures == 0) printf("PASS: all automation tests\n");
  else printf("%d failure(s)\n", g_failures);
  return g_failures ? 1 : 0;
}
