// disptest.cpp - console tests for the automation helpers in afxdisp.h:
// dates, currency, variants, safe arrays and the dispatch driver.
#include <afx.h>
#include <afxdisp.h>
#include <stdio.h>
#include <math.h>

static int g_failures = 0;
#define CHECK(cond)                                                   \
  do {                                                                \
    if (!(cond)) {                                                    \
      printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);          \
      g_failures++;                                                   \
    }                                                                 \
  } while (0)

static const LCID lcidUS = MAKELCID(MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US), SORT_DEFAULT);

static void TestDateTime() {
  COleDateTime dt(1999, 12, 31, 23, 59, 58);
  CHECK(dt.GetStatus() == COleDateTime::valid);
  CHECK(dt.GetYear() == 1999 && dt.GetMonth() == 12 && dt.GetDay() == 31);
  CHECK(dt.GetHour() == 23 && dt.GetMinute() == 59 && dt.GetSecond() == 58);
  CHECK(dt.GetDayOfWeek() == 6);  // a Friday
  CHECK(dt.GetDayOfYear() == 365);
  CHECK(dt.Format(_T("%Y-%m-%d %H:%M:%S")) == _T("1999-12-31 23:59:58"));

  // The epoch, and a day count checked by hand: 1 January 2000 is day 36526.
  CHECK((DATE)COleDateTime(1899, 12, 30, 0, 0, 0) == 0.0);
  CHECK((DATE)COleDateTime(2000, 1, 1, 0, 0, 0) == 36526.0);
  CHECK((DATE)COleDateTime(2000, 1, 1, 12, 0, 0) == 36526.5);

  // Before the epoch the fraction still counts forward from midnight:
  // -1.25 is 29 December 1899 at 06:00.
  COleDateTime neg(-1.25);
  CHECK(neg.GetYear() == 1899 && neg.GetMonth() == 12 && neg.GetDay() == 29);
  CHECK(neg.GetHour() == 6 && neg.GetMinute() == 0);
  CHECK((DATE)COleDateTime(1899, 12, 29, 6, 0, 0) == -1.25);

  // Leap years.
  CHECK(COleDateTime(2000, 2, 29, 0, 0, 0).GetStatus() == COleDateTime::valid);
  COleDateTime bad;
  CHECK(bad.SetDate(1900, 2, 29) != 0);
  CHECK(bad.GetStatus() == COleDateTime::invalid);
  CHECK(bad.SetDateTime(2001, 13, 1, 0, 0, 0) != 0);
  CHECK(bad.SetDateTime(2001, 1, 1, 24, 0, 0) != 0);
  CHECK(bad.SetDateTime(2001, 1, 1, 0, 0, 0) == 0);
  CHECK(bad.GetStatus() == COleDateTime::valid);

  // Spans and arithmetic.
  COleDateTimeSpan span(1, 2, 3, 4);
  CHECK(span.GetDays() == 1 && span.GetHours() == 2);
  CHECK(span.GetMinutes() == 3 && span.GetSeconds() == 4);
  CHECK(span.Format(_T("%D %H:%M:%S")) == _T("1 02:03:04"));
  COleDateTime later = dt + COleDateTimeSpan(0, 0, 0, 3);
  CHECK(later.GetYear() == 2000 && later.GetMonth() == 1 && later.GetDay() == 1);
  CHECK(later.GetSecond() == 1);
  CHECK(later > dt && dt < later && dt != later);
  COleDateTimeSpan diff = later - dt;
  CHECK(fabs(diff.GetTotalSeconds() - 3.0) < 0.001);
  CHECK((neg + COleDateTimeSpan(1, 0, 0, 0)).GetDay() == 30);
  CHECK((neg + COleDateTimeSpan(1, 0, 0, 0)).GetHour() == 6);
  CHECK(fabs((COleDateTime(1899, 12, 30, 6, 0, 0) - neg).GetTotalDays() - 1.0) < 1e-9);
  COleDateTimeSpan negSpan(-1, -2, 0, 0);
  CHECK(negSpan.GetDays() == -1 && negSpan.GetHours() == -2);

  // System time and time_t.
  SYSTEMTIME st;
  CHECK(dt.GetAsSystemTime(st));
  CHECK(st.wYear == 1999 && st.wMonth == 12 && st.wDay == 31 && st.wSecond == 58);
  CHECK(st.wDayOfWeek == 5);
  CHECK(COleDateTime(st) == dt);
  CHECK(COleDateTime((time_t)0) .GetStatus() == COleDateTime::valid);
  COleDateTime now = COleDateTime::GetCurrentTime();
  CHECK(now.GetStatus() == COleDateTime::valid && now.GetYear() >= 2020);

  // Through a VARIANT and the locale functions.
  COleVariant var(dt);
  CHECK(var.vt == VT_DATE);
  CHECK(COleDateTime(var) == dt);
  CString str = dt.Format(0, lcidUS);
  CHECK(str.Find(_T("1999")) >= 0);
  COleDateTime parsed;
  CHECK(parsed.ParseDateTime(str, 0, lcidUS));
  CHECK(parsed == dt);
  CHECK(!parsed.ParseDateTime(_T("not a date"), 0, lcidUS));
  CHECK(parsed.GetStatus() == COleDateTime::invalid);

  COleDateTime nul;
  nul.SetStatus(COleDateTime::null);
  CHECK(nul.Format(_T("%Y")).IsEmpty());
}

