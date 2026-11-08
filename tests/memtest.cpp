// memtest.cpp - console tests for the memory diagnostics in a debug
// build: DEBUG_NEW and object blocks, CMemoryState, dumps, guard bytes,
// AfxIsMemoryBlock, AfxDoForAllObjects, tracking off, the allocation hook
// and delayed frees.
#define _DEBUG
#include <afx.h>
#include <stdio.h>
#include <string>
#include <vector>

#define new DEBUG_NEW

static int g_failures = 0;
// Whether blocks other than objects are tracked (not with MinGW's libstdc++).
static BOOL g_bAll;
#define CHECK(cond)                                                   \
  do {                                                                \
    if (!(cond)) {                                                    \
      printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);          \
      g_failures++;                                                   \
    }                                                                 \
  } while (0)

class CPerson : public CObject {
  DECLARE_DYNAMIC(CPerson)
public:
  CString m_strName;
  int m_nAge;
  CPerson() : m_nAge(0) {}
  virtual void Dump(CDumpContext& dc) const {
    CObject::Dump(dc);
    dc << "name: " << m_strName << "\n";
  }
};
IMPLEMENT_DYNAMIC(CPerson, CObject)

// What afxDump writes, caught in a memory file.
class CCapture {
public:
  CMemFile m_file;
  CCapture() { afxDump.m_pFile = &m_file; }
  ~CCapture() { afxDump.m_pFile = NULL; }
  CStringA Text() {
    afxDump.Flush();
    ULONGLONG n = m_file.GetLength();
    CStringA str;
    m_file.SeekToBegin();
    m_file.Read(str.GetBuffer((int)n), (UINT)n);
    str.ReleaseBuffer((int)n);
    return str;
  }
};

static void CountObjects(CObject* pObject, void* pContext) {
  if (pObject->IsKindOf(RUNTIME_CLASS(CPerson))) ++*(int*)pContext;
}

static BOOL AFXAPI RefuseBig(size_t nSize, BOOL, LONG) { return nSize < 1000; }

static void TestState() {
  CMemoryState before, after, diff;
  before.Checkpoint();
  char* psz = new char[10];
  CPerson* pPerson = new CPerson;
  pPerson->m_strName = _T("Ada");
  after.Checkpoint();
  CHECK(diff.Difference(before, after));
  CHECK(diff.m_lCounts[CMemoryState::objectBlock] == 1);
  CHECK(diff.m_lSizes[CMemoryState::objectBlock] == (LONG)sizeof(CPerson));
  if (g_bAll) {
    CHECK(diff.m_lCounts[CMemoryState::bitBlock] == 1);
    CHECK(diff.m_lSizes[CMemoryState::bitBlock] == 10);
    CHECK(diff.m_lTotalCount == (LONG)(10 + sizeof(CPerson)));
    // The fill a new block gets.
    CHECK((BYTE)psz[0] == 0xCD && (BYTE)psz[9] == 0xCD);
  }
  memcpy(psz, "leak!", 6);

  {
    CCapture cap;
    diff.DumpStatistics();
    CStringA str = cap.Text();
    if (g_bAll) CHECK(str.Find("10 bytes in 1 Non-Object Blocks.") >= 0);
    CHECK(str.Find("in 1 Object Blocks.") >= 0);
  }
  {
    CCapture cap;
    afxDump.SetDepth(1);
    before.DumpAllObjectsSince();
    afxDump.SetDepth(0);
    CStringA str = cap.Text();
    printf("%s", (LPCSTR)str);
    CHECK(str.Find("Dumping objects ->") == 0);
    CHECK(str.Find("memtest.cpp(") >= 0);
    if (g_bAll) {
      CHECK(str.Find(" normal block at $") >= 0);
      CHECK(str.Find("10 bytes long.") >= 0);
      CHECK(str.Find("<leak!") >= 0);
    }
    CHECK(str.Find("a CPerson object at $") >= 0);
    CHECK(str.Find("name: Ada") >= 0);  // its Dump, at depth 1
    CHECK(str.Find("Object dump complete.") >= 0);
  }

  // The blocks themselves.
  if (g_bAll) CHECK(AfxIsMemoryBlock(psz, 10));
  CHECK(!AfxIsMemoryBlock(psz, 11));
  LONG lRequest = 0;
  CHECK(AfxIsMemoryBlock(pPerson, sizeof(CPerson), &lRequest) && lRequest > 0);
  int nLocal = 0;
  CHECK(!AfxIsMemoryBlock(&nLocal, sizeof(int)));
  int nPeople = 0;
  AfxDoForAllObjects(CountObjects, &nPeople);
  CHECK(nPeople == 1);

  // A write past the end shows in AfxCheckMemory.
  CHECK(AfxCheckMemory());
  {
    CCapture cap;
    BYTE* pEnd = (BYTE*)pPerson + sizeof(CPerson);
    BYTE save = *pEnd;
    *pEnd = 'x';
    CHECK(!AfxCheckMemory());
    *pEnd = save;
    CStringA str = cap.Text();
    CHECK(str.Find("DAMAGE: after client block") >= 0);
    CHECK(str.Find("memtest.cpp(") >= 0);
  }
  CHECK(AfxCheckMemory());

  delete[] psz;
  delete pPerson;
  after.Checkpoint();
  CHECK(!diff.Difference(before, after));
  CHECK(!AfxIsMemoryBlock(psz, 10));
}

