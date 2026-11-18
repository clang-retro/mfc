// atlutiltest.cpp - ATL's utility headers, in a program without MFC:
//   - CString from atlstr.h (formatting, resources, conversions, errors);
//   - the collections: CSimpleArray and CSimpleMap, CAtlArray, CAtlList,
//     CAtlMap, CRBMap and CRBMultiMap, and the arrays and lists of
//     CAutoPtr and interface pointers;
//   - CHeapPtr, CComHeapPtr, CTempBuffer, CAutoPtr, CAutoVectorPtr,
//     CHandle, AtlAdd and AtlMultiply;
//   - AtlThrow and CAtlException;
//   - CComSafeArray, one- and two-dimensional;
//   - CComDispatchDriver over an IDispatch of the test's own;
//   - CAtlFile, CAtlTemporaryFile and CAtlFileMapping;
//   - CPath from atlpath.h;
//   - atlsecurity.h: CSid, CDacl, CSecurityDesc, CSecurityAttributes,
//     AtlGetDacl/AtlSetDacl and CAccessToken;
//   - CAtlServiceModuleT: /Service, the service started by the service
//     control manager and stopped, and /UnregServer. The service is this
//     program again, started with -AsService.
// Writes atlutiltest.log in MFC_TEST_OUT; exits with 1 on a failure.
#include <atlbase.h>
#include <atlstr.h>
#include <atlcoll.h>
#include <atlsafe.h>
#include <atlfile.h>
#include <atlpath.h>
#include <atlsecurity.h>
#include <atltime.h>
#include <atlimage.h>
#include <stdio.h>
#include <stdarg.h>
#include "atlutiltest.h"

static int g_nFailures;
static FILE* g_pLog;

static void Log(const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  if (g_pLog != NULL) {
    vfprintf(g_pLog, fmt, args);
    fputc('\n', g_pLog);
    fflush(g_pLog);
  }
  va_end(args);
}

