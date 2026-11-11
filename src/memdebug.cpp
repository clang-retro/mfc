// memdebug.cpp - the memory diagnostics: tracked blocks for DEBUG_NEW and
// for objects made in debug builds, CMemoryState, AfxCheckMemory and the
// leak report at exit.
//
// A tracked block is allocated with a header in front of it, which links
// it into a list in allocation order, and with guard bytes on both sides.
// The program's blocks are freed through the global operator delete, so
// the library replaces it: a block it tracks is unlinked and checked, any
// other pointer goes to free(), as the C++ runtime's own delete does.
// While nothing is tracked, delete costs one comparison more.
//
// In mfc.dll that replacement covers the DLL's own code only. Each module
// that uses the DLL gets another from libmfcs.a (dllstub.cpp), which sends
// its deletes here (_AfxDllDelete), and says when its runtime's delete won
// (_AfxNoteForeignDelete). All three C runtimes the library is built with
// (msvcrt.dll, and picolibc on the process heap) let one module free
// what another allocated, so untracked blocks need nothing more.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#define _AFX_MEMDEBUG_IMPL
#include "stdafx.h"
#include <new>

namespace {

const BYTE kGuardFill = 0xFD;  // the bytes on either side of a block
const BYTE kNewFill = 0xCD;    // a new block's contents
const BYTE kFreeFill = 0xDD;   // a freed block kept by delayFreeMemDF
const size_t kGuard = 4;

struct Block {
  Block* pNext;  // allocation order: pNext is newer
  Block* pPrev;
  LPCSTR lpszFileName;
  int nLine;
  int nUse;  // CMemoryState::blockUsage
  size_t nSize;
  LONG lRequest;
};

// The header and the leading guard, rounded so the data stays aligned.
const size_t kHead = (sizeof(Block) + kGuard + 15) & ~(size_t)15;

inline BYTE* Data(Block* p) { return (BYTE*)p + kHead; }
inline Block* FromData(const void* pv) { return (Block*)((BYTE*)pv - kHead); }

// The state, set up on first use: allocations can come from static
// constructors in any order.
struct State {
  CRITICAL_SECTION cs;
  Block* pFirst;
  Block* pLast;
  LONG lRequest;
  LONG lCounts[CMemoryState::nBlockUseMax];
  LONG lSizes[CMemoryState::nBlockUseMax];
  LONG lHighWater;
  LONG lTotal;
  BOOL bTrack;
  AFX_ALLOC_HOOK pfnHook;
  // The user pointers of the live blocks, for delete to look up: open
  // addressing, kept at most half full.
  void** ppTable;
  size_t nTable;
  size_t nInTable;
};

State* g_pState;
volatile LONG g_nInit;  // 0, 1 while being set up, 2 when ready
volatile LONG g_nTracked;  // blocks in the table; delete's fast path

State* GetState() {
  if (g_nInit == 2) return g_pState;
  if (::InterlockedCompareExchange(&g_nInit, 1, 0) == 0) {
    State* p = (State*)::calloc(1, sizeof(State));
    ::InitializeCriticalSection(&p->cs);
    p->bTrack = TRUE;
    g_pState = p;
    ::InterlockedExchange(&g_nInit, 2);
  } else {
    while (g_nInit != 2) ::Sleep(0);
  }
  return g_pState;
}

class Lock {
public:
  explicit Lock(State* p) : m_p(p) { ::EnterCriticalSection(&p->cs); }
  ~Lock() { ::LeaveCriticalSection(&m_p->cs); }

private:
  State* m_p;
};

size_t Hash(const void* pv, size_t n) { return (((size_t)pv) >> 4) * 2654435761u & (n - 1); }

void TableInsert(State* s, void* pv);

void TableGrow(State* s) {
  void** ppOld = s->ppTable;
  size_t nOld = s->nTable;
  s->nTable = nOld ? nOld * 2 : 256;
  s->ppTable = (void**)::calloc(s->nTable, sizeof(void*));
  s->nInTable = 0;
  for (size_t i = 0; i < nOld; i++)
    if (ppOld[i] != NULL && ppOld[i] != (void*)1) TableInsert(s, ppOld[i]);
  ::free(ppOld);
}

void TableInsert(State* s, void* pv) {
  if ((s->nInTable + 1) * 2 > s->nTable) TableGrow(s);
  size_t i = Hash(pv, s->nTable);
  while (s->ppTable[i] != NULL && s->ppTable[i] != (void*)1) i = (i + 1) & (s->nTable - 1);
  s->ppTable[i] = pv;
  s->nInTable++;
}

// The slot holding pv, or -1.
ptrdiff_t TableFind(State* s, const void* pv) {
  if (s->nTable == 0) return -1;
  size_t i = Hash(pv, s->nTable);
  while (s->ppTable[i] != NULL) {
    if (s->ppTable[i] == pv) return (ptrdiff_t)i;
    i = (i + 1) & (s->nTable - 1);
  }
  return -1;
}

void TableRemove(State* s, ptrdiff_t i) {
  s->ppTable[i] = (void*)1;  // a tombstone keeps later entries reachable
  s->nInTable--;
}

LPCSTR UseName(int nUse) {
  switch (nUse) {
    case CMemoryState::objectBlock: return "client";
    case CMemoryState::freeBlock: return "free";
    case CMemoryState::ignoredBlock: return "ignored";
    default: return "normal";
  }
}

void Output(CDumpContext& dc, LPCSTR lpsz) { dc << lpsz; }

// Reports a block whose guard bytes were overwritten. Returns FALSE then.
BOOL CheckBlock(CDumpContext& dc, Block* p) {
  BOOL bOK = TRUE;
  char sz[256];
  BYTE* pData = Data(p);
  for (size_t i = 0; i < kGuard; i++) {
    if (pData[-(ptrdiff_t)kGuard + (ptrdiff_t)i] != kGuardFill) {
      wsprintfA(sz, "DAMAGE: before %s block (#%ld) at $%p.\n", UseName(p->nUse), p->lRequest,
                pData);
      Output(dc, sz);
      bOK = FALSE;
      break;
    }
  }
  for (size_t i = 0; i < kGuard; i++) {
    if (pData[p->nSize + i] != kGuardFill) {
      wsprintfA(sz, "DAMAGE: after %s block (#%ld) at $%p.\n", UseName(p->nUse), p->lRequest,
                pData);
      Output(dc, sz);
      bOK = FALSE;
      break;
    }
  }
  if (p->nUse == CMemoryState::freeBlock) {
    for (size_t i = 0; i < p->nSize; i++) {
      if (pData[i] != kFreeFill) {
        wsprintfA(sz, "DAMAGE: free block (#%ld) at $%p was written to after it was freed.\n",
                  p->lRequest, pData);
        Output(dc, sz);
        bOK = FALSE;
        break;
      }
    }
  }
  if (!bOK && p->lpszFileName != NULL) {
    wsprintfA(sz, "%s(%d) : the block was allocated here.\n", p->lpszFileName, p->nLine);
    Output(dc, sz);
  }
  return bOK;
}

BOOL CheckAll(State* s) {
  BOOL bOK = TRUE;
  for (Block* p = s->pFirst; p != NULL; p = p->pNext)
    if (!CheckBlock(afxDump, p)) bOK = FALSE;
  return bOK;
}

// One block in a dump, as Visual C++ shows it.
void DumpBlock(CDumpContext& dc, Block* p) {
  char sz[512];
  BYTE* pData = Data(p);
  if (p->lpszFileName != NULL) {
    wsprintfA(sz, "%s(%d) : ", p->lpszFileName, p->nLine);
    Output(dc, sz);
  }
  if (p->nUse == CMemoryState::objectBlock) {
    CObject* pObject = (CObject*)pData;
    CRuntimeClass* pClass = pObject->GetRuntimeClass();
    wsprintfA(sz, "{%ld} a %s object at $%p, %lu bytes long\n", p->lRequest,
              pClass != NULL ? pClass->m_lpszClassName : "CObject", pData,
              (unsigned long)p->nSize);
    Output(dc, sz);
    if (dc.GetDepth() > 0) pObject->Dump(dc);
    return;
  }
  wsprintfA(sz, "{%ld} %s block at $%p, %lu bytes long.\n Data: <", p->lRequest,
            UseName(p->nUse), pData, (unsigned long)p->nSize);
  Output(dc, sz);
  size_t n = p->nSize < 16 ? p->nSize : 16;
  char szText[20], szHex[64];
  char* pHex = szHex;
  for (size_t i = 0; i < n; i++) {
    BYTE b = pData[i];
    szText[i] = (b >= 0x20 && b < 0x7F) ? (char)b : ' ';
    pHex += wsprintfA(pHex, "%02X ", b);
  }
  szText[n] = 0;
  *pHex = 0;
  wsprintfA(sz, "%s> %s\n", szText, szHex);
  Output(dc, sz);
}

// The blocks allocated after lRequest that are still in use.
void DumpSince(CDumpContext& dc, State* s, LONG lRequest) {
  Output(dc, "Dumping objects ->\n");
  for (Block* p = s->pFirst; p != NULL; p = p->pNext) {
    if (p->lRequest <= lRequest) continue;
    if (p->nUse == CMemoryState::freeBlock || p->nUse == CMemoryState::ignoredBlock) continue;
    DumpBlock(dc, p);
  }
  Output(dc, "Object dump complete.\n");
}

void Unlink(State* s, Block* p) {
  if (p->pPrev) p->pPrev->pNext = p->pNext;
  else s->pFirst = p->pNext;
  if (p->pNext) p->pNext->pPrev = p->pPrev;
  else s->pLast = p->pPrev;
  s->lCounts[p->nUse]--;
  s->lSizes[p->nUse] -= (LONG)p->nSize;
}

// Frees a block that the table holds at slot i.
void FreeTracked(State* s, ptrdiff_t i, void* pv) {
  Block* p = FromData(pv);
  if (afxMemDF & checkAlwaysMemDF) CheckAll(s);
  CheckBlock(afxDump, p);
  if (afxMemDF & delayFreeMemDF) {
    // Kept, filled, so a write after the free shows in AfxCheckMemory.
    s->lCounts[p->nUse]--;
    s->lSizes[p->nUse] -= (LONG)p->nSize;
    p->nUse = CMemoryState::freeBlock;
    s->lCounts[p->nUse]++;
    s->lSizes[p->nUse] += (LONG)p->nSize;
    memset(pv, kFreeFill, p->nSize);
    TableRemove(s, i);
    ::InterlockedDecrement(&g_nTracked);
    return;
  }
  Unlink(s, p);
  TableRemove(s, i);
  ::InterlockedDecrement(&g_nTracked);
  memset(p, kFreeFill, kHead + p->nSize + kGuard);
  ::free(p);
}

// The leak report, when a debug program ends: after the other static
// objects are gone, since this one is made before them.
struct LeakReport {
  ~LeakReport() {
    if (g_nInit != 2) return;
    AfxDumpMemoryLeaks();
  }
};
__attribute__((init_priority(101))) LeakReport g_leakReport;

}  // namespace

