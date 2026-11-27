// atlperftest.cpp - atlperf.h's performance counters, with the
// application and the collecting side in one process:
//   - Register writes the service's Performance key and the names and
//     help to Perflib (at the indexes after Last Counter and Last Help);
//   - Initialize, CreateInstanceT, CreateInstanceByName (the same instance
//     twice), counters changed under CPerfLock;
//   - a second CPerfMon opening and collecting "Global", a list of
//     indexes, "Costly", too small a buffer (ERROR_MORE_DATA), and the
//     PERF_OBJECT_TYPE, counter definitions and instances it writes;
//   - ReleaseInstance, PERFREG_ENTRY with AtlPerfRegister and the
//     exported-style _AtlPerfOpen/_AtlPerfCollect/_AtlPerfClose;
//   - Unregister taking the strings out and giving the indexes back.
// Exits with 1 on a failure.
#include <atlbase.h>
#include <atlperf.h>
#include <stdio.h>
#include <string>
#include <vector>

static int g_nFailures;
#define CHECK(cond)                                  \
  do {                                               \
    if (!(cond)) {                                   \
      printf("FAIL line %d: %s\n", __LINE__, #cond); \
      g_nFailures++;                                 \
    }                                                \
  } while (0)

class CRequests : public ATL::CPerfObject {
public:
  DECLARE_PERF_CATEGORY(CRequests, 1, _T("Test Requests"), _T("Requests the test makes"), 0)
  BEGIN_COUNTER_MAP(CRequests)
    DEFINE_COUNTER(m_nCount, _T("Count"), _T("Requests so far"), PERF_COUNTER_RAWCOUNT, 0)
    DEFINE_COUNTER(m_nBytes, _T("Bytes"), _T("Bytes so far"), PERF_COUNTER_LARGE_RAWCOUNT, -3)
  END_COUNTER_MAP()
  ULONG m_nCount;
  ULONGLONG m_nBytes;
};

class CTotals : public ATL::CPerfObject {
public:
  DECLARE_PERF_CATEGORY_NO_INSTANCES(CTotals, 2, _T("Test Totals"), _T("Totals of the test"), -1)
  BEGIN_COUNTER_MAP(CTotals)
    DEFINE_COUNTER(m_nTotal, _T("Total"), _T("All of it"), PERF_COUNTER_RAWCOUNT, 0)
  END_COUNTER_MAP()
  ULONG m_nTotal;
};

class CTestPerfMon : public ATL::CPerfMon {
public:
  BEGIN_PERF_MAP(_T("ClangRetroAtlPerfTest"))
    CHAIN_PERF_CATEGORY(CRequests)
    CHAIN_PERF_CATEGORY(CTotals)
  END_PERF_MAP()
};

PERFREG_ENTRY(CTestPerfMon)

static const WCHAR kPerflibKey[] = L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Perflib";
static const WCHAR kPerfKey[] = L"SYSTEM\\CurrentControlSet\\Services\\ClangRetroAtlPerfTest\\Performance";

static DWORD GetDword(LPCWSTR szKey, LPCWSTR szValue, DWORD dwDefault) {
  HKEY h;
  DWORD dw = dwDefault, cb = sizeof(dw), type;
  if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, szKey, 0, KEY_READ, &h) != ERROR_SUCCESS) return dwDefault;
  if (RegQueryValueExW(h, szValue, NULL, &type, (BYTE*)&dw, &cb) != ERROR_SUCCESS || type != REG_DWORD) dw = dwDefault;
  RegCloseKey(h);
  return dw;
}
static std::wstring GetString(LPCWSTR szKey, LPCWSTR szValue) {
  HKEY h;
  WCHAR sz[MAX_PATH] = L"";
  DWORD cb = sizeof(sz) - sizeof(WCHAR), type;
  if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, szKey, 0, KEY_READ, &h) != ERROR_SUCCESS) return L"";
  if (RegQueryValueExW(h, szValue, NULL, &type, (BYTE*)sz, &cb) != ERROR_SUCCESS) sz[0] = 0;
  RegCloseKey(h);
  return sz;
}
// The string at an index in Perflib\009's Counter or Help.
static std::wstring PerflibString(LPCWSTR szValue, DWORD dwIndex) {
  std::wstring key = std::wstring(kPerflibKey) + L"\\009";
  HKEY h;
  if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, key.c_str(), 0, KEY_READ, &h) != ERROR_SUCCESS) return L"";
  DWORD cb = 0, type;
  std::wstring result;
  if (RegQueryValueExW(h, szValue, NULL, &type, NULL, &cb) == ERROR_SUCCESS && type == REG_MULTI_SZ) {
    std::vector<WCHAR> buf(cb / sizeof(WCHAR) + 2, 0);
    RegQueryValueExW(h, szValue, NULL, &type, (BYTE*)&buf[0], &cb);
    for (const WCHAR* p = &buf[0]; *p;) {
      DWORD n = (DWORD)wcstoul(p, NULL, 10);
      p += wcslen(p) + 1;
      if (*p == 0) break;
      if (n == dwIndex) result = p;
      p += wcslen(p) + 1;
    }
  }
  RegCloseKey(h);
  return result;
}