#define CHECK(c)                                          \
  do {                                                    \
    if (!(c)) {                                           \
      Log("FAIL %s:%d: %s", __FILE__, __LINE__, #c);      \
      g_nFailures++;                                      \
    }                                                     \
  } while (0)

// ---------------------------------------------------------------------
// The module: a service.

// Where the service tells the test what it did.
#define STATE_KEY TEXT("Software\\clang-retro\\AtlUtilTest")

static void SetState(LPCTSTR pszState) {
  CRegKey key;
  if (key.Create(HKEY_LOCAL_MACHINE, STATE_KEY) == ERROR_SUCCESS)
    key.SetStringValue(TEXT("State"), pszState);
}
static CString GetState() {
  CRegKey key;
  TCHAR sz[64] = TEXT("");
  ULONG n = 64;
  if (key.Open(HKEY_LOCAL_MACHINE, STATE_KEY, KEY_READ) == ERROR_SUCCESS)
    key.QueryStringValue(TEXT("State"), sz, &n);
  return sz;
}

class CTestModule : public CAtlServiceModuleT<CTestModule, IDS_SERVICENAME> {
public:
  static LPCOLESTR GetAppId() { return OLESTR("{3C1D7E52-8F4B-4A6D-9E21-5B7C0D9A1F64}"); }
  static HRESULT WINAPI UpdateRegistryAppId(BOOL bRegister) {
    CRegKey key;
    if (bRegister) {
      LONG l = key.Create(HKEY_CLASSES_ROOT, TEXT("AppID\\{3C1D7E52-8F4B-4A6D-9E21-5B7C0D9A1F64}"));
      if (l == ERROR_SUCCESS) l = key.SetStringValue(NULL, TEXT("AtlUtilTest"));
      return l == ERROR_SUCCESS ? S_OK : HRESULT_FROM_WIN32(l);
    }
    if (key.Open(HKEY_CLASSES_ROOT, TEXT("AppID"), KEY_WRITE) == ERROR_SUCCESS)
      key.RecurseDeleteKey(TEXT("{3C1D7E52-8F4B-4A6D-9E21-5B7C0D9A1F64}"));
    return S_OK;
  }
  // No type library to register.
  HRESULT RegisterServer(BOOL = FALSE, const CLSID* pCLSID = NULL) {
    return CAtlServiceModuleT<CTestModule, IDS_SERVICENAME>::RegisterServer(FALSE, pCLSID);
  }
  HRESULT UnregisterServer(BOOL = FALSE, const CLSID* pCLSID = NULL) {
    return CAtlServiceModuleT<CTestModule, IDS_SERVICENAME>::UnregisterServer(FALSE, pCLSID);
  }

  HRESULT PreMessageLoop(int nShowCmd) {
    HRESULT hr = CAtlServiceModuleT<CTestModule, IDS_SERVICENAME>::PreMessageLoop(nShowCmd);
    SetState(m_bService ? TEXT("running") : TEXT("local"));
    return hr;
  }
  void OnStop() {
    SetState(TEXT("stopping"));
    CAtlServiceModuleT<CTestModule, IDS_SERVICENAME>::OnStop();
  }
};

CTestModule _AtlModule;

// ---------------------------------------------------------------------
// Strings

static void TestStrings() {
  CString s = _T("abc");
  CHECK(s.GetLength() == 3 && s == _T("abc"));
  s.Format(_T("%d-%s-%5.2f-%%"), 42, _T("x"), 3.14159);
  CHECK(s == _T("42-x- 3.14-%"));
  CStringA a(s);
  CHECK(a == "42-x- 3.14-%");
  CStringW w = L"wide";
  CStringA b = w;
  CHECK(b == "wide");
  CStringA c;
  c.Format("%S and %s", L"other", "same");
  CHECK(c == "other and same");
  // Resources, through _AtlBaseModule.
  CString h;
  CHECK(h.LoadString(IDS_HELLO) && h == _T("Hello, %s"));
  CString f;
  f.Format(IDS_HELLO, _T("ATL"));
  CHECK(f == _T("Hello, ATL"));
  CString l((LPCTSTR)MAKEINTRESOURCE(IDS_LONG));
  CHECK(l.GetLength() > 256 && l.Right(3) == _T("end"));
  // Editing.
  CString e = _T("one two three");
  CHECK(e.Find(_T("two")) == 4 && e.Mid(4, 3) == _T("two"));
  CHECK(e.Replace(_T("t"), _T("T")) == 2 && e == _T("one Two Three"));
  e.MakeUpper();
  CHECK(e == _T("ONE TWO THREE"));
  int iStart = 0;
  CHECK(e.Tokenize(_T(" "), iStart) == _T("ONE"));
  // Copies share a buffer until one changes.
  CString s1 = _T("shared");
  CString s2 = s1;
  CHECK((LPCTSTR)s1 == (LPCTSTR)s2);
  s2 += _T("!");
  CHECK(s1 == _T("shared") && s2 == _T("shared!"));
  // BSTRs.
  BSTR bstr = a.AllocSysString();
  CHECK(bstr != NULL && ::SysStringLen(bstr) == 12 && bstr[0] == L'4');
  ::SysFreeString(bstr);
  // ATL's names for it.
  CAtlString at = _T("atl");
  CAtlStringW atw(at);
  CHECK(atw == L"atl");
  // Errors as CAtlException.
  bool bCaught = false;
  try {
    (void)s[100];
  } catch (CAtlException& ex) {
    bCaught = ex.m_hr == E_INVALIDARG;
  }
  CHECK(bCaught);
}

// ---------------------------------------------------------------------
// Collections

static int g_nThings;
struct CThing {
  int n;
  CThing(int i = 0) : n(i) { g_nThings++; }
  CThing(const CThing& t) : n(t.n) { g_nThings++; }
  ~CThing() { g_nThings--; }
  CThing& operator=(const CThing& t) {
    n = t.n;
    return *this;
  }
  bool operator==(const CThing& t) const { return n == t.n; }
};

// An object that counts its references.
class CCounted : public IUnknown {
public:
  LONG m_nRef = 1;
  STDMETHOD(QueryInterface)(REFIID riid, void** ppv) override {
    if (IsEqualIID(riid, IID_IUnknown)) {
      *ppv = this;
      AddRef();
      return S_OK;
    }
    *ppv = NULL;
    return E_NOINTERFACE;
  }
  STDMETHOD_(ULONG, AddRef)() override { return ++m_nRef; }
  STDMETHOD_(ULONG, Release)() override { return --m_nRef; }
};

static void TestSimpleCollections() {
  {
    CSimpleArray<CThing> arr;
    for (int i = 0; i < 20; i++) CHECK(arr.Add(CThing(i)));
    CHECK(arr.GetSize() == 20 && arr[7].n == 7 && g_nThings == 20);
    // Adding one of its own elements as it grows.
    CSimpleArray<CThing> grow;
    grow.Add(CThing(5));
    for (int i = 0; i < 6; i++) grow.Add(grow[0]);
    CHECK(grow.GetSize() == 7 && grow[6].n == 5);
    CHECK(arr.Find(CThing(12)) == 12 && arr.Find(CThing(99)) == -1);
    CHECK(arr.Remove(CThing(0)) && arr.GetSize() == 19 && arr[0].n == 1);
    CHECK(arr.RemoveAt(18) && !arr.RemoveAt(18));
    CSimpleArray<CThing> copy(arr);
    CHECK(copy.GetSize() == 18 && copy[17].n == 18);
    CHECK(copy.SetAtIndex(0, CThing(100)) && copy[0].n == 100 && arr[0].n == 1);
    CHECK(arr.m_nSize == 18 && arr.m_aT == arr.GetData());
  }
  CHECK(g_nThings == 0);

  CSimpleArray<CComBSTR> bstrs;
  bstrs.Add(CComBSTR(L"a"));
  bstrs.Add(CComBSTR(L"b"));
  CHECK(bstrs.Find(CComBSTR(L"b")) == 1);

  CSimpleValArray<int> vals;
  vals.Add(3);
  vals.Add(4);
  CHECK(vals[1] == 4 && vals.GetSize() == 2);

  CSimpleMap<int, CString> map;
  CHECK(map.Add(1, _T("one")) && map.Add(2, _T("two")) && map.Add(3, _T("three")));
  CHECK(map.GetSize() == 3 && map.Lookup(2) == _T("two") && map.Lookup(9).IsEmpty());
  CHECK(map.ReverseLookup(_T("three")) == 3 && map.FindKey(1) == 0);
  CHECK(map.SetAt(2, _T("TWO")) && map.GetValueAt(1) == _T("TWO"));
  CHECK(map.Remove(1) && map.GetSize() == 2 && map.GetKeyAt(0) == 2);
  map.RemoveAll();
  CHECK(map.GetSize() == 0);
}

static void TestAtlArray() {
  {
    CAtlArray<CString> arr;
    CHECK(arr.IsEmpty());
    for (int i = 0; i < 10; i++) {
      CString s;
      s.Format(_T("%d"), i);
      CHECK(arr.Add(s) == (size_t)i);
    }
    CHECK(arr.GetCount() == 10 && arr[3] == _T("3"));
    arr.InsertAt(2, _T("x"), 2);
    CHECK(arr.GetCount() == 12 && arr[1] == _T("1") && arr[2] == _T("x") && arr[3] == _T("x") &&
          arr[4] == _T("2"));
    arr.RemoveAt(2, 2);
    CHECK(arr.GetCount() == 10 && arr[2] == _T("2"));
    arr.SetAtGrow(14, _T("far"));
    CHECK(arr.GetCount() == 15 && arr[12].IsEmpty() && arr[14] == _T("far"));
    CAtlArray<CString> other;
    other.Add(_T("o"));
    CHECK(arr.Append(other) == 15 && arr.GetCount() == 16 && arr[15] == _T("o"));
    other.Copy(arr);
    CHECK(other.GetCount() == 16 && other[14] == _T("far"));
    arr.InsertArrayAt(0, &other);
    CHECK(arr.GetCount() == 32 && arr[14] == _T("far") && arr[30] == _T("far"));
    CHECK(arr.SetCount(3) && arr.GetCount() == 3);
    arr.FreeExtra();
    CHECK(arr[2] == _T("2"));
    bool bThrown = false;
    try {
      arr.GetAt(3);
    } catch (CAtlException& e) {
      bThrown = e.m_hr == E_INVALIDARG;
    }
    CHECK(bThrown);
  }
  {
    CAtlArray<CThing> things;
    things.SetCount(5);
    CHECK(g_nThings == 5);
    things.RemoveAt(1);
    CHECK(g_nThings == 4);
  }
  CHECK(g_nThings == 0);

  {
    CAutoPtrArray<CThing> autos;
    CAutoPtr<CThing> p(new CThing(1));
    autos.Add(p);
    CHECK(p.m_p == NULL && g_nThings == 1 && autos[0]->n == 1);
    CAutoPtr<CThing> p2(new CThing(2));
    autos.Add(p2);
    CHECK(g_nThings == 2);
  }
  CHECK(g_nThings == 0);

  CCounted counted;
  {
    CInterfaceArray<IUnknown> unks;
    unks.Add(&counted);
    unks.Add(&counted);
    CHECK(counted.m_nRef == 3);
    unks.RemoveAt(0);
    CHECK(counted.m_nRef == 2);
  }
  CHECK(counted.m_nRef == 1);
}

static void TestAtlList() {
  CAtlList<int> list;
  for (int i = 1; i <= 5; i++) list.AddTail(i);
  list.AddHead(0);
  CHECK(list.GetCount() == 6 && list.GetHead() == 0 && list.GetTail() == 5);
  int sum = 0;
  for (POSITION pos = list.GetHeadPosition(); pos != NULL;) sum += list.GetNext(pos);
  CHECK(sum == 15);
  POSITION pos3 = list.Find(3);
  CHECK(pos3 != NULL && list.GetAt(pos3) == 3 && list.FindIndex(3) == pos3);
  list.InsertBefore(pos3, 30);
  list.InsertAfter(pos3, 31);
  CHECK(list.GetCount() == 8 && list.GetAt(list.FindIndex(3)) == 30 &&
        list.GetAt(list.FindIndex(5)) == 31);
  list.RemoveAt(pos3);
  CHECK(list.Find(3) == NULL && list.GetCount() == 7);
  POSITION pos5 = list.Find(5);
  list.MoveToHead(pos5);
  CHECK(list.GetHead() == 5 && list.GetHeadPosition() == pos5);
  POSITION pos0 = list.Find(0);
  list.SwapElements(pos5, pos0);
  CHECK(list.GetHead() == 0 && list.GetAt(list.FindIndex(1)) == 5);
  CHECK(list.RemoveHead() == 0 && list.RemoveTail() == 4 && list.GetCount() == 5);
  // Backwards.
  int order[5];
  int n = 0;
  for (POSITION pos = list.GetTailPosition(); pos != NULL;) order[n++] = list.GetPrev(pos);
  CHECK(n == 5 && order[0] == 31 && order[4] == 5);
  list.RemoveAll();
  CHECK(list.IsEmpty() && list.GetHeadPosition() == NULL);

  {
    CAtlList<CString> strs;
    strs.AddTail(_T("b"));
    strs.AddHead(_T("a"));
    CAtlList<CString> more;
    more.AddTailList(&strs);
    more.AddHeadList(&strs);
    CHECK(more.GetCount() == 4 && more.GetHead() == _T("a") && more.GetTail() == _T("b"));
    CHECK(more.Find(_T("b")) == more.FindIndex(1));
  }
  {
    CAutoPtrList<CThing> autos;
    CAutoPtr<CThing> p7(new CThing(7));
    autos.AddTail(p7);
    CHECK(g_nThings == 1 && autos.GetHead()->n == 7);
  }
  CHECK(g_nThings == 0);
}

static void TestAtlMap() {
  CAtlMap<CString, int, CStringElementTraits<CString>> map;
  map.SetAt(_T("one"), 1);
  map[_T("two")] = 2;
  map.SetAt(_T("three"), 3);
  int v = 0;
  CHECK(map.GetCount() == 3 && map.Lookup(_T("two"), v) && v == 2);
  CHECK(!map.Lookup(_T("four"), v));
  const CAtlMap<CString, int, CStringElementTraits<CString>>::CPair* pPair = map.Lookup(_T("three"));
  CHECK(pPair != NULL && pPair->m_key == _T("three") && pPair->m_value == 3);
  map.SetAt(_T("one"), 11);
  CHECK(map.GetCount() == 3 && map[_T("one")] == 11);
  CHECK(map.RemoveKey(_T("two")) && !map.RemoveKey(_T("two")) && map.GetCount() == 2);
  int nSeen = 0, nSum = 0;
  for (POSITION pos = map.GetStartPosition(); pos != NULL;) {
    CString key;
    int val;
    map.GetNextAssoc(pos, key, val);
    nSeen++;
    nSum += val;
  }
  CHECK(nSeen == 2 && nSum == 14);

  // Ignoring case.
  CAtlMap<CString, int, CStringElementTraitsI<CString>> mapI;
  mapI[_T("Key")] = 5;
  CHECK(mapI.Lookup(_T("KEY"), v) && v == 5);

  // Many, rehashed as they come.
  CAtlMap<int, int> big;
  for (int i = 0; i < 5000; i++) big[i] = i * 2;
  CHECK(big.GetCount() == 5000 && big.GetHashTableSize() > 17);
  bool bAll = true;
  for (int i = 0; i < 5000; i++)
    if (!big.Lookup(i, v) || v != i * 2) bAll = false;
  CHECK(bAll);
  int nCount = 0;
  for (POSITION pos = big.GetStartPosition(); pos != NULL; big.GetNext(pos)) nCount++;
  CHECK(nCount == 5000);
  for (int i = 0; i < 5000; i += 2) big.RemoveKey(i);
  CHECK(big.GetCount() == 2500 && !big.Lookup(10, v) && big.Lookup(11, v) && v == 22);
  POSITION pos = big.GetStartPosition();
  big.RemoveAtPos(pos);
  CHECK(big.GetCount() == 2499);

  // GUID keys.
  CAtlMap<GUID, int> guids;
  guids[IID_IUnknown] = 1;
  guids[IID_IDispatch] = 2;
  CHECK(guids.Lookup(IID_IDispatch, v) && v == 2);
}

static void TestRBMap() {
  CRBMap<int, CString> map;
  map.SetAt(5, _T("five"));
  map.SetAt(1, _T("one"));
  map.SetAt(3, _T("three"));
  map.SetAt(3, _T("THREE"));
  CHECK(map.GetCount() == 3);
  int keys[3];
  int n = 0;
  for (POSITION pos = map.GetHeadPosition(); pos != NULL;) keys[n++] = map.GetNextKey(pos);
  CHECK(n == 3 && keys[0] == 1 && keys[1] == 3 && keys[2] == 5);
  CString s;
  CHECK(map.Lookup(3, s) && s == _T("THREE"));
  POSITION posAfter = map.FindFirstKeyAfter(3);
  CHECK(posAfter != NULL && map.GetKeyAt(posAfter) == 5);
  POSITION posTail = map.GetTailPosition();
  CHECK(map.GetPrev(posTail)->m_key == 5 && map.GetKeyAt(posTail) == 3);
  CHECK(map.RemoveKey(1) && map.GetCount() == 2 && map.GetKeyAt(map.GetHeadPosition()) == 3);

  CRBMultiMap<CString, int, CStringElementTraits<CString>> multi;
  multi.Insert(_T("b"), 1);
  multi.Insert(_T("a"), 2);
  multi.Insert(_T("b"), 3);
  multi.Insert(_T("c"), 4);
  CHECK(multi.GetCount() == 4);
  int nB = 0, nSum = 0;
  for (POSITION pos = multi.FindFirstWithKey(_T("b")); pos != NULL;) {
    nSum += multi.GetNextValueWithKey(pos, _T("b"));
    nB++;
  }
  CHECK(nB == 2 && nSum == 4);
  CHECK(multi.RemoveKey(_T("b")) == 2 && multi.GetCount() == 2);
  CHECK(multi.FindFirstWithKey(_T("b")) == NULL);
}

// ---------------------------------------------------------------------
// Memory and errors

static void TestMemory() {
  size_t r;
  CHECK(AtlMultiply<size_t>(&r, 3, 4) == S_OK && r == 12);
  CHECK(FAILED(AtlMultiply<size_t>(&r, (size_t)-1, 2)));
  int ri;
  CHECK(FAILED(AtlAdd<int>(&ri, 0x7FFFFFFF, 1)) && AtlAdd<int>(&ri, 2, 3) == S_OK && ri == 5);

  CHeapPtr<int> p;
  CHECK(p.Allocate(10));
  for (int i = 0; i < 10; i++) p[i] = i;
  CHECK(p.Reallocate(1000) && p[9] == 9);
  CHeapPtr<int> q(p);  // the memory passes to q
  CHECK(p.m_pData == NULL && q[5] == 5);
  q.Free();
  CHECK(q.m_pData == NULL);

  CComHeapPtr<WCHAR> co;
  CHECK(co.Allocate(4));
  ::lstrcpyW(co, L"abc");
  WCHAR* pDetached = co.Detach();
  CHECK(co.m_pData == NULL);
  ::CoTaskMemFree(pDetached);

  CTempBuffer<char, 16> small(10);
  CHECK((char*)small != NULL);
  ::memcpy((char*)small, "0123456789", 10);
  small.Reallocate(1000);  // off the stack: the contents go along
  CHECK(::memcmp((char*)small, "0123456789", 10) == 0);
  small.Reallocate(8);  // and back
  CHECK(::memcmp((char*)small, "01234567", 8) == 0);

  {
    CAutoPtr<CThing> a(new CThing(4));
    CAutoPtr<CThing> b;
    b = a;
    CHECK(a.m_p == NULL && b->n == 4 && g_nThings == 1);
    CAutoVectorPtr<CThing> v;
    CHECK(v.Allocate(3) && g_nThings == 4);
  }
  CHECK(g_nThings == 0);

  HANDLE hEvent = ::CreateEvent(NULL, TRUE, FALSE, NULL);
  {
    CHandle h(hEvent);
    CHandle h2(h);  // passes too
    CHECK(h.m_h == NULL && h2 == hEvent);
  }
  // Closed: the handle no longer works.
  CHECK(!::SetEvent(hEvent));

  bool bCaught = false;
  try {
    AtlThrow(E_ACCESSDENIED);
  } catch (CAtlException& e) {
    bCaught = e == E_ACCESSDENIED;
  }
  CHECK(bCaught);
  bCaught = false;
  try {
    ::SetLastError(ERROR_FILE_NOT_FOUND);
    AtlThrowLastWin32();
  } catch (CAtlException& e) {
    bCaught = e.m_hr == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
  }
  CHECK(bCaught);
}

// ---------------------------------------------------------------------
// CComSafeArray

static void TestSafeArray() {
  CComSafeArray<LONG> sa;
  CHECK(SUCCEEDED(sa.Create(5, 1)));
  CHECK(sa.GetCount() == 5 && sa.GetLowerBound() == 1 && sa.GetUpperBound() == 5);
  CHECK(sa.GetType() == VT_I4 && sa.GetDimensions() == 1);
  for (LONG i = 1; i <= 5; i++) CHECK(SUCCEEDED(sa.SetAt(i, i * 10)));
  CHECK(sa[3] == 30 && sa.GetAt(5) == 50);
  sa[2] = 22;
  CHECK(sa.GetAt(2) == 22);
  CHECK(SUCCEEDED(sa.Add(60)) && sa.GetCount() == 6 && sa[6] == 60);
  LONG more[] = {70, 80};
  CHECK(SUCCEEDED(sa.Add(2, more)) && sa.GetCount() == 8 && sa[8] == 80);
  CHECK(SUCCEEDED(sa.Resize(3, 1)) && sa.GetCount() == 3 && sa[3] == 30);
  CHECK(sa.SetAt(9, 1) == E_INVALIDARG);
  // Through a VARIANT, as a server returns it.
  CComVariant var;
  var.vt = VT_ARRAY | VT_I4;
  CHECK(SUCCEEDED(sa.CopyTo(&var.parray)));
  CComSafeArray<LONG> back(var.parray);
  CHECK(back.GetCount() == 3 && back[1] == 10 && back[2] == 22);
  // Detached and attached again.
  LPSAFEARRAY psa = back.Detach();
  CHECK(back.m_psa == NULL && psa != NULL);
  CHECK(SUCCEEDED(back.Attach(psa)) && back[3] == 30);
  CComSafeArray<LONG> copy(back);
  copy[1] = 99;
  CHECK(back[1] == 10 && copy[1] == 99);
  // An array of another type is refused.
  CComSafeArray<double> dbl;
  CHECK(dbl.Attach(psa) == E_INVALIDARG);

  CComSafeArray<BSTR> strs;
  CComBSTR bstr(L"first");
  CHECK(SUCCEEDED(strs.Add(bstr)));
  CHECK(SUCCEEDED(strs.Add(::SysAllocString(L"second"), FALSE)));
  CHECK(strs.GetCount() == 2 && strs[0] != bstr.m_str && ::lstrcmpW(strs[0], L"first") == 0 &&
        ::lstrcmpW(strs[1], L"second") == 0);

  CComSafeArray<VARIANT> vars(2);
  CHECK(SUCCEEDED(vars.SetAt(0, CComVariant(L"text"))));
  CHECK(SUCCEEDED(vars.SetAt(1, CComVariant(7L))));
  CHECK(vars[0].vt == VT_BSTR && vars[1].vt == VT_I4 && vars[1].lVal == 7);

  // Two dimensions.
  CComSafeArrayBound bounds[2] = {CComSafeArrayBound(3), CComSafeArrayBound(4, 1)};
  CComSafeArray<SHORT> grid(bounds, 2);
  CHECK(grid.GetDimensions() == 2 && grid.GetCount(0) == 3 && grid.GetCount(1) == 4 &&
        grid.GetLowerBound(1) == 1);
  LONG idx[2] = {2, 4};
  CHECK(SUCCEEDED(grid.MultiDimSetAt(idx, 42)));
  SHORT sv = 0;
  CHECK(SUCCEEDED(grid.MultiDimGetAt(idx, sv)) && sv == 42);
}

// ---------------------------------------------------------------------
// CComDispatchDriver, over a dispatch of the test's own: a property
// Value (1) and methods Add (2: Value += a) and Sub (3: a - b).
class CDisp : public IDispatch {
public:
  LONG m_nRef = 1;
  long m_nValue = 0;
  STDMETHOD(QueryInterface)(REFIID riid, void** ppv) override {
    if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_IDispatch)) {
      *ppv = static_cast<IDispatch*>(this);
      AddRef();
      return S_OK;
    }
    *ppv = NULL;
    return E_NOINTERFACE;
  }
  STDMETHOD_(ULONG, AddRef)() override { return ++m_nRef; }
  STDMETHOD_(ULONG, Release)() override { return --m_nRef; }
  STDMETHOD(GetTypeInfoCount)(UINT* pctinfo) override {
    *pctinfo = 0;
    return S_OK;
  }
  STDMETHOD(GetTypeInfo)(UINT, LCID, ITypeInfo**) override { return E_NOTIMPL; }
  STDMETHOD(GetIDsOfNames)(REFIID, LPOLESTR* names, UINT n, LCID, DISPID* ids) override {
    static const OLECHAR* known[] = {OLESTR("Value"), OLESTR("Add"), OLESTR("Sub")};
    for (UINT i = 0; i < n; i++) {
      ids[i] = DISPID_UNKNOWN;
      for (int k = 0; k < 3; k++)
        if (CComBSTR(names[i]) == CComBSTR(known[k])) ids[i] = k + 1;
      if (ids[i] == DISPID_UNKNOWN) return DISP_E_UNKNOWNNAME;
    }
    return S_OK;
  }
  STDMETHOD(Invoke)(DISPID id, REFIID, LCID, WORD flags, DISPPARAMS* dp, VARIANT* pResult,
                    EXCEPINFO*, UINT*) override {
    CComVariant a;
    if (id == 1 && (flags & DISPATCH_PROPERTYGET)) {
      if (pResult == NULL) return E_INVALIDARG;
      CComVariant(m_nValue).Detach(pResult);
      return S_OK;
    }
    if (id == 1 && (flags & DISPATCH_PROPERTYPUT)) {
      if (dp->cArgs != 1 || dp->cNamedArgs != 1 || dp->rgdispidNamedArgs[0] != DISPID_PROPERTYPUT)
        return DISP_E_BADPARAMCOUNT;
      if (FAILED(a.ChangeType(VT_I4, &dp->rgvarg[0]))) return DISP_E_TYPEMISMATCH;
      m_nValue = a.lVal;
      return S_OK;
    }
    if (id == 2 && (flags & DISPATCH_METHOD)) {
      if (dp->cArgs != 1) return DISP_E_BADPARAMCOUNT;
      if (FAILED(a.ChangeType(VT_I4, &dp->rgvarg[0]))) return DISP_E_TYPEMISMATCH;
      m_nValue += a.lVal;
      if (pResult != NULL) CComVariant(m_nValue).Detach(pResult);
      return S_OK;
    }
    if (id == 3 && (flags & DISPATCH_METHOD)) {
      // Sub(a, b): the arguments come last first.
      if (dp->cArgs != 2) return DISP_E_BADPARAMCOUNT;
      CComVariant b;
      a.ChangeType(VT_I4, &dp->rgvarg[1]);
      b.ChangeType(VT_I4, &dp->rgvarg[0]);
      if (pResult != NULL) CComVariant(a.lVal - b.lVal).Detach(pResult);
      return S_OK;
    }
    return DISP_E_MEMBERNOTFOUND;
  }
};

