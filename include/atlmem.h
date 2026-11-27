// atlmem.h - memory managers: the IAtlMemMgr interface and the heaps that
// implement it over Win32's HeapAlloc (CWin32Heap), COM's task allocator
// (CComHeap), the C library (CCRTHeap), and LocalAlloc and GlobalAlloc
// (CLocalHeap, CGlobalHeap).
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#ifndef __ATLMEM_H__
#define __ATLMEM_H__

#include <atlbase.h>
#include <stdlib.h>

namespace ATL {

struct IAtlMemMgr {
  virtual ~IAtlMemMgr() {}
  virtual void* Allocate(size_t nBytes) = 0;
  virtual void Free(void* p) = 0;
  virtual void* Reallocate(void* p, size_t nBytes) = 0;
  virtual size_t GetSize(void* p) = 0;
};

// A Win32 heap: one of its own (created, and destroyed with it), or one
// it is given.
class CWin32Heap : public IAtlMemMgr {
public:
  HANDLE m_hHeap;
  bool m_bOwnHeap;

  CWin32Heap() : m_hHeap(NULL), m_bOwnHeap(false) {}
  explicit CWin32Heap(HANDLE hHeap) : m_hHeap(hHeap), m_bOwnHeap(false) {}
  CWin32Heap(DWORD dwFlags, size_t nInitialSize, size_t nMaxSize = 0)
      : m_hHeap(NULL), m_bOwnHeap(true) {
    m_hHeap = ::HeapCreate(dwFlags, nInitialSize, nMaxSize);
    if (m_hHeap == NULL) AtlThrowLastWin32();
  }
  virtual ~CWin32Heap() {
    if (m_bOwnHeap && m_hHeap != NULL) ::HeapDestroy(m_hHeap);
  }
  CWin32Heap(const CWin32Heap&) = delete;
  CWin32Heap& operator=(const CWin32Heap&) = delete;

  void Attach(HANDLE hHeap, bool bTakeOwnership) {
    if (m_bOwnHeap && m_hHeap != NULL) ::HeapDestroy(m_hHeap);
    m_hHeap = hHeap;
    m_bOwnHeap = bTakeOwnership;
  }
  HANDLE Detach() {
    HANDLE h = m_hHeap;
    m_hHeap = NULL;
    m_bOwnHeap = false;
    return h;
  }

  void* Allocate(size_t nBytes) override { return ::HeapAlloc(m_hHeap, 0, nBytes); }
  void Free(void* p) override {
    if (p != NULL) ::HeapFree(m_hHeap, 0, p);
  }
  void* Reallocate(void* p, size_t nBytes) override {
    if (p == NULL) return Allocate(nBytes);
    if (nBytes == 0) {
      Free(p);
      return NULL;
    }
    return ::HeapReAlloc(m_hHeap, 0, p, nBytes);
  }
  size_t GetSize(void* p) override { return ::HeapSize(m_hHeap, 0, p); }
};

class CComHeap : public IAtlMemMgr {
public:
  void* Allocate(size_t nBytes) override { return ::CoTaskMemAlloc((ULONG)nBytes); }
  void Free(void* p) override { ::CoTaskMemFree(p); }
  void* Reallocate(void* p, size_t nBytes) override { return ::CoTaskMemRealloc(p, (ULONG)nBytes); }
  size_t GetSize(void* p) override {
    IMalloc* pMalloc = NULL;
    if (FAILED(::CoGetMalloc(1, &pMalloc))) return 0;
    size_t n = pMalloc->GetSize(p);
    pMalloc->Release();
    return n;
  }
};

// The C library's heap. It keeps the size of each block in front of it,
// since the library has no portable way to give it.
class CCRTHeap : public IAtlMemMgr {
public:
  void* Allocate(size_t nBytes) override {
    size_t* p = (size_t*)::malloc(nBytes + sizeof(_Header));
    if (p == NULL) return NULL;
    *p = nBytes;
    return (BYTE*)p + sizeof(_Header);
  }
  void Free(void* p) override {
    if (p != NULL) ::free((BYTE*)p - sizeof(_Header));
  }
  void* Reallocate(void* p, size_t nBytes) override {
    if (p == NULL) return Allocate(nBytes);
    if (nBytes == 0) {
      Free(p);
      return NULL;
    }
    size_t* q = (size_t*)::realloc((BYTE*)p - sizeof(_Header), nBytes + sizeof(_Header));
    if (q == NULL) return NULL;
    *q = nBytes;
    return (BYTE*)q + sizeof(_Header);
  }
  size_t GetSize(void* p) override { return p == NULL ? 0 : *(size_t*)((BYTE*)p - sizeof(_Header)); }

private:
  // Room for the size that keeps the block aligned as malloc's are.
  union alignas(16) _Header {
    size_t n;
    char pad[16];
  };
};

class CLocalHeap : public IAtlMemMgr {
public:
  void* Allocate(size_t nBytes) override { return ::LocalAlloc(LMEM_FIXED, nBytes); }
  void Free(void* p) override {
    if (p != NULL) ::LocalFree((HLOCAL)p);
  }
  void* Reallocate(void* p, size_t nBytes) override {
    if (p == NULL) return Allocate(nBytes);
    if (nBytes == 0) {
      Free(p);
      return NULL;
    }
    return ::LocalReAlloc((HLOCAL)p, nBytes, LMEM_MOVEABLE);
  }
  size_t GetSize(void* p) override { return ::LocalSize((HLOCAL)p); }
};

class CGlobalHeap : public IAtlMemMgr {
public:
  void* Allocate(size_t nBytes) override { return ::GlobalAlloc(GMEM_FIXED, nBytes); }
  void Free(void* p) override {
    if (p != NULL) ::GlobalFree((HGLOBAL)p);
  }
  void* Reallocate(void* p, size_t nBytes) override {
    if (p == NULL) return Allocate(nBytes);
    if (nBytes == 0) {
      Free(p);
      return NULL;
    }
    return ::GlobalReAlloc((HGLOBAL)p, nBytes, GMEM_MOVEABLE);
  }
  size_t GetSize(void* p) override { return ::GlobalSize((HGLOBAL)p); }
};

}  // namespace ATL

#endif  // __ATLMEM_H__
