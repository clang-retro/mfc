// conntest.cpp - console tests for connection points and type libraries:
// a connection map driven through IConnectionPointContainer and
// IConnectionPoint, AfxConnectionAdvise with and without a reference,
// events fired to dispatch sinks, and the type information of a class
// with IMPLEMENT_OLETYPELIB from a type library this test writes and
// registers.
#include <afx.h>
#include <afxwin.h>
#include <afxdisp.h>
#include <stdio.h>

static int g_failures = 0;
#define CHECK(cond)                                          \
  do {                                                       \
    if (!(cond)) {                                           \
      printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
      g_failures++;                                          \
    }                                                        \
  } while (0)

// {6C1E0A52-3B7D-4F28-9A61-2D5C8E4B7F01}
static const IID IID_IEvents = {
    0x6c1e0a52, 0x3b7d, 0x4f28, {0x9a, 0x61, 0x2d, 0x5c, 0x8e, 0x4b, 0x7f, 0x01}};
// {6C1E0A52-3B7D-4F28-9A61-2D5C8E4B7F02}
static const IID IID_INotify = {
    0x6c1e0a52, 0x3b7d, 0x4f28, {0x9a, 0x61, 0x2d, 0x5c, 0x8e, 0x4b, 0x7f, 0x02}};
// {6C1E0A52-3B7D-4F28-9A61-2D5C8E4B7F03}
static const IID IID_IOther = {
    0x6c1e0a52, 0x3b7d, 0x4f28, {0x9a, 0x61, 0x2d, 0x5c, 0x8e, 0x4b, 0x7f, 0x03}};
// The type library and the dispatch interface it describes.
// {6C1E0A52-3B7D-4F28-9A61-2D5C8E4B7F10}
static const GUID TLID_Test = {
    0x6c1e0a52, 0x3b7d, 0x4f28, {0x9a, 0x61, 0x2d, 0x5c, 0x8e, 0x4b, 0x7f, 0x10}};
// {6C1E0A52-3B7D-4F28-9A61-2D5C8E4B7F11}
static const IID IID_DTyped = {
    0x6c1e0a52, 0x3b7d, 0x4f28, {0x9a, 0x61, 0x2d, 0x5c, 0x8e, 0x4b, 0x7f, 0x11}};

// ---------------------------------------------------------------------
// The source: two outgoing interfaces, one of which takes one sink.

class CSource : public CCmdTarget {
public:
  int m_nAdvised;
  int m_nUnadvised;
  static int s_nAlive;

  CSource() : m_nAdvised(0), m_nUnadvised(0) {
    s_nAlive++;
    EnableAutomation();
    EnableConnections();
  }
  virtual ~CSource() { s_nAlive--; }

  // Calls Fired(n) on every sink of the events point.
  int FireEvent(long n) {
    int nCalled = 0;
    POSITION pos = m_xEventsCP.GetStartPosition();
    while (pos != NULL) {
      LPDISPATCH pDisp = (LPDISPATCH)m_xEventsCP.GetNextConnection(pos);
      if (pDisp == NULL) continue;
      COleDispatchDriver driver(pDisp, FALSE);
      static const BYTE params[] = VTS_I4;
      driver.InvokeHelper(1, DISPATCH_METHOD, VT_EMPTY, NULL, params, n);
      nCalled++;
    }
    return nCalled;
  }

  BEGIN_CONNECTION_PART(CSource, EventsCP)
    CONNECTION_IID(IID_IEvents)
    virtual void OnAdvise(BOOL bAdvise) {
      CSource* pSource = (CSource*)m_pContainer;
      if (bAdvise)
        pSource->m_nAdvised++;
      else
        pSource->m_nUnadvised++;
    }
  END_CONNECTION_PART(EventsCP)

  BEGIN_CONNECTION_PART(CSource, NotifyCP)
    CONNECTION_IID(IID_INotify)
    virtual int GetMaxConnections() { return 1; }
  END_CONNECTION_PART(NotifyCP)

  DECLARE_CONNECTION_MAP()
  DECLARE_INTERFACE_MAP()
};
int CSource::s_nAlive = 0;

BEGIN_CONNECTION_MAP(CSource, CCmdTarget)
  CONNECTION_PART(CSource, IID_IEvents, EventsCP)
  CONNECTION_PART(CSource, IID_INotify, NotifyCP)
END_CONNECTION_MAP()