static void TestDispatchDriver() {
  CDisp disp;
  {
    CComPtr<IUnknown> spUnk(&disp);
    CComDispatchDriver dd(spUnk);
    CHECK(dd.p != NULL);
    CComVariant v(5L);
    CHECK(SUCCEEDED(dd.PutPropertyByName(OLESTR("Value"), &v)) && disp.m_nValue == 5);
    CComVariant r;
    CHECK(SUCCEEDED(dd.GetProperty(1, &r)) && r.vt == VT_I4 && r.lVal == 5);
    DISPID id;
    CHECK(SUCCEEDED(dd.GetIDOfName(OLESTR("Add"), &id)) && id == 2);
    CComVariant arg(3L), res;
    CHECK(SUCCEEDED(dd.Invoke1(OLESTR("Add"), &arg, &res)) && res.lVal == 8 && disp.m_nValue == 8);
    CComVariant x(10L), y(4L);
    res.Clear();
    CHECK(SUCCEEDED(dd.Invoke2(OLESTR("Sub"), &x, &y, &res)) && res.lVal == 6);
    CComVariant args[2] = {CComVariant(1L), CComVariant(10L)};  // last first: 10 - 1
    res.Clear();
    CHECK(SUCCEEDED(dd.InvokeN((DISPID)3, args, 2, &res)) && res.lVal == 9);
    CHECK(dd.Invoke0(OLESTR("Nothing")) == DISP_E_UNKNOWNNAME);
    // The same on CComPtr<IDispatch>, and statically.
    CComPtr<IDispatch> spDisp(&disp);
    r.Clear();
    CHECK(SUCCEEDED(spDisp.GetPropertyByName(OLESTR("Value"), &r)) && r.lVal == 8);
    CComVariant v2(1L);
    CHECK(SUCCEEDED(CComDispatchDriver::PutProperty(&disp, 1, &v2)) && disp.m_nValue == 1);
  }
  CHECK(disp.m_nRef == 1);
}