int afxMemDF = allocMemDF;

static BOOL OwnDelete();

void* AFXAPI AfxAllocMemoryDebug(size_t nSize, BOOL bIsObject, LPCSTR lpszFileName, int nLine) {
  State* s = GetState();
  Lock lock(s);
  LONG lRequest = s->lRequest + 1;
  if (s->pfnHook != NULL && !s->pfnHook(nSize, bIsObject, lRequest)) return NULL;
  // Without this file's delete, only objects can be tracked: other blocks
  // are plain ones, for the runtime's delete to free.
  if (!bIsObject && !OwnDelete()) return ::malloc(nSize ? nSize : 1);
  if (nSize > (size_t)-1 - kHead - kGuard) return NULL;
  Block* p = (Block*)::malloc(kHead + nSize + kGuard);
  if (p == NULL) return NULL;
  if (afxMemDF & checkAlwaysMemDF) CheckAll(s);
  s->lRequest = lRequest;
  p->lRequest = lRequest;
  p->lpszFileName = lpszFileName;
  p->nLine = nLine;
  p->nSize = nSize;
  BOOL bTrack = s->bTrack && (afxMemDF & allocMemDF) != 0;
  p->nUse = !bTrack ? CMemoryState::ignoredBlock
                    : bIsObject ? CMemoryState::objectBlock : CMemoryState::bitBlock;
  BYTE* pData = Data(p);
  memset(pData - kGuard, kGuardFill, kGuard);
  memset(pData, kNewFill, nSize);
  memset(pData + nSize, kGuardFill, kGuard);
  p->pNext = NULL;
  p->pPrev = s->pLast;
  if (s->pLast) s->pLast->pNext = p;
  else s->pFirst = p;
  s->pLast = p;
  s->lCounts[p->nUse]++;
  s->lSizes[p->nUse] += (LONG)nSize;
  if (p->nUse != CMemoryState::ignoredBlock) {
    s->lTotal += (LONG)nSize;
    LONG lInUse = s->lSizes[CMemoryState::objectBlock] + s->lSizes[CMemoryState::bitBlock];
    if (lInUse > s->lHighWater) s->lHighWater = lInUse;
  }
  TableInsert(s, pData);
  ::InterlockedIncrement(&g_nTracked);
  return pData;
}