// As MFC programs write it: the container in the interface map.
BEGIN_INTERFACE_MAP(CSource, CCmdTarget)
  INTERFACE_PART(CSource, IID_IConnectionPointContainer, ConnPtContainer)
END_INTERFACE_MAP()

// A source that adds a point of its own through the hook.
class CDerivedSource : public CSource {
public:
  BEGIN_CONNECTION_PART(CDerivedSource, OtherCP)
    CONNECTION_IID(IID_IOther)
  END_CONNECTION_PART(OtherCP)

  CDerivedSource() { EnableConnections(); }
  DECLARE_CONNECTION_MAP()
};

BEGIN_CONNECTION_MAP(CDerivedSource, CSource)
  CONNECTION_PART(CDerivedSource, IID_IOther, OtherCP)
END_CONNECTION_MAP()

// ---------------------------------------------------------------------
// A sink: a dispatch map whose IDispatch also answers for IID_IEvents.

class CSink : public CCmdTarget {
public:
  long m_nLast;
  int m_nCalls;
  CSink() : m_nLast(0), m_nCalls(0) { EnableAutomation(); }
  virtual void OnFinalRelease() {}  // lives on the stack
  void Fired(long n) {
    m_nLast = n;
    m_nCalls++;
  }
  DECLARE_DISPATCH_MAP()
  DECLARE_INTERFACE_MAP()
};

BEGIN_DISPATCH_MAP(CSink, CCmdTarget)
  DISP_FUNCTION_ID(CSink, "Fired", 1, Fired, VT_EMPTY, VTS_I4)
END_DISPATCH_MAP()

BEGIN_INTERFACE_MAP(CSink, CCmdTarget)
  INTERFACE_PART(CSink, IID_IEvents, Dispatch)
  INTERFACE_PART(CSink, IID_INotify, Dispatch)
END_INTERFACE_MAP()

static ULONG RefCount(IUnknown* p) {
  p->AddRef();
  return p->Release();
}