// ---------------------------------------------------------------------
// Files

static void TestFiles() {
  TCHAR szDir[MAX_PATH], szPath[MAX_PATH];
  ::GetTempPath(MAX_PATH, szDir);
  ::lstrcpy(szPath, szDir);
  ::lstrcat(szPath, TEXT("atlutiltest.dat"));
  {
    CAtlFile file;
    CHECK(SUCCEEDED(file.Create(szPath, GENERIC_READ | GENERIC_WRITE, 0, CREATE_ALWAYS)));
    DWORD nWritten = 0;
    CHECK(SUCCEEDED(file.Write("hello, file", 11, &nWritten)) && nWritten == 11);
    ULONGLONG nPos = 0, nSize = 0;
    CHECK(SUCCEEDED(file.GetPosition(nPos)) && nPos == 11);
    CHECK(SUCCEEDED(file.GetSize(nSize)) && nSize == 11);
    CHECK(SUCCEEDED(file.Seek(7, FILE_BEGIN)));
    char buf[8] = {};
    CHECK(SUCCEEDED(file.Read(buf, 4)) && ::memcmp(buf, "file", 4) == 0);
    // Reading past the end.
    CHECK(file.Read(buf, 4) == HRESULT_FROM_WIN32(ERROR_HANDLE_EOF));
    CHECK(SUCCEEDED(file.LockRange(0, 5)) && SUCCEEDED(file.UnlockRange(0, 5)));
    CHECK(SUCCEEDED(file.SetSize(5)) && SUCCEEDED(file.GetSize(nSize)) && nSize == 5);
    CHECK(SUCCEEDED(file.Flush()));
  }
  {
    CAtlFile file;
    CHECK(SUCCEEDED(file.Create(szPath, GENERIC_READ, FILE_SHARE_READ, OPEN_EXISTING)));
    CAtlFileMapping<char> map;
    CHECK(SUCCEEDED(map.MapFile(file)));
    CHECK(map.GetMappingSize() == 5 && ::memcmp((char*)map, "hello", 5) == 0);
  }
  CAtlFile missing;
  CHECK(missing.Create(TEXT("Z:\\no\\such\\file"), GENERIC_READ, 0, OPEN_EXISTING) ==
            HRESULT_FROM_WIN32(ERROR_PATH_NOT_FOUND) ||
        missing.m_h == NULL);
  ::DeleteFile(szPath);

  // A temporary file, kept under a name.
  TCHAR szKept[MAX_PATH];
  ::lstrcpy(szKept, szDir);
  ::lstrcat(szKept, TEXT("atlutiltest.kept"));
  {
    CAtlTemporaryFile tmp;
    CHECK(SUCCEEDED(tmp.Create()));
    CString strTemp = tmp.TempFileName();
    CHECK(::GetFileAttributes(strTemp) != INVALID_FILE_ATTRIBUTES);
    CHECK(SUCCEEDED(tmp.Write("kept", 4)));
    CHECK(SUCCEEDED(tmp.Close(szKept)));
    CHECK(::GetFileAttributes(strTemp) == INVALID_FILE_ATTRIBUTES);
  }
  CHECK(::GetFileAttributes(szKept) != INVALID_FILE_ATTRIBUTES);
  ::DeleteFile(szKept);
  {
    // And one that goes.
    CString strTemp;
    {
      CAtlTemporaryFile tmp;
      CHECK(SUCCEEDED(tmp.Create()));
      strTemp = tmp.TempFileName();
    }
    CHECK(::GetFileAttributes(strTemp) == INVALID_FILE_ATTRIBUTES);
  }

  // Shared memory, mapped twice.
  CAtlFileMapping<int> shm1, shm2;
  BOOL bExisted = TRUE;
  CHECK(SUCCEEDED(shm1.MapSharedMem(4096, TEXT("AtlUtilTestShm"), &bExisted)) && !bExisted);
  ((int*)shm1)[10] = 1234;
  CHECK(SUCCEEDED(shm2.OpenMapping(TEXT("AtlUtilTestShm"), 4096)) && ((int*)shm2)[10] == 1234);
}