void AFXAPI AfxFreeMemoryDebug(void* pbData, BOOL) { _AfxDebugDelete(pbData); }

void* AFXAPI _AfxDebugNew(size_t nSize, BOOL bIsObject, LPCSTR lpszFileName, int nLine) {
  void* p = AfxAllocMemoryDebug(nSize, bIsObject, lpszFileName, nLine);
  if (p == NULL) AfxThrowMemoryException();
  return p;
}

void AFXAPI _AfxDebugDelete(void* p);

// DEBUG_NEW.
void* AFX_CDECL operator new(size_t nSize, LPCSTR lpszFileName, int nLine) {
  return _AfxDebugNew(nSize, FALSE, lpszFileName, nLine);
}
void* AFX_CDECL operator new[](size_t nSize, LPCSTR lpszFileName, int nLine) {
  return _AfxDebugNew(nSize, FALSE, lpszFileName, nLine);
}
void AFX_CDECL operator delete(void* p, LPCSTR, int) { _AfxDebugDelete(p); }
void AFX_CDECL operator delete[](void* p, LPCSTR, int) { _AfxDebugDelete(p); }

// A block the library may track: unlinked and freed when it does,
// handed to the runtime's delete when not.
static BOOL FreeIfTracked(void* p) {
  if (g_nTracked == 0) return FALSE;
  State* s = GetState();
  Lock lock(s);
  ptrdiff_t i = TableFind(s, p);
  if (i < 0) return FALSE;
  FreeTracked(s, i, p);
  return TRUE;
}