static void TestConnections() {
  CSource* pSource = new CSource;
  LPUNKNOWN pUnk = pSource->GetControllingUnknown();

  LPCONNECTIONPOINTCONTAINER pCPC = NULL;
  CHECK(pUnk->QueryInterface(IID_IConnectionPointContainer, (void**)&pCPC) == S_OK);
  if (pCPC == NULL) return;

  // Finding the points, and one that is not there.
  LPCONNECTIONPOINT pCP = NULL;
  CHECK(pCPC->FindConnectionPoint(IID_IOther, &pCP) == CONNECT_E_NOCONNECTION && pCP == NULL);
  CHECK(pCPC->FindConnectionPoint(IID_IEvents, &pCP) == S_OK && pCP != NULL);
  IID iid;
  CHECK(pCP->GetConnectionInterface(&iid) == S_OK && iid == IID_IEvents);
  LPCONNECTIONPOINTCONTAINER pCPC2 = NULL;
  CHECK(pCP->GetConnectionPointContainer(&pCPC2) == S_OK && pCPC2 == pCPC);
  if (pCPC2 != NULL) pCPC2->Release();
  // A point is its own object, but keeps the source alive.
  LPUNKNOWN pUnkCP = NULL;
  CHECK(pCP->QueryInterface(IID_IUnknown, (void**)&pUnkCP) == S_OK);
  CHECK(pUnkCP == (LPUNKNOWN)pCP);
  if (pUnkCP != NULL) pUnkCP->Release();
  LPVOID pv = NULL;
  CHECK(pCP->QueryInterface(IID_IConnectionPointContainer, &pv) == E_NOINTERFACE);
  CHECK(pSource->GetConnectionHook(IID_IEvents) == NULL);
  CHECK(pSource->FindConnection(IID_IEvents) == &pSource->m_xEventsCP);
  CHECK(pSource->m_xEventsCP.GetContainer() == pCPC);

  // All the points.
  IEnumConnectionPoints* pEnumCP = NULL;
  CHECK(pCPC->EnumConnectionPoints(&pEnumCP) == S_OK);
  if (pEnumCP != NULL) {
    LPCONNECTIONPOINT rgCP[4] = {};
    ULONG n = 0;
    CHECK(pEnumCP->Next(4, rgCP, &n) == S_FALSE && n == 2);
    for (ULONG i = 0; i < n; i++) {
      IID iidCP;
      rgCP[i]->GetConnectionInterface(&iidCP);
      CHECK(i == 0 ? iidCP == IID_IEvents : iidCP == IID_INotify);
      rgCP[i]->Release();
    }
    CHECK(pEnumCP->Reset() == S_OK && pEnumCP->Skip(1) == S_OK);
    IEnumConnectionPoints* pClone = NULL;
    CHECK(pEnumCP->Clone(&pClone) == S_OK);
    if (pClone != NULL) {
      CHECK(pClone->Next(1, rgCP, NULL) == S_OK);
      rgCP[0]->Release();
      CHECK(pClone->Next(1, rgCP, NULL) == S_FALSE);
      pClone->Release();
    }
    pEnumCP->Release();
  }

  // Two sinks: one held by the connection, one not.
  CSink sink1, sink2;
  LPUNKNOWN pSink1 = sink1.GetIDispatch(FALSE);
  LPUNKNOWN pSink2 = sink2.GetIDispatch(FALSE);
  ULONG nRef1 = RefCount(pSink1), nRef2 = RefCount(pSink2);
  DWORD dwCookie1 = 0, dwCookie2 = 0;
  CHECK(pCP->Advise(pSink1, &dwCookie1) == S_OK && dwCookie1 != 0);
  CHECK(RefCount(pSink1) == nRef1 + 1);
  CHECK(AfxConnectionAdvise(pUnk, IID_IEvents, pSink2, FALSE, &dwCookie2));
  CHECK(RefCount(pSink2) == nRef2);
  CHECK(dwCookie2 != 0 && dwCookie2 != dwCookie1);
  CHECK(pSource->m_nAdvised == 2);
  CHECK(pSource->m_xEventsCP.GetConnections()->GetSize() == 2);

  // Firing reaches both.
  CHECK(pSource->FireEvent(42) == 2);
  CHECK(sink1.m_nCalls == 1 && sink1.m_nLast == 42);
  CHECK(sink2.m_nCalls == 1 && sink2.m_nLast == 42);

  // The connections, enumerated.
  IEnumConnections* pEnum = NULL;
  CHECK(pCP->EnumConnections(&pEnum) == S_OK);
  if (pEnum != NULL) {
    CONNECTDATA cd[3];
    ULONG n = 0;
    CHECK(pEnum->Next(3, cd, &n) == S_FALSE && n == 2);
    for (ULONG i = 0; i < n; i++) {
      CHECK(cd[i].dwCookie == (i == 0 ? dwCookie1 : dwCookie2));
      cd[i].pUnk->Release();
    }
    pEnum->Release();
  }

  // A sink without the interface, and the one-sink point.
  CCmdTarget plain;
  plain.EnableAutomation();
  DWORD dw = 0;
  LPUNKNOWN pPlain = plain.GetInterface(&IID_IUnknown);
  CHECK(pCP->Advise(pPlain, &dw) == CONNECT_E_CANNOTCONNECT && dw == 0);
  CHECK(!AfxConnectionAdvise(pUnk, IID_IOther, pSink1, TRUE, &dw));
  LPCONNECTIONPOINT pNotify = NULL;
  CHECK(pCPC->FindConnectionPoint(IID_INotify, &pNotify) == S_OK);
  if (pNotify != NULL) {
    DWORD dwN = 0, dwN2 = 0;
    CHECK(pNotify->Advise(pSink1, &dwN) == S_OK);
    CHECK(pNotify->Advise(pSink2, &dwN2) == CONNECT_E_ADVISELIMIT);
    CHECK(pNotify->Unadvise(dwN) == S_OK);
    CHECK(pNotify->Advise(pSink2, &dwN2) == S_OK);
    CHECK(pNotify->Unadvise(dwN2) == S_OK);
    pNotify->Release();
  }

  // Disconnecting: a wrong cookie, then each sink.
  CHECK(pCP->Unadvise(12345) == CONNECT_E_NOCONNECTION);
  CHECK(pCP->Unadvise(dwCookie1) == S_OK);
  CHECK(RefCount(pSink1) == nRef1);
  CHECK(AfxConnectionUnadvise(pUnk, IID_IEvents, pSink2, FALSE, dwCookie2));
  CHECK(RefCount(pSink2) == nRef2);
  CHECK(pCP->Unadvise(dwCookie1) == CONNECT_E_NOCONNECTION);
  CHECK(pSource->m_nUnadvised == 2);
  CHECK(pSource->FireEvent(7) == 0);
  CHECK(sink1.m_nCalls == 1);

  // The point keeps the source: release everything else first.
  pCPC->Release();
  pUnk->Release();
  CHECK(CSource::s_nAlive == 1);
  pCP->Release();
  CHECK(CSource::s_nAlive == 0);

  // A derived class's points come before its base's, and an object
  // without EnableConnections has no container.
  CDerivedSource* pDerived = new CDerivedSource;
  pCPC = NULL;
  CHECK(pDerived->GetControllingUnknown()->QueryInterface(IID_IConnectionPointContainer,
                                                          (void**)&pCPC) == S_OK);
  if (pCPC != NULL) {
    pCP = NULL;
    CHECK(pCPC->FindConnectionPoint(IID_IOther, &pCP) == S_OK);
    if (pCP != NULL) pCP->Release();
    CHECK(pCPC->FindConnectionPoint(IID_IEvents, &pCP) == S_OK);
    if (pCP != NULL) pCP->Release();
    pEnumCP = NULL;
    CHECK(pCPC->EnumConnectionPoints(&pEnumCP) == S_OK);
    if (pEnumCP != NULL) {
      LPCONNECTIONPOINT rgCP[4] = {};
      ULONG n = 0;
      pEnumCP->Next(4, rgCP, &n);
      CHECK(n == 3);
      for (ULONG i = 0; i < n; i++) rgCP[i]->Release();
      pEnumCP->Release();
    }
    pCPC->Release();
  }
  pDerived->ExternalRelease();
  CHECK(CSource::s_nAlive == 0);
  CHECK(plain.GetInterface(&IID_IConnectionPointContainer) == NULL);
  printf("connection points done\n");
}