static void TestCurrency() {
  COleCurrency a(12, 3400);  // 12.34
  CHECK(a.GetStatus() == COleCurrency::valid);
  CHECK(((CURRENCY)a).int64 == 123400);
  COleCurrency b(-1, -5000);  // -1.5
  CHECK(((CURRENCY)b).int64 == -15000);
  CHECK((a + b).m_cur.int64 == 108400);
  CHECK((a - b).m_cur.int64 == 138400);
  CHECK((a * 3).m_cur.int64 == 370200);
  CHECK((a / 4).m_cur.int64 == 30850);
  CHECK(-b == COleCurrency(1, 5000));
  CHECK(b < a && a > b && a != b);

  COleCurrency big;
  big.m_cur.int64 = 0x7fffffffffffffffLL;
  COleCurrency sum = big + a;
  CHECK(sum.GetStatus() == COleCurrency::invalid);
  CHECK((big * 2).GetStatus() == COleCurrency::invalid);

  CHECK(a.Format(0, lcidUS) == _T("12.34"));
  COleCurrency p;
  CHECK(p.ParseCurrency(_T("-7.25"), 0, lcidUS));
  CHECK(p.m_cur.int64 == -72500);
  CHECK(!p.ParseCurrency(_T("seven"), 0, lcidUS));
  CHECK(p.GetStatus() == COleCurrency::invalid);

  COleVariant v(a);
  CHECK(v.vt == VT_CY && v.cyVal.int64 == 123400);
  CHECK(COleCurrency(v) == a);
}