void AFXAPI _AfxDebugDelete(void* p) {
  if (p == NULL || FreeIfTracked(p)) return;
  ::operator delete(p);
}

// The replacement of the global delete, so that a block from DEBUG_NEW
// is freed by delete. Any other pointer came from the runtime's
// operator new, which uses malloc. libc++ makes its own definitions weak,
// so these replace them. libstdc++'s are not weak, and when they are
// linked first (MinGW, with the library as a default library) these must
// give way: they are weak there, and then only objects are tracked,
// since CObject's operator delete comes here itself.
#ifdef _LIBCPP_VERSION
#define _AFX_DELETE_LINKAGE
#else
#define _AFX_DELETE_LINKAGE __attribute__((weak))
#endif

static void* volatile g_pProbe;  // OwnDelete's test pointer

static void GlobalDelete(void* p) noexcept {
  if (p == NULL) return;
  if (p == g_pProbe) g_pProbe = NULL;  // this delete is the one linked
  if (FreeIfTracked(p)) return;
  ::free(p);
}

_AFX_DELETE_LINKAGE void operator delete(void* p) noexcept { GlobalDelete(p); }
_AFX_DELETE_LINKAGE void operator delete[](void* p) noexcept { GlobalDelete(p); }
_AFX_DELETE_LINKAGE void operator delete(void* p, size_t) noexcept { GlobalDelete(p); }
_AFX_DELETE_LINKAGE void operator delete[](void* p, size_t) noexcept { GlobalDelete(p); }
_AFX_DELETE_LINKAGE void operator delete(void* p, const std::nothrow_t&) noexcept {
  GlobalDelete(p);
}
_AFX_DELETE_LINKAGE void operator delete[](void* p, const std::nothrow_t&) noexcept {
  GlobalDelete(p);
}

static volatile LONG g_bForeignDelete;  // a module's runtime delete won over the library's

// Whether the global delete is this file's: one freed through it says.
static BOOL OwnDelete() {
  static volatile LONG nOwn = -1;
  if (nOwn < 0) {
    void* p = ::malloc(1);
    g_pProbe = p;
    ::operator delete(p);  // the runtime's frees with free() as well
    nOwn = g_pProbe == NULL;
    g_pProbe = NULL;
  }
  return nOwn && !g_bForeignDelete;
}

#ifdef _AFX_BUILDING_DLL
extern "C" void AFXAPI _AfxDllDelete(void* p) {
  if (p == NULL || FreeIfTracked(p)) return;
  ::free(p);
}

extern "C" void AFXAPI _AfxNoteForeignDelete() { g_bForeignDelete = TRUE; }
#endif

BOOL AFXAPI _AfxTracksAllBlocks() { return OwnDelete(); }

AFX_ALLOC_HOOK AFXAPI AfxSetAllocHook(AFX_ALLOC_HOOK pfnAllocHook) {
  State* s = GetState();
  Lock lock(s);
  AFX_ALLOC_HOOK pfnOld = s->pfnHook;
  s->pfnHook = pfnAllocHook;
  return pfnOld;
}

