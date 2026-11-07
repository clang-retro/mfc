// kernel_mem.cpp - kernel32 memory: global and local blocks, heaps and
// virtual memory.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include "winport.h"

#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include <map>
#include <mutex>
#include <unordered_set>

using namespace winport;

// ---------------------------------------------------------------------
// Global and local blocks
//
// Every block has a header kept apart from its data, so a moveable
// block's handle stays the same when the data moves. The data starts
// with a pointer back to the header, which GlobalHandle follows. A fixed
// block's handle is its data pointer, as on Windows.

namespace {

struct Block {
  void* data;  // the user's bytes; NULL when discarded
  SIZE_T size;
  UINT flags;
  UINT locks;
};

const size_t kPrefix = 16;  // keeps the data aligned for any type

std::mutex& BlockLock() {
  static std::mutex m;
  return m;
}

std::unordered_set<Block*>& Blocks() {
  static std::unordered_set<Block*> s;
  return s;
}

void* NewData(Block* b, SIZE_T size, bool zero) {
  char* p = (char*)(zero ? calloc(1, kPrefix + size) : malloc(kPrefix + size));
  if (!p) return NULL;
  *(Block**)p = b;
  return p + kPrefix;
}

Block* FromData(const void* p) {
  if (!p) return NULL;
  Block* b = *(Block* const*)((const char*)p - kPrefix);
  return Blocks().count(b) ? b : NULL;
}

// A handle is a header (moveable blocks) or a data pointer (fixed ones).
Block* Resolve(HGLOBAL h) {
  if (!h) return NULL;
  Block* b = (Block*)h;
  if (Blocks().count(b)) return b;
  return FromData(h);
}

HGLOBAL HandleOf(Block* b) { return (b->flags & GMEM_MOVEABLE) ? (HGLOBAL)b : (HGLOBAL)b->data; }

HGLOBAL Alloc(UINT flags, SIZE_T size) {
  std::lock_guard<std::mutex> g(BlockLock());
  Block* b = new Block{NULL, size, flags & (GMEM_MOVEABLE | GMEM_DISCARDABLE | GMEM_DDESHARE), 0};
  if (!(flags & GMEM_MOVEABLE) || size > 0) {
    b->data = NewData(b, size, (flags & GMEM_ZEROINIT) != 0);
    if (!b->data) {
      delete b;
      SetLastError(ERROR_NOT_ENOUGH_MEMORY);
      return NULL;
    }
  }
  Blocks().insert(b);
  return HandleOf(b);
}

HGLOBAL ReAlloc(HGLOBAL h, SIZE_T size, UINT flags) {
  std::lock_guard<std::mutex> g(BlockLock());
  Block* b = Resolve(h);
  if (!b) {
    SetLastError(ERROR_INVALID_HANDLE);
    return NULL;
  }
  if (flags & GMEM_MODIFY) {
    if (flags & GMEM_MOVEABLE) b->flags |= GMEM_MOVEABLE;
    return h;
  }
  if (size == 0 && (b->flags & GMEM_MOVEABLE)) {
    // Discards the block; the handle stays valid.
    if (b->data) free((char*)b->data - kPrefix);
    b->data = NULL;
    b->size = 0;
    return h;
  }
  // A fixed block cannot move unless the caller allows it.
  if (!(b->flags & GMEM_MOVEABLE) && !(flags & GMEM_MOVEABLE) && size > b->size) {
    SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return NULL;
  }
  char* old = b->data ? (char*)b->data - kPrefix : NULL;
  char* p = (char*)realloc(old, kPrefix + size);
  if (!p) {
    SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return NULL;
  }
  if ((flags & GMEM_ZEROINIT) && size > b->size) memset(p + kPrefix + b->size, 0, size - b->size);
  b->data = p + kPrefix;
  b->size = size;
  return HandleOf(b);
}

HGLOBAL Free(HGLOBAL h) {
  if (!h) return NULL;
  std::lock_guard<std::mutex> g(BlockLock());
  Block* b = Resolve(h);
  if (!b) {
    SetLastError(ERROR_INVALID_HANDLE);
    return h;
  }
  Blocks().erase(b);
  if (b->data) free((char*)b->data - kPrefix);
  delete b;
  return NULL;
}

LPVOID Lock(HGLOBAL h) {
  std::lock_guard<std::mutex> g(BlockLock());
  Block* b = Resolve(h);
  if (!b) {
    SetLastError(ERROR_INVALID_HANDLE);
    return NULL;
  }
  if (!b->data) {
    SetLastError(ERROR_DISCARDED);
    return NULL;
  }
  if ((b->flags & GMEM_MOVEABLE) && b->locks < GMEM_LOCKCOUNT) b->locks++;
  return b->data;
}

BOOL Unlock(HGLOBAL h) {
  std::lock_guard<std::mutex> g(BlockLock());
  Block* b = Resolve(h);
  if (!b) {
    SetLastError(ERROR_INVALID_HANDLE);
    return FALSE;
  }
  if (b->locks == 0) {
    SetLastError(ERROR_NOT_LOCKED);
    return FALSE;
  }
  if (--b->locks > 0) return TRUE;
  SetLastError(ERROR_SUCCESS);
  return FALSE;
}

SIZE_T Size(HGLOBAL h) {
  std::lock_guard<std::mutex> g(BlockLock());
  Block* b = Resolve(h);
  if (!b) {
    SetLastError(ERROR_INVALID_HANDLE);
    return 0;
  }
  return b->size;
}

}  // namespace