// ---------------------------------------------------------------------
// Type libraries

class CTyped : public CCmdTarget {
public:
  CTyped() {
    EnableAutomation();
    EnableTypeLib();
  }
  virtual void OnFinalRelease() {}
  virtual BOOL GetDispatchIID(IID* pIID) {
    *pIID = IID_DTyped;
    return TRUE;
  }
  long Add(long a, long b) { return a + b; }
  DECLARE_DISPATCH_MAP()
  DECLARE_OLETYPELIB(CTyped)
};

BEGIN_DISPATCH_MAP(CTyped, CCmdTarget)
  DISP_FUNCTION_ID(CTyped, "Add", 1, Add, VT_I4, VTS_I4 VTS_I4)
END_DISPATCH_MAP()

IMPLEMENT_OLETYPELIB(CTyped, TLID_Test, 1, 0)

// Writes a type library with one dispinterface, DTyped, with Add(a, b).
static BOOL WriteTypeLib(LPCWSTR lpszFile) {
  ICreateTypeLib2* pctl = NULL;
  if (FAILED(::CreateTypeLib2(SYS_WIN32, lpszFile, &pctl))) return FALSE;
  pctl->SetGuid(TLID_Test);
  pctl->SetVersion(1, 0);
  pctl->SetName((LPOLESTR)L"ConnTestLib");
  pctl->SetLcid(LOCALE_NEUTRAL);
  BOOL bOK = FALSE;
  ICreateTypeInfo* pcti = NULL;
  if (SUCCEEDED(pctl->CreateTypeInfo((LPOLESTR)L"DTyped", TKIND_DISPATCH, &pcti))) {
    pcti->SetGuid(IID_DTyped);
    // A dispinterface derives from IDispatch.
    ITypeLib* pStdLib = NULL;
    ITypeInfo* pDispInfo = NULL;
    HREFTYPE hRef = 0;
    if (SUCCEEDED(::LoadTypeLib(L"stdole2.tlb", &pStdLib)) &&
        SUCCEEDED(pStdLib->GetTypeInfoOfGuid(IID_IDispatch, &pDispInfo)) &&
        SUCCEEDED(pcti->AddRefTypeInfo(pDispInfo, &hRef)))
      pcti->AddImplType(0, hRef);
    if (pDispInfo != NULL) pDispInfo->Release();
    if (pStdLib != NULL) pStdLib->Release();
    ELEMDESC params[2] = {};
    params[0].tdesc.vt = VT_I4;
    params[1].tdesc.vt = VT_I4;
    FUNCDESC fd = {};
    fd.memid = 1;
    fd.funckind = FUNC_DISPATCH;
    fd.invkind = INVOKE_FUNC;
    fd.callconv = CC_STDCALL;
    fd.cParams = 2;
    fd.lprgelemdescParam = params;
    fd.elemdescFunc.tdesc.vt = VT_I4;
    LPOLESTR names[] = {(LPOLESTR)L"Add", (LPOLESTR)L"a", (LPOLESTR)L"b"};
    bOK = SUCCEEDED(pcti->AddFuncDesc(0, &fd)) && SUCCEEDED(pcti->SetFuncAndParamNames(0, names, 3));
    pcti->Release();
  }
  bOK = bOK && SUCCEEDED(pctl->SaveAllChanges());
  pctl->Release();
  return bOK;
}