// ---------------------------------------------------------------------
// The service

static bool WaitForState(SC_HANDLE hService, DWORD dwState) {
  for (int i = 0; i < 300; i++) {
    SERVICE_STATUS status;
    if (::QueryServiceStatus(hService, &status) && status.dwCurrentState == dwState) return true;
    ::Sleep(100);
  }
  return false;
}

static void TestService() {
  // As /Service gives it: installed, and the AppID names it.
  CHECK(lstrcmp(_AtlModule.m_szServiceName, TEXT("AtlUtilTestSvc")) == 0);
  HRESULT hr = E_FAIL;
  CHECK(!_AtlModule.ParseCommandLine(TEXT("atlutiltest.exe /Service"), &hr));
  CHECK(SUCCEEDED(hr));
  CHECK(_AtlModule.IsInstalled());
  CRegKey key;
  TCHAR sz[64] = TEXT("");
  ULONG n = 64;
  CHECK(key.Open(HKEY_CLASSES_ROOT, TEXT("AppID\\{3C1D7E52-8F4B-4A6D-9E21-5B7C0D9A1F64}"),
                 KEY_READ) == ERROR_SUCCESS &&
        key.QueryStringValue(TEXT("LocalService"), sz, &n) == ERROR_SUCCESS &&
        lstrcmp(sz, TEXT("AtlUtilTestSvc")) == 0);
  key.Close();

  SC_HANDLE hSCM = ::OpenSCManager(NULL, NULL, SC_MANAGER_ALL_ACCESS);
  CHECK(hSCM != NULL);
  SC_HANDLE hService = hSCM ? ::OpenService(hSCM, TEXT("AtlUtilTestSvc"), SERVICE_ALL_ACCESS) : NULL;
  CHECK(hService != NULL);
  if (hService != NULL) {
    // Its command line is this program's path, quoted.
    BYTE buf[4096];
    QUERY_SERVICE_CONFIG* pConfig = (QUERY_SERVICE_CONFIG*)buf;
    DWORD cb = 0;
    CHECK(::QueryServiceConfig(hService, pConfig, sizeof(buf), &cb));
    TCHAR szExe[MAX_PATH + 2];
    szExe[0] = TEXT('"');
    DWORD nExe = ::GetModuleFileName(NULL, szExe + 1, MAX_PATH);
    szExe[nExe + 1] = TEXT('"');
    szExe[nExe + 2] = 0;
    CHECK(lstrcmpi(pConfig->lpBinaryPathName, szExe) == 0);
    // Started with -AsService, so that this program knows to be it.
    TCHAR szCmd[MAX_PATH + 16];
    lstrcpy(szCmd, szExe);
    lstrcat(szCmd, TEXT(" -AsService"));
    CHECK(::ChangeServiceConfig(hService, SERVICE_NO_CHANGE, SERVICE_NO_CHANGE, SERVICE_NO_CHANGE,
                                szCmd, NULL, NULL, NULL, NULL, NULL, NULL));

    SetState(TEXT(""));
    CHECK(::StartService(hService, 0, NULL));
    CHECK(WaitForState(hService, SERVICE_RUNNING));
    CHECK(GetState() == _T("running"));
    SERVICE_STATUS status;
    CHECK(::ControlService(hService, SERVICE_CONTROL_STOP, &status));
    CHECK(WaitForState(hService, SERVICE_STOPPED));
    CHECK(GetState() == _T("stopping"));
    ::CloseServiceHandle(hService);
  }
  if (hSCM != NULL) ::CloseServiceHandle(hSCM);

  // /UnregServer: uninstalled, and the AppID gone.
  CHECK(!_AtlModule.ParseCommandLine(TEXT("atlutiltest.exe -UnregServer"), &hr));
  CHECK(SUCCEEDED(hr));
  CHECK(!_AtlModule.IsInstalled());
  CHECK(key.Open(HKEY_CLASSES_ROOT, TEXT("AppID\\{3C1D7E52-8F4B-4A6D-9E21-5B7C0D9A1F64}"),
                 KEY_READ) != ERROR_SUCCESS);
  RegDeleteKey(HKEY_LOCAL_MACHINE, STATE_KEY);
}