static void TestVariant() {
  COleVariant s(_T("hello"));
  CHECK(s.vt == VT_BSTR);
  CHECK(wcscmp(s.bstrVal, L"hello") == 0);
  COleVariant copy(s);
  CHECK(copy == s);
  CHECK(copy.bstrVal != s.bstrVal);

  COleVariant l(42L);
  CHECK(l.vt == VT_I4 && l.lVal == 42);
  COleVariant b((short)1, VT_BOOL);
  CHECK(b.vt == VT_BOOL && b.boolVal == VARIANT_TRUE);
  COleVariant d(2.5);
  CHECK(d.vt == VT_R8 && d.dblVal == 2.5);
  COleVariant by((BYTE)7);
  CHECK(by.vt == VT_UI1 && by.bVal == 7);
  CHECK(l != d);

  // Conversions.
  COleVariant c(l);
  c.ChangeType(VT_BSTR);
  CHECK(c.vt == VT_BSTR && wcscmp(c.bstrVal, L"42") == 0);
  c.ChangeType(VT_R8);
  CHECK(c.vt == VT_R8 && c.dblVal == 42.0);
  BOOL bThrew = FALSE;
  try {
    COleVariant x(_T("abc"));
    x.ChangeType(VT_I4);
  } catch (COleException* e) {
    bThrew = TRUE;
    CHECK(e->m_sc == DISP_E_TYPEMISMATCH);
    e->Delete();
  }
  CHECK(bThrew);

  // Assignment replaces the old value.
  c = _T("again");
  CHECK(c.vt == VT_BSTR && wcscmp(c.bstrVal, L"again") == 0);
  c = 3L;
  CHECK(c.vt == VT_I4 && c.lVal == 3);

  // Bytes as a safe array.
  CByteArray bytes;
  bytes.Add(1);
  bytes.Add(2);
  bytes.Add(3);
  COleVariant arr(bytes);
  CHECK(arr.vt == (VT_ARRAY | VT_UI1));
  CHECK(arr.parray->rgsabound[0].cElements == 3);
  CHECK(((BYTE*)arr.parray->pvData)[2] == 3);

  // Attach and Detach.
  VARIANT raw;
  ::VariantInit(&raw);
  raw.vt = VT_I2;
  raw.iVal = -5;
  COleVariant att;
  att.Attach(raw);
  CHECK(att.vt == VT_I2 && att.iVal == -5);
  CHECK(raw.vt == VT_EMPTY);
  VARIANT out = att.Detach();
  CHECK(out.vt == VT_I2 && att.vt == VT_EMPTY);

  // Through an archive.
  CMemFile file;
  {
    CArchive ar(&file, CArchive::store);
    ar << s << l << d << arr << COleCurrency(1, 0) << COleDateTime(2001, 2, 3, 4, 5, 6)
       << COleDateTimeSpan(1.5);
  }
  file.SeekToBegin();
  {
    CArchive ar(&file, CArchive::load);
    COleVariant s2, l2, d2, arr2;
    COleCurrency cy;
    COleDateTime dt;
    COleDateTimeSpan span;
    ar >> s2 >> l2 >> d2 >> arr2 >> cy >> dt >> span;
    CHECK(s2 == s);
    CHECK(l2 == l);
    CHECK(d2 == d);
    CHECK(arr2.vt == (VT_ARRAY | VT_UI1) && arr2.parray->rgsabound[0].cElements == 3);
    CHECK(((BYTE*)arr2.parray->pvData)[1] == 2);
    CHECK(cy == COleCurrency(1, 0));
    CHECK(dt == COleDateTime(2001, 2, 3, 4, 5, 6));
    CHECK(span.GetTotalDays() == 1.5);
  }
}

static void TestSafeArray() {
  COleSafeArray sa;
  LONG data[4] = {10, 20, 30, 40};
  sa.CreateOneDim(VT_I4, 4, data);
  CHECK(sa.vt == (VT_ARRAY | VT_I4));
  CHECK(sa.GetDim() == 1);
  CHECK(sa.GetOneDimSize() == 4);
  CHECK(sa.GetElemSize() == sizeof(LONG));
  LONG lo = -1, hi = -1;
  sa.GetLBound(1, &lo);
  sa.GetUBound(1, &hi);
  CHECK(lo == 0 && hi == 3);
  LONG idx = 2, val = 0;
  sa.GetElement(&idx, &val);
  CHECK(val == 30);
  val = 99;
  sa.PutElement(&idx, &val);
  val = 0;
  sa.GetElement(&idx, &val);
  CHECK(val == 99);
  sa.ResizeOneDim(6);
  CHECK(sa.GetOneDimSize() == 6);
  idx = 5;
  val = 123;
  sa.PutElement(&idx, &val);
  LONG* p = NULL;
  sa.AccessData((void**)&p);
  CHECK(p[0] == 10 && p[2] == 99 && p[4] == 0 && p[5] == 123);
  sa.UnaccessData();

  COleSafeArray copy(sa);
  CHECK(copy.parray != sa.parray);
  CHECK(copy.GetOneDimSize() == 6);

  BOOL bThrew = FALSE;
  try {
    idx = 50;
    sa.GetElement(&idx, &val);
  } catch (COleException* e) {
    bThrew = TRUE;
    CHECK(e->m_sc == DISP_E_BADINDEX);
    e->Delete();
  }
  CHECK(bThrew);

  // Two dimensions.
  COleSafeArray grid;
  DWORD dims[2] = {3, 2};
  grid.Create(VT_R8, 2, dims);
  CHECK(grid.GetDim() == 2);
  LONG ij[2] = {2, 1};
  double dv = 1.25;
  grid.PutElement(ij, &dv);
  dv = 0;
  grid.GetElement(ij, &dv);
  CHECK(dv == 1.25);
}