static const PERF_OBJECT_TYPE* FindObject(BYTE* pData, DWORD cb, DWORD nObjects, DWORD dwNameIndex) {
  BYTE* p = pData;
  for (DWORD i = 0; i < nObjects && p < pData + cb; i++) {
    const PERF_OBJECT_TYPE* pObj = (const PERF_OBJECT_TYPE*)p;
    if (pObj->ObjectNameTitleIndex == dwNameIndex) return pObj;
    p += pObj->TotalByteLength;
  }
  return NULL;
}

// Perflib\\009 that is a view of the performance text (later Windows, and
// Wine) rather than registry values: the strings go through lodctr there,
// and only the indexes can be checked.
static bool TextInRegistry() {
  std::wstring key = std::wstring(kPerflibKey) + L"\\009";
  HKEY h;
  if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, key.c_str(), 0, KEY_READ, &h) != ERROR_SUCCESS) return true;
  bool b = (LONG_PTR)h > 0;
  RegCloseKey(h);
  return b;
}

int main() {
  bool bText = TextInRegistry();
  if (!bText) printf("Perflib text is a view here: checking indexes only\n");
  DWORD dwLastBefore = GetDword(kPerflibKey, L"Last Counter", 1846);
  DWORD dwLastHelpBefore = GetDword(kPerflibKey, L"Last Help", 1847);

  CTestPerfMon app;
  // Register: the Performance key, then 5 names (2 categories, 3 counters).
  HRESULT hrReg = app.Register(_T("OpenPerfMon"), _T("CollectPerfMon"), _T("ClosePerfMon"));
  if (hrReg != S_OK) printf("Register: 0x%08lx\n", (unsigned long)hrReg);
  CHECK(hrReg == S_OK);
  CHECK(GetString(kPerfKey, L"Open") == L"OpenPerfMon");
  CHECK(GetString(kPerfKey, L"Collect") == L"CollectPerfMon");
  CHECK(GetString(kPerfKey, L"Close") == L"ClosePerfMon");
  CHECK(GetString(kPerfKey, L"Library").find(L"atlperftest") != std::wstring::npos);
  DWORD dwFirst = GetDword(kPerfKey, L"First Counter", 0);
  DWORD dwFirstHelp = GetDword(kPerfKey, L"First Help", 0);
  CHECK(dwFirst == dwLastBefore + 2);
  CHECK(dwFirstHelp == dwLastHelpBefore + 2);
  CHECK(GetDword(kPerfKey, L"Last Counter", 0) == dwFirst + 8);
  CHECK(GetDword(kPerfKey, L"Last Help", 0) == dwFirstHelp + 8);
  CHECK(GetDword(kPerflibKey, L"Last Counter", 0) == dwFirst + 8);
  CHECK(GetDword(kPerflibKey, L"Last Help", 0) == dwFirstHelp + 8);
  CHECK(!bText || PerflibString(L"Counter", dwFirst) == L"Test Requests");
  CHECK(!bText || PerflibString(L"Counter", dwFirst + 2) == L"Count");
  CHECK(!bText || PerflibString(L"Counter", dwFirst + 4) == L"Bytes");
  CHECK(!bText || PerflibString(L"Counter", dwFirst + 6) == L"Test Totals");
  CHECK(!bText || PerflibString(L"Counter", dwFirst + 8) == L"Total");
  CHECK(!bText || PerflibString(L"Help", dwFirstHelp + 2) == L"Requests so far");
  CHECK(!bText || PerflibString(L"Help", dwFirstHelp + 8) == L"All of it");

  // The application's instances.
  CHECK(app.Initialize() == S_OK);
  CRequests* pA = NULL;
  CRequests* pB = NULL;
  CRequests* pA2 = NULL;
  CTotals* pTotals = NULL;
  CHECK(app.CreateInstanceT(0, L"alpha", &pA) == S_OK && pA != NULL);
  CHECK(app.CreateInstanceByNameT(L"beta", &pB) == S_OK && pB != NULL);
  CHECK(app.CreateInstanceByNameT(L"alpha", &pA2) == S_OK && pA2 == pA);
  CHECK(app.CreateInstanceT(0, NULL, &pTotals) == S_OK && pTotals != NULL);
  if (pA == NULL || pB == NULL || pTotals == NULL) {
    printf("no instances\n");
    return 1;
  }
  CHECK(wcscmp(pA->GetInstanceName(), L"alpha") == 0);
  CHECK(pA->m_dwCategoryId == 1 && pTotals->m_dwCategoryId == 2);
  CHECK(pA->m_dwInstance != pB->m_dwInstance);
  {
    ATL::CPerfLock lock(&app);
    CHECK(SUCCEEDED(lock.GetStatus()));
    pA->m_nCount = 7;
    pA->m_nBytes = 0x100000005ULL;
    pB->m_nCount = 11;
    pB->m_nBytes = 3;
    pTotals->m_nTotal = 18;
  }

  // The collecting side, as the performance DLL would be.
  CTestPerfMon dll;
  CHECK(dll.Open(NULL) == ERROR_SUCCESS);
  CHECK(dll.GetCategoryNameIndex(1) == dwFirst);
  CHECK(dll.GetCategoryNameIndex(2) == dwFirst + 6);
  std::vector<BYTE> buf(65536);
  LPVOID pv = &buf[0];
  DWORD cb = (DWORD)buf.size(), nObjects = 0;
  WCHAR szGlobal[] = L"Global";
  CHECK(dll.Collect(szGlobal, &pv, &cb, &nObjects) == ERROR_SUCCESS);
  CHECK(nObjects == 2);
  CHECK((BYTE*)pv == &buf[0] + cb);
  const PERF_OBJECT_TYPE* pReq = FindObject(&buf[0], cb, nObjects, dwFirst);
  const PERF_OBJECT_TYPE* pTot = FindObject(&buf[0], cb, nObjects, dwFirst + 6);
  CHECK(pReq != NULL && pTot != NULL);
  if (pReq != NULL) {
    CHECK(pReq->ObjectHelpTitleIndex == dwFirstHelp);
    CHECK(pReq->NumCounters == 2);
    CHECK(pReq->NumInstances == 2);
    CHECK(pReq->DefaultCounter == 0);
    const PERF_COUNTER_DEFINITION* pDef = (const PERF_COUNTER_DEFINITION*)((BYTE*)pReq + pReq->HeaderLength);
    CHECK(pDef[0].CounterNameTitleIndex == dwFirst + 2);
    CHECK(pDef[0].CounterType == PERF_COUNTER_RAWCOUNT && pDef[0].CounterSize == 4);
    CHECK(pDef[1].CounterNameTitleIndex == dwFirst + 4);
    CHECK(pDef[1].CounterType == PERF_COUNTER_LARGE_RAWCOUNT && pDef[1].CounterSize == 8);
    CHECK(pDef[1].DefaultScale == -3);
    // Each instance: its definition and name, then its counter block.
    const BYTE* p = (const BYTE*)pReq + pReq->DefinitionLength;
    bool bSawAlpha = false, bSawBeta = false;
    for (LONG i = 0; i < pReq->NumInstances; i++) {
      const PERF_INSTANCE_DEFINITION* pInst = (const PERF_INSTANCE_DEFINITION*)p;
      LPCWSTR szName = (LPCWSTR)(p + pInst->NameOffset);
      const PERF_COUNTER_BLOCK* pBlock = (const PERF_COUNTER_BLOCK*)(p + pInst->ByteLength);
      ULONG nCount = *(const ULONG*)((const BYTE*)pBlock + pDef[0].CounterOffset);
      ULONGLONG nBytes;
      memcpy(&nBytes, (const BYTE*)pBlock + pDef[1].CounterOffset, 8);
      if (wcscmp(szName, L"alpha") == 0) {
        bSawAlpha = true;
        CHECK(nCount == 7 && nBytes == 0x100000005ULL);
      } else if (wcscmp(szName, L"beta") == 0) {
        bSawBeta = true;
        CHECK(nCount == 11 && nBytes == 3);
      }
      p += pInst->ByteLength + pBlock->ByteLength;
    }
    CHECK(bSawAlpha && bSawBeta);
    CHECK(p == (const BYTE*)pReq + pReq->TotalByteLength);
  }
  if (pTot != NULL) {
    CHECK(pTot->NumInstances == PERF_NO_INSTANCES);
    CHECK(pTot->NumCounters == 1);
    const PERF_COUNTER_DEFINITION* pDef = (const PERF_COUNTER_DEFINITION*)((BYTE*)pTot + pTot->HeaderLength);
    const PERF_COUNTER_BLOCK* pBlock = (const PERF_COUNTER_BLOCK*)((BYTE*)pTot + pTot->DefinitionLength);
    CHECK(*(const ULONG*)((const BYTE*)pBlock + pDef[0].CounterOffset) == 18);
  }

  // One category by its index; one that is not ours; costly; too small.
  WCHAR szList[32];
  swprintf(szList, 32, L"%lu 238", (unsigned long)(dwFirst + 6));
  pv = &buf[0];
  cb = (DWORD)buf.size();
  CHECK(dll.Collect(szList, &pv, &cb, &nObjects) == ERROR_SUCCESS);
  CHECK(nObjects == 1 && ((PERF_OBJECT_TYPE*)&buf[0])->ObjectNameTitleIndex == dwFirst + 6);
  swprintf(szList, 32, L"238");
  pv = &buf[0];
  cb = (DWORD)buf.size();
  CHECK(dll.Collect(szList, &pv, &cb, &nObjects) == ERROR_SUCCESS && nObjects == 0 && cb == 0);
  WCHAR szCostly[] = L"Costly";
  pv = &buf[0];
  cb = (DWORD)buf.size();
  CHECK(dll.Collect(szCostly, &pv, &cb, &nObjects) == ERROR_SUCCESS && nObjects == 0 && cb == 0);
  pv = &buf[0];
  cb = 64;
  CHECK(dll.Collect(szGlobal, &pv, &cb, &nObjects) == ERROR_MORE_DATA && cb == 0 && pv == &buf[0]);

  // Releasing: alpha was taken twice, so it stays once.
  CHECK(app.ReleaseInstance(pA2) == S_OK);
  CHECK(app.ReleaseInstance(pB) == S_OK);
  pv = &buf[0];
  cb = (DWORD)buf.size();
  CHECK(dll.Collect(szGlobal, &pv, &cb, &nObjects) == ERROR_SUCCESS);
  pReq = FindObject(&buf[0], cb, nObjects, dwFirst);
  CHECK(pReq != NULL && pReq->NumInstances == 1);
  CHECK(app.ReleaseInstance(pA) == S_OK);
  pv = &buf[0];
  cb = (DWORD)buf.size();
  CHECK(dll.Collect(szGlobal, &pv, &cb, &nObjects) == ERROR_SUCCESS);
  pReq = FindObject(&buf[0], cb, nObjects, dwFirst);
  CHECK(pReq != NULL && pReq->NumInstances == 0);
  CHECK(dll.Close() == ERROR_SUCCESS);

  // The module-wide entry points (PERFREG_ENTRY listed CTestPerfMon).
  CHECK(ATL::_AtlPerfOpen(NULL) == ERROR_SUCCESS);
  pv = &buf[0];
  cb = (DWORD)buf.size();
  CHECK(ATL::_AtlPerfCollect(szGlobal, &pv, &cb, &nObjects) == ERROR_SUCCESS && nObjects == 2);
  CHECK(ATL::_AtlPerfClose() == ERROR_SUCCESS);

  // Unregister: the strings go and Last Counter is as before.
  app.UnInitialize();
  CHECK(app.Unregister() == S_OK);
  CHECK(!bText || PerflibString(L"Counter", dwFirst).empty());
  CHECK(!bText || PerflibString(L"Help", dwFirstHelp + 8).empty());
  CHECK(GetDword(kPerflibKey, L"Last Counter", 1846) == dwLastBefore);
  CHECK(GetDword(kPerflibKey, L"Last Help", 1847) == dwLastHelpBefore);
  CHECK(GetString(kPerfKey, L"Open").empty());

  // AtlPerfRegister and AtlPerfUnregister do the same for the module.
  CHECK(ATL::AtlPerfRegister() == S_OK);
  CHECK(!bText || PerflibString(L"Counter", dwFirst) == L"Test Requests");
  CHECK(ATL::AtlPerfUnregister() == S_OK);
  CHECK(GetDword(kPerflibKey, L"Last Counter", 1846) == dwLastBefore);

  if (g_nFailures) {
    printf("%d failure(s)\n", g_nFailures);
    return 1;
  }
  printf("atlperftest: ok\n");
  return 0;
}