// ---------------------------------------------------------------------
// CPath.

static void TestPath() {
  CPath p(_T("C:\\dir"));
  p.Append(_T("sub\\file.txt"));
  CHECK(CString((LPCTSTR)p) == _T("C:\\dir\\sub\\file.txt"));
  CHECK(p.FindExtension() == 15);  // indexes, as in ATL
  CHECK(p.GetExtension() == _T(".txt"));
  CHECK(p.FindFileName() == 11);
  CHECK(p.MatchSpec(_T("*.txt")));
  CHECK(!p.MatchSpec(_T("*.doc")));
  CHECK(!p.IsRelative());
  CHECK(p.GetDriveNumber() == 2);
  CHECK(p.RenameExtension(_T(".log")));
  CHECK(CString(p.m_strPath) == _T("C:\\dir\\sub\\file.log"));
  p.RemoveExtension();
  CHECK(p.m_strPath == _T("C:\\dir\\sub\\file"));
  p.AddExtension(_T(".bak"));
  CHECK(p.m_strPath == _T("C:\\dir\\sub\\file.bak"));
  CHECK(p.RemoveFileSpec());
  CHECK(p.m_strPath == _T("C:\\dir\\sub"));
  p.AddBackslash();
  CHECK(p.m_strPath == _T("C:\\dir\\sub\\"));
  p.RemoveBackslash();
  CHECK(p.m_strPath == _T("C:\\dir\\sub"));
  p.StripToRoot();
  CHECK(p.m_strPath == _T("C:\\"));
  CHECK(p.IsRoot());

  CPath q(_T("C:\\a\\.\\b\\..\\c.txt"));
  q.Canonicalize();
  CHECK(q.m_strPath == _T("C:\\a\\c.txt"));
  CPath r(_T("rel\\x"));
  CHECK(r.IsRelative());
  CPath c;
  c.Combine(_T("C:\\one"), _T("two.txt"));
  CHECK(c.m_strPath == _T("C:\\one\\two.txt"));
  CPath s(_T("C:\\with space\\x"));
  s.QuoteSpaces();
  CHECK(s.m_strPath == _T("\"C:\\with space\\x\""));
  s.UnquoteSpaces();
  CHECK(s.m_strPath == _T("C:\\with space\\x"));
  CPath f(_T("C:\\dir\\name.ext"));
  f.StripPath();
  CHECK(f.m_strPath == _T("name.ext"));
  CHECK(f.IsFileSpec());
  CHECK(CPath(_T("\\\\server\\share")).IsUNCServerShare());
  CHECK(CPath(_T("C:\\a\\b")).CommonPrefix(_T("C:\\a\\c")).m_strPath == _T("C:\\a"));
  CPath rel;
  CHECK(rel.RelativePathTo(_T("C:\\a\\b"), FILE_ATTRIBUTE_DIRECTORY, _T("C:\\a\\c\\d.txt"), 0));
  CHECK(rel.m_strPath == _T("..\\c\\d.txt"));

  TCHAR szWin[MAX_PATH];
  ::GetWindowsDirectory(szWin, MAX_PATH);
  CPath w(szWin);
  CHECK(w.FileExists());
  CHECK(w.IsDirectory());
  w.Append(_T("no-such-file.xyz"));
  CHECK(!w.FileExists());

  // The ANSI and Unicode forms, whatever TCHAR is.
  CPathA pa("x\\y.c");
  CHECK(pa.FindExtension() == 3 && pa.GetExtension() == ".c");
  CPathW pw(L"x\\y.c");
  CHECK(pw.FindExtension() == 3 && pw.GetExtension() == L".c");
}

// ---------------------------------------------------------------------
// atlsecurity.h.

static void TestSecurity() {
  // Well-known SIDs, their string forms and comparisons.
  CHECK(CString(Sids::World().Sid()) == _T("S-1-1-0"));
  CHECK(CString(Sids::System().Sid()) == _T("S-1-5-18"));
  CHECK(CString(Sids::Admins().Sid()) == _T("S-1-5-32-544"));
  CHECK(Sids::Admins() == Sids::Admins());
  CHECK(Sids::Admins() != Sids::Users());
  CHECK(Sids::Admins().EqualPrefix(Sids::Users()));
  CHECK(Sids::Users().GetSubAuthority(1) == DOMAIN_ALIAS_RID_USERS);
  CHECK(Sids::World().IsValid());
  SID_IDENTIFIER_AUTHORITY nt = SECURITY_NT_AUTHORITY;
  CSid admins(nt, 2, SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS);
  CHECK(admins == Sids::Admins());

  // CSid in an ATL collection.
  CAtlMap<CSid, int> map;
  map[Sids::World()] = 1;
  map[Sids::Admins()] = 2;
  int n = 0;
  CHECK(map.Lookup(Sids::Admins(), n) && n == 2);

  // The process token: its user, a SID that round-trips through a name.
  CAccessToken tok;
  CHECK(tok.GetProcessToken(TOKEN_QUERY));
  CSid user;
  CHECK(tok.GetUser(&user));
  CHECK(user.IsValid());
  Log("token user %s\\%s (%s)", (const char*)CStringA(user.Domain()),
      (const char*)CStringA(user.AccountName()), (const char*)CStringA(user.Sid()));
  CHECK(*user.AccountName() != 0);
  CSid byName;
  CString strFull = CString(user.Domain()) + _T("\\") + user.AccountName();
  CHECK(byName.LoadAccount(strFull));
  CHECK(byName == user);
  CSid::CSidArray groups;
  CHECK(tok.GetGroups(&groups));
  CHECK(groups.GetCount() > 0);
  TOKEN_TYPE type;
  CHECK(tok.GetType(&type) && type == TokenPrimary);

  // A DACL: the denied entry goes first, whatever the order of adds.
  CDacl dacl;
  CHECK(dacl.IsEmpty());
  CHECK(dacl.AddAllowedAce(Sids::World(), GENERIC_READ));
  CHECK(dacl.AddAllowedAce(user, GENERIC_ALL));
  CHECK(dacl.AddDeniedAce(Sids::Guests(), GENERIC_WRITE));
  CHECK(dacl.GetAceCount() == 3);
  CSid sid0;
  ACCESS_MASK mask0 = 0;
  BYTE type0 = 0;
  dacl.GetAclEntry(0, &sid0, &mask0, &type0);
  CHECK(sid0 == Sids::Guests() && type0 == ACCESS_DENIED_ACE_TYPE && mask0 == GENERIC_WRITE);
  CHECK(::IsValidAcl((PACL)dacl.GetPACL()));
  dacl.RemoveAces(Sids::Guests());
  CHECK(dacl.GetAceCount() == 2);

  // A security descriptor: built, to SDDL and back.
  CSecurityDesc sd;
  sd.SetOwner(Sids::Admins());
  sd.SetGroup(Sids::System());
  sd.SetDacl(dacl);
  CString strSddl;
  CHECK(sd.ToString(&strSddl));
  Log("sddl %s", (const char*)CStringA(strSddl));
  CHECK(strSddl.Find(_T("O:BA")) == 0);
  CHECK(strSddl.Find(_T("G:SY")) > 0);
  CHECK(strSddl.Find(_T("D:")) > 0);
  CSecurityDesc sd2;
  CHECK(sd2.FromString(strSddl));
  CSid owner;
  CHECK(sd2.GetOwner(&owner) && owner == Sids::Admins());
  CDacl dacl2;
  bool bPresent = false;
  CHECK(sd2.GetDacl(&dacl2, &bPresent) && bPresent);
  CHECK(dacl2.GetAceCount() == 2);
  CHECK(::IsValidSecurityDescriptor((PSECURITY_DESCRIPTOR)sd2.GetPSECURITY_DESCRIPTOR()));
  CSecurityDesc sd3;
  CHECK(sd3.FromString(_T("D:(A;;GA;;;WD)")));
  CDacl dacl3;
  CHECK(sd3.GetDacl(&dacl3) && dacl3.GetAceCount() == 1);

  // An event created with the descriptor; its DACL read back, and replaced.
  CSecurityAttributes sa(sd);
  HANDLE hEvent = ::CreateEvent(&sa, TRUE, FALSE, NULL);
  CHECK(hEvent != NULL);
  if (hEvent != NULL) {
    CDacl got;
    CHECK(AtlGetDacl(hEvent, SE_KERNEL_OBJECT, &got));
    CHECK(got.GetAceCount() == 2);
    CDacl one;
    one.AddAllowedAce(user, EVENT_ALL_ACCESS);
    CHECK(AtlSetDacl(hEvent, SE_KERNEL_OBJECT, one));
    CHECK(AtlGetDacl(hEvent, SE_KERNEL_OBJECT, &got));
    CHECK(got.GetAceCount() == 1);
    ::CloseHandle(hEvent);
  }

  // A file's owner and DACL, by name.
  TCHAR szTmp[MAX_PATH], szFile[MAX_PATH];
  ::GetTempPath(MAX_PATH, szTmp);
  ::GetTempFileName(szTmp, _T("sec"), 0, szFile);
  CSid fileOwner;
  CHECK(AtlGetOwnerSid(szFile, SE_FILE_OBJECT, &fileOwner));
  CHECK(fileOwner.IsValid());
  CDacl fileDacl;
  fileDacl.AddAllowedAce(user, FILE_ALL_ACCESS);
  fileDacl.AddAllowedAce(Sids::World(), FILE_GENERIC_READ);
  CHECK(AtlSetDacl(szFile, SE_FILE_OBJECT, fileDacl));
  CDacl fileDacl2;
  CHECK(AtlGetDacl(szFile, SE_FILE_OBJECT, &fileDacl2));
  CHECK(fileDacl2.GetAceCount() >= 2);
  ::DeleteFile(szFile);
}