static void TestUntracked() {
  // Plain allocations still come and go while blocks are tracked.
  CPerson* pKeep = new CPerson;
  std::vector<std::string> v;
  for (int i = 0; i < 1000; i++) v.push_back(std::string(40, (char)('a' + i % 26)));
  v.clear();
  v.shrink_to_fit();
  int* pn = ::new int[100];
  ::delete[] pn;
  CString str(_T("a string long enough to be on the heap, surely"));
  str += str;
  delete pKeep;

  // Tracking off: blocks are made but not counted.
  CMemoryState before, after, diff;
  before.Checkpoint();
  BOOL bOld = AfxEnableMemoryTracking(FALSE);
  CHECK(bOld);
  CPerson* p = new CPerson;
  CHECK(!AfxEnableMemoryTracking(bOld));
  after.Checkpoint();
  CHECK(!diff.Difference(before, after));
  delete p;
}

static void TestHook() {
  AFX_ALLOC_HOOK pfnOld = AfxSetAllocHook(RefuseBig);
  char* pSmall = new char[100];
  CHECK(pSmall != NULL);
  delete[] pSmall;
  BOOL bThrown = FALSE;
  try {
    char* pBig = new char[5000];
    delete[] pBig;
  } catch (CMemoryException* e) {
    bThrown = TRUE;
    e->Delete();
  }
  CHECK(bThrown);
  CHECK(AfxSetAllocHook(pfnOld) == RefuseBig);
}

static void TestDelayFree() {
  int nOld = afxMemDF;
  afxMemDF |= delayFreeMemDF;
  CMemoryState before, after, diff;
  before.Checkpoint();
  CPerson* pPerson = new CPerson;
  delete pPerson;
  BYTE* p = (BYTE*)pPerson;
  after.Checkpoint();
  CHECK(!diff.Difference(before, after));
  CHECK(diff.m_lCounts[CMemoryState::freeBlock] == 1);
  CHECK(p[0] == 0xDD);  // kept, filled
  CHECK(AfxCheckMemory());
  {
    CCapture cap;
    p[3] = 1;  // a write after the free
    CHECK(!AfxCheckMemory());
    p[3] = 0xDD;
    CHECK(cap.Text().Find("written to after it was freed") >= 0);
  }
  afxMemDF = nOld;
}

static void TestLeaks() {
  CHECK(!AfxDumpMemoryLeaks());
  CPerson* p = new CPerson;
  CHECK(AfxDumpMemoryLeaks());
  delete p;
  CHECK(!AfxDumpMemoryLeaks());
}

int main() {
  g_bAll = _AfxTracksAllBlocks();
  if (!g_bAll) printf("note: the C++ runtime's delete is linked; only objects are tracked\n");
  try {
    TestState();
    TestUntracked();
    TestHook();
    TestDelayFree();
    TestLeaks();
  } catch (CException* e) {
    printf("FAIL unexpected exception\n");
    e->Delete();
    g_failures++;
  }
  printf("%s (%d failures)\n", g_failures ? "FAIL" : "PASS", g_failures);
  return g_failures ? 1 : 0;
}