// ---------------------------------------------------------------------
// A small IDispatch that records what it was called with.

enum { TID_ADD = 1, TID_VALUE = 2, TID_BUMP = 3, TID_FAIL = 4, TID_NAME = 5 };

class CTestDispatch : public IDispatch {
public:
  ULONG m_cRef = 1;
  long m_lValue = 0;
  UINT m_cArgs = 0;
  VARTYPE m_vt[4] = {};
  WORD m_wFlags = 0;
  DISPID m_namedArg = 0;

  STDMETHOD(QueryInterface)(REFIID riid, void** ppv) {
    if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_IDispatch)) {
      *ppv = this;
      AddRef();
      return S_OK;
    }
    *ppv = NULL;
    return E_NOINTERFACE;
  }
  STDMETHOD_(ULONG, AddRef)() { return ++m_cRef; }
  STDMETHOD_(ULONG, Release)() { return --m_cRef; }
  STDMETHOD(GetTypeInfoCount)(UINT* pctinfo) { *pctinfo = 0; return S_OK; }
  STDMETHOD(GetTypeInfo)(UINT, LCID, ITypeInfo**) { return E_NOTIMPL; }
  STDMETHOD(GetIDsOfNames)(REFIID, LPOLESTR*, UINT, LCID, DISPID*) { return E_NOTIMPL; }
  STDMETHOD(Invoke)(DISPID id, REFIID, LCID, WORD wFlags, DISPPARAMS* pdp, VARIANT* pResult,
                    EXCEPINFO* pExcep, UINT*) {
    m_wFlags = wFlags;
    m_cArgs = pdp->cArgs;
    m_namedArg = pdp->cNamedArgs ? pdp->rgdispidNamedArgs[0] : 0;
    // The arguments arrive last first.
    for (UINT i = 0; i < pdp->cArgs && i < 4; i++) m_vt[i] = pdp->rgvarg[pdp->cArgs - 1 - i].vt;
    VARIANT* args = pdp->rgvarg;
    switch (id) {
    case TID_ADD: {  // Add(long, double, BSTR) -> BSTR "<long+double>:<str>"
      if (pdp->cArgs != 3) return DISP_E_BADPARAMCOUNT;
      wchar_t buf[128];
      swprintf(buf, 128, L"%g:%ls", args[2].lVal + args[1].dblVal, args[0].bstrVal);
      if (pResult) {
        pResult->vt = VT_BSTR;
        pResult->bstrVal = ::SysAllocString(buf);
      }
      return S_OK;
    }
    case TID_VALUE:
      if (wFlags & DISPATCH_PROPERTYPUT) {
        VARIANT v;
        ::VariantInit(&v);
        if (FAILED(::VariantChangeType(&v, &args[0], 0, VT_I4))) return DISP_E_TYPEMISMATCH;
        m_lValue = v.lVal;
        return S_OK;
      }
      if (pResult) {
        pResult->vt = VT_I4;
        pResult->lVal = m_lValue;
      }
      return S_OK;
    case TID_BUMP:  // Bump(long*) -> BOOL; adds one through the pointer
      if (args[0].vt != (VT_BYREF | VT_I4)) return DISP_E_TYPEMISMATCH;
      (*args[0].plVal)++;
      if (pResult) {
        pResult->vt = VT_BOOL;
        pResult->boolVal = VARIANT_TRUE;
      }
      return S_OK;
    case TID_FAIL:
      if (pExcep) {
        memset(pExcep, 0, sizeof(*pExcep));
        pExcep->wCode = 1234;
        pExcep->bstrSource = ::SysAllocString(L"TestSource");
        pExcep->bstrDescription = ::SysAllocString(L"Something broke");
      }
      return DISP_E_EXCEPTION;
    case TID_NAME:  // a string property
      if (pResult) {
        pResult->vt = VT_BSTR;
        pResult->bstrVal = ::SysAllocString(L"Tester");
      }
      return S_OK;
    }
    return DISP_E_MEMBERNOTFOUND;
  }
};

