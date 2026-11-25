// win16auto.cpp - tests MFC automation on 16-bit Windows: a dispatch map
// with a property and a method, the IDispatch of a CCmdTarget, and a
// connection map with a sink that is advised and unadvised. Wine's
// 16-bit OLE2DISP has no VariantClear and no VariantChangeType, so the
// test does not call IDispatch::Invoke. The checks use MFC code and the
// BSTR functions of OLE2DISP only.
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

// The outgoing interface of the source. {7D2F1B63-4C8E-4039-AB72-3E6D9F5C8021}
static const IID IID_IEvents = {
    0x7d2f1b63L, 0x4c8e, 0x4039, {0xab, 0x72, 0x3e, 0x6d, 0x9f, 0x5c, 0x80, 0x21}};

// The source: one property, one method and one connection point.
class CSource : public CCmdTarget {
public:
  long m_lValue;

  /// Makes a source with automation and connection points on.
  CSource() : m_lValue(7) {
    EnableAutomation();
    EnableConnections();
  }

  /// Keeps the object alive: it is on the stack.
  virtual void OnFinalRelease() {}

  /// Gives the sum of the two numbers.
  long Add(long a, long b) { return a + b; }

  /// Gives the number of sinks on the events point.
  int CountSinks() {
    int n = 0;
    POSITION pos = m_xEventsCP.GetStartPosition();
    while (pos != NULL)
      if (m_xEventsCP.GetNextConnection(pos) != NULL) n++;
    return n;
  }

  BEGIN_CONNECTION_PART(CSource, EventsCP)
    CONNECTION_IID(IID_IEvents)
  END_CONNECTION_PART(EventsCP)

  DECLARE_DISPATCH_MAP()
  DECLARE_CONNECTION_MAP()
  DECLARE_INTERFACE_MAP()
};

BEGIN_DISPATCH_MAP(CSource, CCmdTarget)
  DISP_PROPERTY(CSource, "Value", m_lValue, VT_I4)
  DISP_FUNCTION(CSource, "Add", Add, VT_I4, VTS_I4 VTS_I4)
END_DISPATCH_MAP()

BEGIN_CONNECTION_MAP(CSource, CCmdTarget)
  CONNECTION_PART(CSource, IID_IEvents, EventsCP)
END_CONNECTION_MAP()

BEGIN_INTERFACE_MAP(CSource, CCmdTarget)
  INTERFACE_PART(CSource, IID_IConnectionPointContainer, ConnPtContainer)
END_INTERFACE_MAP()

// The sink: an IDispatch that also answers to IID_IEvents.
class CSink : public CCmdTarget {
public:
  /// Makes a sink with automation on.
  CSink() { EnableAutomation(); }

  /// Keeps the object alive: it is on the stack.
  virtual void OnFinalRelease() {}

  /// Is the event handler. The test does not call it.
  void Fired(long n) { (void)n; }

  DECLARE_DISPATCH_MAP()
  DECLARE_INTERFACE_MAP()
};

BEGIN_DISPATCH_MAP(CSink, CCmdTarget)
  DISP_FUNCTION_ID(CSink, "Fired", 1, Fired, VT_EMPTY, VTS_I4)
END_DISPATCH_MAP()

BEGIN_INTERFACE_MAP(CSink, CCmdTarget)
  INTERFACE_PART(CSink, IID_IEvents, Dispatch)
END_INTERFACE_MAP()

/// Checks the IDispatch of the source: the names, the type information
/// count and QueryInterface.
static void TestDispatch(CSource& source) {
  LPDISPATCH pDisp = source.GetIDispatch(FALSE);
  CHECK(pDisp != NULL);
  if (pDisp == NULL) return;

  // The names, in any case, give the IDs of the dispatch map.
  char szValue[] = "value";
  char szAdd[] = "ADD";
  char szNone[] = "Missing";
  LPOLESTR names[1];
  DISPID id = 0;
  names[0] = szValue;
  CHECK(pDisp->GetIDsOfNames(IID_NULL, names, 1, 0, &id) == S_OK);
  DISPID idValue = id;
  names[0] = szAdd;
  CHECK(pDisp->GetIDsOfNames(IID_NULL, names, 1, 0, &id) == S_OK);
  CHECK(id != idValue);
  names[0] = szNone;
  CHECK(pDisp->GetIDsOfNames(IID_NULL, names, 1, 0, &id) == DISP_E_UNKNOWNNAME);

  // No type library: the count is 0.
  UINT nCount = 1;
  CHECK(pDisp->GetTypeInfoCount(&nCount) == S_OK && nCount == 0);

  // The controlling unknown gives the same IDispatch.
  LPVOID pv = NULL;
  CHECK(pDisp->QueryInterface(IID_IDispatch, &pv) == S_OK && pv == pDisp);
  if (pv != NULL) ((LPUNKNOWN)pv)->Release();
  pv = NULL;
  CHECK(pDisp->QueryInterface(IID_IClassFactory, &pv) == E_NOINTERFACE && pv == NULL);
}

/// Checks the connection point of the source: find, advise, enumerate
/// and unadvise.
static void TestConnections(CSource& source, CSink& sink) {
  LPUNKNOWN pUnk = source.GetControllingUnknown();
  LPCONNECTIONPOINTCONTAINER pCPC = NULL;
  CHECK(pUnk->QueryInterface(IID_IConnectionPointContainer, (LPVOID FAR*)&pCPC) == S_OK);
  if (pCPC == NULL) return;

  LPCONNECTIONPOINT pCP = NULL;
  CHECK(pCPC->FindConnectionPoint(IID_IDispatch, &pCP) == CONNECT_E_NOCONNECTION);
  CHECK(pCP == NULL);
  CHECK(pCPC->FindConnectionPoint(IID_IEvents, &pCP) == S_OK && pCP != NULL);
  if (pCP == NULL) {
    pCPC->Release();
    return;
  }

  IID iid;
  CHECK(pCP->GetConnectionInterface(&iid) == S_OK && iid == IID_IEvents);

  DWORD dwCookie = 0;
  CHECK(pCP->Advise(sink.GetIDispatch(FALSE), &dwCookie) == S_OK && dwCookie != 0);
  CHECK(source.CountSinks() == 1);

  LPENUMCONNECTIONS pEnum = NULL;
  CHECK(pCP->EnumConnections(&pEnum) == S_OK && pEnum != NULL);
  if (pEnum != NULL) {
    CONNECTDATA cd;
    ULONG nFetched = 0;
    CHECK(pEnum->Next(1, &cd, &nFetched) == S_OK && nFetched == 1);
    CHECK(cd.dwCookie == dwCookie && cd.pUnk != NULL);
    if (cd.pUnk != NULL) cd.pUnk->Release();
    CHECK(pEnum->Next(1, &cd, &nFetched) == S_FALSE && nFetched == 0);
    pEnum->Release();
  }

  CHECK(pCP->Unadvise(dwCookie) == S_OK);
  CHECK(source.CountSinks() == 0);
  CHECK(pCP->Unadvise(dwCookie) == CONNECT_E_NOCONNECTION);
  pCP->Release();
  pCPC->Release();
}

/// Runs the tests and prints ALL PASSED when every check holds.
int main() {
  CSource source;
  CSink sink;
  TestDispatch(source);
  TestConnections(source, sink);
  if (g_failures == 0) printf("ALL PASSED\n");
  return g_failures;
}