BOOL AFXAPI AfxCheckMemory() {
  State* s = GetState();
  Lock lock(s);
  return CheckAll(s);
}

BOOL AFXAPI AfxIsMemoryBlock(const void* pv, UINT nBytes, LONG* plRequestNumber) {
  if (plRequestNumber != NULL) *plRequestNumber = 0;
  if (pv == NULL || g_nTracked == 0) return FALSE;
  State* s = GetState();
  Lock lock(s);
  if (TableFind(s, pv) < 0) return FALSE;
  Block* p = FromData(pv);
  if (plRequestNumber != NULL) *plRequestNumber = p->lRequest;
  return p->nSize == nBytes;
}

BOOL AFXAPI AfxEnableMemoryTracking(BOOL bTrack) {
  State* s = GetState();
  Lock lock(s);
  BOOL bOld = s->bTrack;
  s->bTrack = bTrack;
  return bOld;
}

void AFXAPI AfxDoForAllObjects(void(AFX_CDECL* pfn)(CObject* pObject, void* pContext),
                               void* pContext) {
  State* s = GetState();
  // The list is copied first, so the callback may allocate.
  CPtrArray objects;
  {
    Lock lock(s);
    for (Block* p = s->pFirst; p != NULL; p = p->pNext)
      if (p->nUse == CMemoryState::objectBlock) objects.Add(Data(p));
  }
  for (INT_PTR i = 0; i < objects.GetSize(); i++) pfn((CObject*)objects[i], pContext);
}

BOOL AFXAPI AfxDumpMemoryLeaks() {
  State* s = GetState();
  Lock lock(s);
  if (s->lCounts[CMemoryState::objectBlock] == 0 && s->lCounts[CMemoryState::bitBlock] == 0)
    return FALSE;
  CDumpContext dc;
  Output(dc, "Detected memory leaks!\n");
  DumpSince(dc, s, 0);
  return TRUE;
}

BOOL AFXAPI AfxDiagnosticInit() {
  GetState();
  return TRUE;
}

// ---------------------------------------------------------------------
// CMemoryState

CMemoryState::CMemoryState() {
  memset(m_lCounts, 0, sizeof(m_lCounts));
  memset(m_lSizes, 0, sizeof(m_lSizes));
  m_lHighWaterCount = 0;
  m_lTotalCount = 0;
  m_lRequest = 0;
}

void CMemoryState::Checkpoint() {
  State* s = GetState();
  Lock lock(s);
  memcpy(m_lCounts, s->lCounts, sizeof(m_lCounts));
  memcpy(m_lSizes, s->lSizes, sizeof(m_lSizes));
  m_lHighWaterCount = s->lHighWater;
  m_lTotalCount = s->lTotal;
  m_lRequest = s->lRequest;
}

BOOL CMemoryState::Difference(const CMemoryState& oldState, const CMemoryState& newState) {
  BOOL bDifferent = FALSE;
  for (int i = 0; i < nBlockUseMax; i++) {
    m_lCounts[i] = newState.m_lCounts[i] - oldState.m_lCounts[i];
    m_lSizes[i] = newState.m_lSizes[i] - oldState.m_lSizes[i];
    if ((i == objectBlock || i == bitBlock) && (m_lCounts[i] != 0 || m_lSizes[i] != 0))
      bDifferent = TRUE;
  }
  m_lHighWaterCount = newState.m_lHighWaterCount - oldState.m_lHighWaterCount;
  m_lTotalCount = newState.m_lTotalCount - oldState.m_lTotalCount;
  m_lRequest = oldState.m_lRequest;
  return bDifferent;
}

void CMemoryState::DumpStatistics() const {
  static const char* const names[nBlockUseMax] = {"Free", "Object", "Non-Object", "CRT",
                                                  "Ignored"};
  char sz[128];
  for (int i = 0; i < nBlockUseMax; i++) {
    if (i == crtBlock) continue;
    wsprintfA(sz, "%ld bytes in %ld %s Blocks.\n", m_lSizes[i], m_lCounts[i], names[i]);
    afxDump << sz;
  }
  wsprintfA(sz, "Largest number used: %ld bytes.\n", m_lHighWaterCount);
  afxDump << sz;
  wsprintfA(sz, "Total allocations: %ld bytes.\n", m_lTotalCount);
  afxDump << sz;
}

void CMemoryState::DumpAllObjectsSince() const {
  State* s = GetState();
  Lock lock(s);
  DumpSince(afxDump, s, m_lRequest);
}