extern "C" {

HGLOBAL WINAPI GlobalAlloc(UINT f, SIZE_T n) { return Alloc(f, n); }
HGLOBAL WINAPI GlobalReAlloc(HGLOBAL h, SIZE_T n, UINT f) { return ReAlloc(h, n, f); }
HGLOBAL WINAPI GlobalFree(HGLOBAL h) { return Free(h); }
LPVOID WINAPI GlobalLock(HGLOBAL h) { return Lock(h); }
BOOL WINAPI GlobalUnlock(HGLOBAL h) { return Unlock(h); }
SIZE_T WINAPI GlobalSize(HGLOBAL h) { return Size(h); }

UINT WINAPI GlobalFlags(HGLOBAL h) {
  std::lock_guard<std::mutex> g(BlockLock());
  Block* b = Resolve(h);
  if (!b) return GMEM_INVALID_HANDLE;
  return b->locks | (b->flags & (GMEM_DISCARDABLE | GMEM_DDESHARE)) | (b->data ? 0 : GMEM_DISCARDED);
}

HGLOBAL WINAPI GlobalHandle(LPCVOID p) {
  std::lock_guard<std::mutex> g(BlockLock());
  Block* b = FromData(p);
  if (!b) {
    SetLastError(ERROR_INVALID_HANDLE);
    return NULL;
  }
  return HandleOf(b);
}

HLOCAL WINAPI LocalAlloc(UINT f, SIZE_T n) { return Alloc(f, n); }
HLOCAL WINAPI LocalReAlloc(HLOCAL h, SIZE_T n, UINT f) { return ReAlloc(h, n, f); }
HLOCAL WINAPI LocalFree(HLOCAL h) { return Free(h); }
LPVOID WINAPI LocalLock(HLOCAL h) { return Lock(h); }
BOOL WINAPI LocalUnlock(HLOCAL h) { return Unlock(h); }
SIZE_T WINAPI LocalSize(HLOCAL h) { return Size(h); }

// ---------------------------------------------------------------------
// Heaps: blocks from malloc with their size in front.

namespace {
struct Heap : Object {
  Heap() : Object(kHeap) {}
};
}  // namespace

HANDLE WINAPI GetProcessHeap(void) {
  static HANDLE h = [] {
    Heap* p = new Heap;
    p->refs = 1 << 30;
    return NewHandle(p);
  }();
  return h;
}

HANDLE WINAPI HeapCreate(DWORD, SIZE_T, SIZE_T) { return NewHandle(new Heap); }

BOOL WINAPI HeapDestroy(HANDLE h) {
  if (h == GetProcessHeap()) return FALSE;
  return CloseHandle(h);
}

LPVOID WINAPI HeapAlloc(HANDLE, DWORD flags, SIZE_T n) {
  char* p = (char*)((flags & HEAP_ZERO_MEMORY) ? calloc(1, kPrefix + n) : malloc(kPrefix + n));
  if (!p) {
    SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return NULL;
  }
  *(SIZE_T*)p = n;
  return p + kPrefix;
}

LPVOID WINAPI HeapReAlloc(HANDLE h, DWORD flags, LPVOID mem, SIZE_T n) {
  if (!mem) return HeapAlloc(h, flags, n);
  char* old = (char*)mem - kPrefix;
  SIZE_T was = *(SIZE_T*)old;
  if ((flags & HEAP_REALLOC_IN_PLACE_ONLY) && n > was) {
    SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return NULL;
  }
  char* p = (char*)realloc(old, kPrefix + n);
  if (!p) {
    SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return NULL;
  }
  if ((flags & HEAP_ZERO_MEMORY) && n > was) memset(p + kPrefix + was, 0, n - was);
  *(SIZE_T*)p = n;
  return p + kPrefix;
}

BOOL WINAPI HeapFree(HANDLE, DWORD, LPVOID mem) {
  if (mem) free((char*)mem - kPrefix);
  return TRUE;
}

SIZE_T WINAPI HeapSize(HANDLE, DWORD, LPCVOID mem) {
  if (!mem) return (SIZE_T)-1;
  return *(const SIZE_T*)((const char*)mem - kPrefix);
}

// ---------------------------------------------------------------------
// Virtual memory over mmap. Reserving maps the range without access;
// committing gives it access.

}  // extern "C"