static void TestTime() {
  // CTime and CTimeSpan without MFC, and ATL's names for them.
  ATL::CTime t(2003, 7, 15, 13, 45, 30);
  CHECK(t.GetYear() == 2003 && t.GetMonth() == 7 && t.GetDay() == 15);
  CHECK(t.GetHour() == 13 && t.GetMinute() == 45 && t.GetSecond() == 30);
  CHECK(t.GetDayOfWeek() == 3);  // a Tuesday
  CHECK(t.Format(_T("%Y-%m-%d %H:%M:%S")) == _T("2003-07-15 13:45:30"));
  CHECK(t.Format(_T("%A, %B %#d")) == _T("Tuesday, July 15"));
  CTimeSpan span(1, 2, 3, 4);
  CHECK(span.GetTotalSeconds() == 93784);
  CHECK(span.Format(_T("%D:%H:%M:%S")) == _T("1:02:03:04"));
  CTime u = t + span;
  CHECK(u - t == span);
  CHECK(u.GetDay() == 16 && u.GetHour() == 15);
  CHECK(CTime::GetCurrentTime() > t);

  // CFileTime: 100-nanosecond units, and spans of them.
  CFileTime ft(t.GetTime() * CFileTime::Second + 116444736000000000ULL);
  CHECK(CTime(ft).GetTime() == t.GetTime());  // a FILETIME, so CTime takes it
  CFileTime ft2 = ft + CFileTimeSpan(CFileTime::Day + 5 * CFileTime::Millisecond);
  CHECK((ft2 - ft).GetTimeSpan() == (LONGLONG)(CFileTime::Day + 50000));
  CHECK(ft2 > ft && ft != ft2);
  ft2 -= CFileTimeSpan(CFileTime::Day);
  CHECK(ft2.GetTime() == ft.GetTime() + 50000);
  CHECK(ft.UTCToLocal().LocalToUTC() == ft);
  CFileTime now = CFileTime::GetCurrentTime();
  CHECK(now > ft);
  CHECK(CFileTime::Week == 7 * 24 * 3600 * 10000000ULL);
  ATL::CFileTime copy = now;
  CHECK(copy == now);
  SYSTEMTIME st;
  CHECK(::FileTimeToSystemTime(&ft, &st) && st.wYear == 2003);
}