static void TestDispatchDriver() {
  CTestDispatch obj;
  {
    COleDispatchDriver drv(&obj, FALSE);
    CHECK((LPDISPATCH)drv == &obj);

    static const BYTE parmsAdd[] = VTS_I4 VTS_R8 VTS_BSTR;
    CString result;
    drv.InvokeHelper(TID_ADD, DISPATCH_METHOD, VT_BSTR, &result, parmsAdd, 2L, 0.5,
                     _T("x"));
    CHECK(result == _T("2.5:x"));
    CHECK(obj.m_cArgs == 3);
    CHECK(obj.m_vt[0] == VT_I4 && obj.m_vt[1] == VT_R8 && obj.m_vt[2] == VT_BSTR);
    CHECK(obj.m_wFlags == DISPATCH_METHOD);

    drv.SetProperty(TID_VALUE, VT_I4, 77L);
    CHECK(obj.m_lValue == 77);
    CHECK(obj.m_wFlags == DISPATCH_PROPERTYPUT);
    CHECK(obj.m_namedArg == DISPID_PROPERTYPUT);
    long lValue = 0;
    drv.GetProperty(TID_VALUE, VT_I4, &lValue);
    CHECK(lValue == 77);
    CHECK(obj.m_wFlags == DISPATCH_PROPERTYGET);

    // A variant argument goes by pointer and is converted by the object.
    static const BYTE parmsVar[] = VTS_VARIANT;
    COleVariant v(_T("31"));
    drv.InvokeHelper(TID_VALUE, DISPATCH_PROPERTYPUT, VT_EMPTY, NULL, parmsVar,
                     (const VARIANT*)&v);
    CHECK(obj.m_lValue == 31);
    CHECK(obj.m_vt[0] == VT_BSTR);

    static const BYTE parmsBump[] = VTS_PI4;
    long n = 5;
    BOOL bResult = FALSE;
    drv.InvokeHelper(TID_BUMP, DISPATCH_METHOD, VT_BOOL, &bResult, parmsBump, &n);
    CHECK(n == 6);
    CHECK(bResult == TRUE);

    CString strName;
    drv.GetProperty(TID_NAME, VT_BSTR, &strName);
    CHECK(strName == _T("Tester"));

    BOOL bThrew = FALSE;
    try {
      drv.InvokeHelper(TID_FAIL, DISPATCH_METHOD, VT_EMPTY, NULL, NULL);
    } catch (COleDispatchException* e) {
      bThrew = TRUE;
      CHECK(e->m_wCode == 1234);
      CHECK(e->m_strSource == _T("TestSource"));
      CHECK(e->m_strDescription == _T("Something broke"));
      TCHAR sz[64];
      CHECK(e->GetErrorMessage(sz, 64));
      CHECK(CString(sz) == _T("Something broke"));
      e->Delete();
    }
    CHECK(bThrew);

    bThrew = FALSE;
    try {
      drv.InvokeHelper(99, DISPATCH_METHOD, VT_EMPTY, NULL, NULL);
    } catch (COleException* e) {
      bThrew = TRUE;
      CHECK(e->m_sc == DISP_E_MEMBERNOTFOUND);
      e->Delete();
    }
    CHECK(bThrew);
  }
  // Without auto-release the driver leaves the reference alone.
  CHECK(obj.m_cRef == 1);
  {
    obj.AddRef();
    COleDispatchDriver drv(&obj);
    COleDispatchDriver copy(drv);  // copies add a reference
    CHECK(obj.m_cRef == 3);
  }
  CHECK(obj.m_cRef == 1);

  // A failed CreateDispatch reports the error.
  COleDispatchDriver none;
  COleException err;
  CHECK(!none.CreateDispatch(_T("No.Such.Class"), &err));
  CHECK(FAILED(err.m_sc));
  CHECK(none.m_lpDispatch == NULL);
}

int main() {
  AfxOleInit();
  TestDateTime();
  TestCurrency();
  TestVariant();
  TestSafeArray();
  TestDispatchDriver();
  if (g_failures == 0) printf("PASS: all automation tests\n");
  else printf("%d failure(s)\n", g_failures);
  return g_failures ? 1 : 0;
}