namespace {
std::mutex& RegionLock() {
  static std::mutex m;
  return m;
}
std::map<char*, SIZE_T>& Regions() {
  static std::map<char*, SIZE_T> m;
  return m;
}
int Prot(DWORD p) {
  switch (p & 0xff) {
    case PAGE_NOACCESS: return PROT_NONE;
    case PAGE_READONLY: return PROT_READ;
    case 0x10: return PROT_EXEC;                           // PAGE_EXECUTE
    case 0x20: return PROT_READ | PROT_EXEC;               // PAGE_EXECUTE_READ
    case 0x40: return PROT_READ | PROT_WRITE | PROT_EXEC;  // PAGE_EXECUTE_READWRITE
    default: return PROT_READ | PROT_WRITE;
  }
}
size_t PageSize() {
  static size_t n = (size_t)sysconf(_SC_PAGESIZE);
  return n;
}
}  // namespace

extern "C" {

LPVOID WINAPI VirtualAlloc(LPVOID addr, SIZE_T size, DWORD type, DWORD protect) {
  size_t pg = PageSize();
  size = (size + pg - 1) & ~(pg - 1);
  if (addr && !(type & MEM_RESERVE)) {
    // Committing pages of a reserved range.
    char* a = (char*)((UINT_PTR)addr & ~(pg - 1));
    if (mprotect(a, size, Prot(protect)) != 0) {
      SetErrorFromErrno();
      return NULL;
    }
    return a;
  }
  int prot = (type & MEM_COMMIT) ? Prot(protect) : PROT_NONE;
  void* p = mmap(addr, size, prot, MAP_PRIVATE | MAP_ANON, -1, 0);
  if (p == MAP_FAILED) {
    SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return NULL;
  }
  std::lock_guard<std::mutex> g(RegionLock());
  Regions()[(char*)p] = size;
  return p;
}

BOOL WINAPI VirtualFree(LPVOID addr, SIZE_T size, DWORD type) {
  if (type & MEM_RELEASE) {
    std::lock_guard<std::mutex> g(RegionLock());
    auto it = Regions().find((char*)addr);
    if (it == Regions().end()) {
      SetLastError(ERROR_INVALID_ADDRESS);
      return FALSE;
    }
    munmap(it->first, it->second);
    Regions().erase(it);
    return TRUE;
  }
  // Decommitting: the pages lose their access and contents.
  size_t pg = PageSize();
  char* a = (char*)((UINT_PTR)addr & ~(pg - 1));
  size = (size + pg - 1) & ~(pg - 1);
  if (mmap(a, size, PROT_NONE, MAP_PRIVATE | MAP_ANON | MAP_FIXED, -1, 0) == MAP_FAILED) {
    SetErrorFromErrno();
    return FALSE;
  }
  return TRUE;
}

SIZE_T WINAPI VirtualQuery(LPCVOID addr, PMEMORY_BASIC_INFORMATION info, SIZE_T n) {
  if (n < sizeof(*info)) {
    SetLastError(ERROR_BAD_LENGTH);
    return 0;
  }
  memset(info, 0, sizeof(*info));
  size_t pg = PageSize();
  info->BaseAddress = (PVOID)((UINT_PTR)addr & ~(pg - 1));
  info->RegionSize = pg;
  std::lock_guard<std::mutex> g(RegionLock());
  auto it = Regions().upper_bound((char*)addr);
  if (it != Regions().begin()) {
    --it;
    if ((const char*)addr < it->first + it->second) {
      info->AllocationBase = it->first;
      info->RegionSize = it->first + it->second - (char*)info->BaseAddress;
    }
  }
  info->State = MEM_COMMIT;
  info->Protect = info->AllocationProtect = PAGE_READWRITE;
  info->Type = 0x20000;  // MEM_PRIVATE
  return sizeof(*info);
}

// These only catch NULL; probing memory is not portable.
BOOL WINAPI IsBadReadPtr(LPCVOID p, UINT_PTR n) { return p == NULL && n != 0; }
BOOL WINAPI IsBadWritePtr(LPVOID p, UINT_PTR n) { return p == NULL && n != 0; }
BOOL WINAPI IsBadCodePtr(FARPROC p) { return p == NULL; }
BOOL WINAPI IsBadStringPtrA(LPCSTR p, UINT_PTR n) { return p == NULL && n != 0; }
BOOL WINAPI IsBadStringPtrW(LPCWSTR p, UINT_PTR n) { return p == NULL && n != 0; }

void WINAPI GlobalMemoryStatus(LPMEMORYSTATUS ms) {
  ULONGLONG pg = PageSize();
  ULONGLONG total = 0, avail = 0;
#ifdef _SC_PHYS_PAGES
  total = (ULONGLONG)sysconf(_SC_PHYS_PAGES) * pg;
#endif
#ifdef _SC_AVPHYS_PAGES
  avail = (ULONGLONG)sysconf(_SC_AVPHYS_PAGES) * pg;
#else
  avail = total / 2;
#endif
  ms->dwLength = sizeof(*ms);
  ms->dwMemoryLoad = total ? (DWORD)(100 - avail * 100 / total) : 0;
  ms->dwTotalPhys = (SIZE_T)total;
  ms->dwAvailPhys = (SIZE_T)avail;
  ms->dwTotalPageFile = (SIZE_T)total;
  ms->dwAvailPageFile = (SIZE_T)avail;
  ms->dwTotalVirtual = sizeof(void*) == 8 ? (SIZE_T)0x7fffffff0000ULL : (SIZE_T)0x7ffe0000;
  ms->dwAvailVirtual = ms->dwTotalVirtual;
}

}  // extern "C"