static void TestImage() {
  TCHAR szDir[MAX_PATH], szPng[MAX_PATH], szBmp[MAX_PATH], szJpg[MAX_PATH];
  ::GetTempPath(MAX_PATH, szDir);
  wsprintf(szPng, TEXT("%satlimg.png"), szDir);
  wsprintf(szBmp, TEXT("%satlimg.bmp"), szDir);
  wsprintf(szJpg, TEXT("%satlimg.jpg"), szDir);

  // A 32-bit image with an alpha channel, through PNG and back.
  CImage img;
  CHECK(img.IsNull());
  CHECK(img.Create(8, 6, 32, CImage::createAlphaChannel));
  CHECK(!img.IsNull() && img.IsDIBSection() && !img.IsIndexed());
  CHECK(img.GetWidth() == 8 && img.GetHeight() == 6 && img.GetBPP() == 32);
  CHECK(img.GetPitch() == -32);  // bottom-up: GetBits is the top row
  for (int y = 0; y < 6; y++)
    for (int x = 0; x < 8; x++)
      *(DWORD*)img.GetPixelAddress(x, y) = (x < 4 ? 0xFF000000u : 0x80000000u) |
                                           (DWORD)(x * 30) << 16 | (DWORD)(y * 40) << 8 | 0x11;
  CHECK((img.GetPixel(2, 3) & 0xFFFFFF) == RGB(60, 120, 0x11));
  CHECK(SUCCEEDED(img.Save(szPng)));  // the format from the extension
  CImage png;
  CHECK(SUCCEEDED(png.Load(szPng)));
  CHECK(png.GetWidth() == 8 && png.GetHeight() == 6 && png.GetBPP() == 32);
  CHECK(*(DWORD*)png.GetPixelAddress(2, 3) == *(DWORD*)img.GetPixelAddress(2, 3));
  CHECK(*(DWORD*)png.GetPixelAddress(6, 5) == *(DWORD*)img.GetPixelAddress(6, 5));
  CHECK((*(DWORD*)png.GetPixelAddress(6, 5) >> 24) == 0x80);  // alpha kept, unpremultiplied

  // Drawn onto white: the opaque half as it is, the other half blended.
  CImage dest;
  CHECK(dest.Create(8, 6, 24));
  HDC hdc = dest.GetDC();
  RECT rc = {0, 0, 8, 6};
  ::FillRect(hdc, &rc, (HBRUSH)::GetStockObject(WHITE_BRUSH));
  CHECK(png.Draw(hdc, 0, 0));
  dest.ReleaseDC();
  CHECK(dest.GetPixel(1, 1) == RGB(30, 40, 0x11));
  // At (5, 0), (150, 0, 0x11) at alpha 0x80: AlphaBlend takes the pixels
  // as premultiplied, so white shows through by half. (Red, 150 + 127,
  // is out of range, as it is for unpremultiplied pixels on Windows.)
  COLORREF c = dest.GetPixel(5, 0);
  CHECK(GetGValue(c) >= 0x7E && GetGValue(c) <= 0x80);
  CHECK(GetBValue(c) >= 0x8F && GetBValue(c) <= 0x91);

  // 24 bits, through BMP and JPEG, saved by GUID; a stream too.
  CImage rgb;
  CHECK(rgb.Create(5, -4, 24));  // top-down
  CHECK(rgb.GetPitch() == 16);
  rgb.SetPixelRGB(1, 2, 10, 200, 30);
  CHECK(rgb.GetPixel(1, 2) == RGB(10, 200, 30));
  CHECK(SUCCEEDED(rgb.Save(szBmp, Gdiplus::ImageFormatBMP)));
  CHECK(SUCCEEDED(rgb.Save(szJpg, Gdiplus::ImageFormatJPEG)));
  CImage bmp;
  CHECK(SUCCEEDED(bmp.Load(szBmp)));
  CHECK(bmp.GetBPP() == 24 && bmp.GetPixel(1, 2) == RGB(10, 200, 30));
  CImage jpg;
  CHECK(SUCCEEDED(jpg.Load(szJpg)) && jpg.GetWidth() == 5 && jpg.GetHeight() == 4);
  IStream* pStream = NULL;
  CHECK(SUCCEEDED(::CreateStreamOnHGlobal(NULL, TRUE, &pStream)));
  CHECK(SUCCEEDED(rgb.Save(pStream, Gdiplus::ImageFormatPNG)));
  LARGE_INTEGER zero = {};
  pStream->Seek(zero, STREAM_SEEK_SET, NULL);
  CImage fromStream;
  CHECK(SUCCEEDED(fromStream.Load(pStream)));
  CHECK(fromStream.GetPixel(1, 2) == RGB(10, 200, 30));
  pStream->Release();
  CHECK(FAILED(fromStream.Save(szPng, GUID_NULL)) == false);
  CImage missing;
  CHECK(FAILED(missing.Load(TEXT("no such file.png"))) && missing.IsNull());

  // An 8-bit indexed image: its color table, SetPixelIndexed, and a
  // transparent color for Draw.
  CImage ind;
  CHECK(ind.Create(4, 4, 8));
  CHECK(ind.IsIndexed() && ind.GetMaxColorTableEntries() == 256);
  RGBQUAD q[2] = {{0, 0, 0xFF, 0}, {0xFF, 0, 0, 0}};  // red, blue
  ind.SetColorTable(0, 2, q);
  RGBQUAD back[2];
  ind.GetColorTable(0, 2, back);
  CHECK(back[0].rgbRed == 0xFF && back[1].rgbBlue == 0xFF);
  for (int y = 0; y < 4; y++)
    for (int x = 0; x < 4; x++) ind.SetPixelIndexed(x, y, x < 2 ? 0 : 1);
  CHECK(ind.GetPixel(0, 0) == RGB(0xFF, 0, 0) && ind.GetPixel(3, 3) == RGB(0, 0, 0xFF));
  CHECK(ind.SetTransparentColor(0) == -1 && ind.GetTransparentColor() == 0);
  CImage canvas;
  CHECK(canvas.Create(4, 4, 24));
  hdc = canvas.GetDC();
  RECT rc4 = {0, 0, 4, 4};
  ::FillRect(hdc, &rc4, (HBRUSH)::GetStockObject(WHITE_BRUSH));
  CHECK(ind.Draw(hdc, 0, 0));
  canvas.ReleaseDC();
  CHECK(canvas.GetPixel(0, 0) == RGB(0xFF, 0xFF, 0xFF));  // red was transparent
  CHECK(canvas.GetPixel(3, 0) == RGB(0, 0, 0xFF));
  CHECK(SUCCEEDED(ind.Save(szBmp)));
  CImage ind2;
  CHECK(SUCCEEDED(ind2.Load(szBmp)) && ind2.GetBPP() == 8);
  CHECK(ind2.GetPixel(3, 3) == RGB(0, 0, 0xFF));

  // Attach and Detach, and a bitmap that is not a DIB section.
  HBITMAP h = img.Detach();
  CHECK(img.IsNull() && h != NULL);
  img.Attach(h);
  CHECK(img.GetWidth() == 8 && img.GetPitch() == -32);
  HDC hScreen = ::GetDC(NULL);
  HBITMAP hddb = ::CreateCompatibleBitmap(hScreen, 3, 3);
  ::ReleaseDC(NULL, hScreen);
  CImage ddb;
  ddb.Attach(hddb);
  CHECK(!ddb.IsDIBSection() && ddb.GetWidth() == 3 && ddb.GetBits() == NULL);
  CHECK(SUCCEEDED(ddb.Save(szBmp)));

  // The file dialogs' filter strings.
  CString strFilter;
  CSimpleArray<GUID> guids;
  CHECK(SUCCEEDED(CImage::GetImporterFilterString(strFilter, guids, TEXT("All Image Files"))));
  CHECK(strFilter.Find(TEXT("All Image Files (")) == 0);
  CHECK(strFilter.Find(TEXT("*.PNG")) > 0 && strFilter.Right(2) == TEXT("||"));
  CHECK(guids.GetSize() > 4 && guids[0] == GUID_NULL);
  CString strSave;
  CSimpleArray<GUID> saveGuids;
  CHECK(SUCCEEDED(CImage::GetExporterFilterString(strSave, saveGuids, NULL,
                                                  CImage::excludeDefaultSave | CImage::excludeGIF)));
  CHECK(strSave.Find(TEXT("GIF")) < 0 && strSave.Find(TEXT("*.BMP")) >= 0);
  CHECK(saveGuids.Find(Gdiplus::ImageFormatPNG) >= 0);

  ::DeleteFile(szPng);
  ::DeleteFile(szBmp);
  ::DeleteFile(szJpg);
}

int main() {
  // Started by the service control manager: be the service.
  if (_tcsstr(::GetCommandLine(), TEXT("-AsService")) != NULL) return _AtlModule.WinMain(SW_HIDE);

  char szPath[MAX_PATH];
  DWORD n = ::GetEnvironmentVariableA("MFC_TEST_OUT", szPath, MAX_PATH);
  if (n == 0 || n >= MAX_PATH - 20) lstrcpyA(szPath, ".");
  lstrcatA(szPath, "/atlutiltest.log");
  g_pLog = fopen(szPath, "w");
  ::CoInitialize(NULL);

  TestStrings();
  Log("strings: %d failure(s) so far", g_nFailures);
  TestSimpleCollections();
  TestAtlArray();
  TestAtlList();
  TestAtlMap();
  TestRBMap();
  Log("collections: %d failure(s) so far", g_nFailures);
  TestMemory();
  TestSafeArray();
  TestDispatchDriver();
  Log("memory, safe arrays, dispatch: %d failure(s) so far", g_nFailures);
  TestFiles();
  Log("files: %d failure(s) so far", g_nFailures);
  TestTime();
  Log("times: %d failure(s) so far", g_nFailures);
  TestImage();
  Log("images: %d failure(s) so far", g_nFailures);
  TestPath();
  Log("paths: %d failure(s) so far", g_nFailures);
  TestSecurity();
  Log("security: %d failure(s) so far", g_nFailures);
  TestService();
  Log("service: %d failure(s) so far", g_nFailures);

  ::CoUninitialize();
  Log(g_nFailures ? "FAIL: %d failure(s)" : "PASS: %d failure(s)", g_nFailures);
  if (g_pLog != NULL) fclose(g_pLog);
  return g_nFailures ? 1 : 0;
}