static void TestTypeLib() {
  // Before registration there is no type information.
  CTyped typed;
  LPDISPATCH pDisp = typed.GetIDispatch(FALSE);
  UINT nCount = 0;
  CHECK(pDisp->GetTypeInfoCount(&nCount) == S_OK && nCount == 1);
  ITypeInfo* pti = NULL;
  AfxOleUnregisterTypeLib(TLID_Test);
  CHECK(FAILED(pDisp->GetTypeInfo(0, 0, &pti)) && pti == NULL);

  WCHAR szDir[MAX_PATH], szFile[MAX_PATH];
  ::GetTempPathW(MAX_PATH, szDir);
  lstrcpyW(szFile, szDir);
  lstrcatW(szFile, L"conntest.tlb");
  CHECK(WriteTypeLib(szFile));
  CString strFile(szFile);
  // The wrong library ID is refused.
  CHECK(!AfxOleRegisterTypeLib(NULL, IID_DTyped, strFile));
  CHECK(AfxOleRegisterTypeLib(NULL, TLID_Test, strFile));

  CHECK(pDisp->GetTypeInfo(1, 0, &pti) == DISP_E_BADINDEX);
  CHECK(pDisp->GetTypeInfo(0, 0, &pti) == S_OK && pti != NULL);
  if (pti != NULL) {
    TYPEATTR* pAttr = NULL;
    CHECK(pti->GetTypeAttr(&pAttr) == S_OK);
    if (pAttr != NULL) {
      CHECK(pAttr->guid == IID_DTyped);
      CHECK(pAttr->typekind == TKIND_DISPATCH);
      pti->ReleaseTypeAttr(pAttr);
    }
    BSTR bstrName = NULL;
    CHECK(pti->GetDocumentation(1, &bstrName, NULL, NULL, NULL) == S_OK);
    CHECK(bstrName != NULL && lstrcmpW(bstrName, L"Add") == 0);
    ::SysFreeString(bstrName);
    // The cache gives the same object again.
    ITypeInfo* pti2 = NULL;
    CHECK(pDisp->GetTypeInfo(0, 0, &pti2) == S_OK && pti2 == pti);
    if (pti2 != NULL) pti2->Release();
    // And a call through the type information reaches the dispatch map.
    VARIANTARG args[2];
    args[0].vt = VT_I4;
    args[0].lVal = 5;  // b
    args[1].vt = VT_I4;
    args[1].lVal = 37;  // a
    DISPPARAMS dp = {args, NULL, 2, 0};
    VARIANT result;
    ::VariantInit(&result);
    CHECK(pti->Invoke(pDisp, 1, DISPATCH_METHOD, &dp, &result, NULL, NULL) == S_OK);
    CHECK(result.vt == VT_I4 && result.lVal == 42);
    pti->Release();
  }
  CTypeLibCache* pCache = AfxGetTypeLibCache(&TLID_Test);
  CHECK(pCache != NULL && pCache == AfxGetTypeLibCache(&TLID_Test));
  CHECK(pCache != NULL && *pCache->m_pTypeLibID == TLID_Test);

  CHECK(AfxOleUnregisterTypeLib(TLID_Test));
  ITypeLib* ptl = NULL;
  CHECK(FAILED(::LoadRegTypeLib(TLID_Test, 1, 0, 0, &ptl)));
  if (ptl != NULL) ptl->Release();
  ::DeleteFileW(szFile);
  printf("type libraries done\n");
}

int main() {
  if (FAILED(::OleInitialize(NULL))) {
    printf("FAIL: OleInitialize\n");
    return 1;
  }
  TestConnections();
  TestTypeLib();
  // AfxOleTerm empties the type library caches before OLE goes.
  AfxOleTerm(FALSE);
  ::OleUninitialize();
  if (g_failures == 0) printf("PASS: all connection and type library tests\n");
  else printf("%d failure(s)\n", g_failures);
  return g_failures ? 1 : 0;
}
