// atlalloc.h - ATL's allocators and the smart pointers over them:
// CCRTAllocator, CLocalAllocator, CGlobalAllocator, CComAllocator;
// CHeapPtr, CComHeapPtr, CTempBuffer; CAutoPtr, CAutoVectorPtr; CHandle;
// and the overflow-checked AtlAdd and AtlMultiply.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#ifndef __ATLALLOC_H__
#define __ATLALLOC_H__

#include <windows.h>
#include <objbase.h>
#include <stdlib.h>
#include <string.h>
#include <new>
#include <atlexcept.h>

namespace ATL {

// Overflow-checked arithmetic: S_OK and the result, or E_INVALIDARG.
template <typename T>
inline HRESULT AtlAdd(T* ptResult, T tLeft, T tRight) {
  if (__builtin_add_overflow(tLeft, tRight, ptResult)) return E_INVALIDARG;
  return S_OK;
}
template <typename T>
inline HRESULT AtlMultiply(T* ptResult, T tLeft, T tRight) {
  if (__builtin_mul_overflow(tLeft, tRight, ptResult)) return E_INVALIDARG;
  return S_OK;
}
template <typename T>
inline T AtlAlignUp(T n, ULONG nAlign) {
  return T((n + (nAlign - 1)) & ~(T(nAlign) - 1));
}
template <typename T>
inline T AtlAlignDown(T n, ULONG nAlign) {
  return T(n & ~(T(nAlign) - 1));
}

// The allocators: Reallocate, Allocate and Free, as static functions.
class CCRTAllocator {
public:
  static void* Reallocate(void* p, size_t nBytes) { return ::realloc(p, nBytes); }
  static void* Allocate(size_t nBytes) { return ::malloc(nBytes); }
  static void Free(void* p) { ::free(p); }
};

class CLocalAllocator {
public:
  static void* Allocate(size_t nBytes) { return ::LocalAlloc(LMEM_FIXED, nBytes); }
  static void* Reallocate(void* p, size_t nBytes) {
    if (p == NULL) return Allocate(nBytes);
    if (nBytes == 0) {
      Free(p);
      return NULL;
    }
    return ::LocalReAlloc(p, nBytes, LMEM_MOVEABLE);
  }
  static void Free(void* p) {
    if (p != NULL) ::LocalFree(p);
  }
};

class CGlobalAllocator {
public:
  static void* Allocate(size_t nBytes) { return ::GlobalAlloc(GMEM_FIXED, nBytes); }
  static void* Reallocate(void* p, size_t nBytes) {
    if (p == NULL) return Allocate(nBytes);
    if (nBytes == 0) {
      Free(p);
      return NULL;
    }
    return ::GlobalReAlloc(p, nBytes, GMEM_MOVEABLE);
  }
  static void Free(void* p) {
    if (p != NULL) ::GlobalFree(p);
  }
};

class CComAllocator {
public:
  static void* Reallocate(void* p, size_t nBytes) { return ::CoTaskMemRealloc(p, (ULONG)nBytes); }
  static void* Allocate(size_t nBytes) { return ::CoTaskMemAlloc((ULONG)nBytes); }
  static void Free(void* p) { ::CoTaskMemFree(p); }
};

// A pointer to memory from an allocator, freed when it goes.
template <typename T, class Allocator = CCRTAllocator>
class CHeapPtrBase {
protected:
  CHeapPtrBase() : m_pData(NULL) {}
  CHeapPtrBase(CHeapPtrBase<T, Allocator>& p) : m_pData(p.Detach()) {}
  explicit CHeapPtrBase(T* pData) : m_pData(pData) {}

public:
  T* m_pData;

  ~CHeapPtrBase() { Free(); }

  CHeapPtrBase<T, Allocator>& operator=(CHeapPtrBase<T, Allocator>& p) {
    if (m_pData != p.m_pData) Attach(p.Detach());
    return *this;
  }

  operator T*() const { return m_pData; }
  T* operator->() const { return m_pData; }
  T** operator&() { return &m_pData; }

  bool AllocateBytes(size_t nBytes) {
    m_pData = static_cast<T*>(Allocator::Allocate(nBytes));
    return m_pData != NULL;
  }
  void Attach(T* pData) {
    Allocator::Free(m_pData);
    m_pData = pData;
  }
  T* Detach() {
    T* p = m_pData;
    m_pData = NULL;
    return p;
  }
  void Free() {
    Allocator::Free(m_pData);
    m_pData = NULL;
  }
  bool ReallocateBytes(size_t nBytes) {
    T* p = static_cast<T*>(Allocator::Reallocate(m_pData, nBytes));
    if (p == NULL) return false;
    m_pData = p;
    return true;
  }
};

template <typename T, class Allocator = CCRTAllocator>
class CHeapPtr : public CHeapPtrBase<T, Allocator> {
public:
  CHeapPtr() {}
  CHeapPtr(CHeapPtr<T, Allocator>& p) : CHeapPtrBase<T, Allocator>(p) {}
  explicit CHeapPtr(T* p) : CHeapPtrBase<T, Allocator>(p) {}

  CHeapPtr<T, Allocator>& operator=(CHeapPtr<T, Allocator>& p) {
    CHeapPtrBase<T, Allocator>::operator=(p);
    return *this;
  }

  bool Allocate(size_t nElements = 1) {
    size_t nBytes;
    if (FAILED(AtlMultiply(&nBytes, nElements, sizeof(T)))) return false;
    return this->AllocateBytes(nBytes);
  }
  bool Reallocate(size_t nElements) {
    size_t nBytes;
    if (FAILED(AtlMultiply(&nBytes, nElements, sizeof(T)))) return false;
    return this->ReallocateBytes(nBytes);
  }
};

// Memory from CoTaskMemAlloc.
template <typename T>
class CComHeapPtr : public CHeapPtr<T, CComAllocator> {
public:
  CComHeapPtr() {}
  explicit CComHeapPtr(T* pData) : CHeapPtr<T, CComAllocator>(pData) {}
};

// A buffer on the stack when it is small, from the allocator when not.
template <typename T, int t_nFixedBytes = 128, class Allocator = CCRTAllocator>
class CTempBuffer {
public:
  CTempBuffer() : m_p(NULL) {}
  CTempBuffer(size_t nElements) : m_p(NULL) { Allocate(nElements); }
  ~CTempBuffer() {
    if (m_p != reinterpret_cast<T*>(m_abFixedBuffer)) FreeHeap();
  }

  operator T*() const { return m_p; }
  T* operator->() const { return m_p; }

  T* Allocate(size_t nElements) {
    size_t nBytes;
    if (FAILED(AtlMultiply(&nBytes, nElements, sizeof(T)))) AtlThrow(E_OUTOFMEMORY);
    return AllocateBytes(nBytes);
  }
  T* Reallocate(size_t nElements) {
    size_t nBytes;
    if (FAILED(AtlMultiply(&nBytes, nElements, sizeof(T)))) AtlThrow(E_OUTOFMEMORY);
    if (m_p == NULL) return AllocateBytes(nBytes);
    if (nBytes > t_nFixedBytes) {
      if (m_p == reinterpret_cast<T*>(m_abFixedBuffer)) {
        AllocateHeap(nBytes);
        ::memcpy(m_p, m_abFixedBuffer, t_nFixedBytes);
      } else {
        ReAllocateHeap(nBytes);
      }
    } else if (m_p != reinterpret_cast<T*>(m_abFixedBuffer)) {
      ::memcpy(m_abFixedBuffer, m_p, nBytes);
      FreeHeap();
      m_p = reinterpret_cast<T*>(m_abFixedBuffer);
    }
    return m_p;
  }
  T* AllocateBytes(size_t nBytes) {
    if (nBytes > t_nFixedBytes)
      AllocateHeap(nBytes);
    else
      m_p = reinterpret_cast<T*>(m_abFixedBuffer);
    return m_p;
  }

private:
  void AllocateHeap(size_t nBytes) {
    T* p = static_cast<T*>(Allocator::Allocate(nBytes));
    if (p == NULL) AtlThrow(E_OUTOFMEMORY);
    m_p = p;
  }
  void ReAllocateHeap(size_t nNewSize) {
    T* p = static_cast<T*>(Allocator::Reallocate(m_p, nNewSize));
    if (p == NULL) AtlThrow(E_OUTOFMEMORY);
    m_p = p;
  }
  void FreeHeap() { Allocator::Free(m_p); }

  T* m_p;
  alignas(8) BYTE m_abFixedBuffer[t_nFixedBytes];
};

// Ownership of a new'd object (CAutoPtr) or array (CAutoVectorPtr) that
// passes on copy, as std::auto_ptr's did.
template <typename T>
class CAutoPtr {
public:
  T* m_p;

  CAutoPtr() : m_p(NULL) {}
  template <typename TSrc>
  CAutoPtr(CAutoPtr<TSrc>& p) : m_p(p.Detach()) {}
  CAutoPtr(CAutoPtr<T>& p) : m_p(p.Detach()) {}
  explicit CAutoPtr(T* p) : m_p(p) {}
  ~CAutoPtr() { Free(); }

  template <typename TSrc>
  CAutoPtr<T>& operator=(CAutoPtr<TSrc>& p) {
    if (m_p != p.m_p) Attach(p.Detach());
    return *this;
  }
  CAutoPtr<T>& operator=(CAutoPtr<T>& p) {
    if (m_p != p.m_p) Attach(p.Detach());
    return *this;
  }
  bool operator!=(const CAutoPtr<T>& p) const { return m_p != p.m_p; }
  bool operator==(const CAutoPtr<T>& p) const { return m_p == p.m_p; }

  operator T*() const { return m_p; }
  T* operator->() const { return m_p; }

  void Attach(T* p) {
    Free();
    m_p = p;
  }
  T* Detach() {
    T* p = m_p;
    m_p = NULL;
    return p;
  }
  void Free() {
    delete m_p;
    m_p = NULL;
  }
};

template <typename T>
class CAutoVectorPtr {
public:
  T* m_p;

  CAutoVectorPtr() : m_p(NULL) {}
  CAutoVectorPtr(CAutoVectorPtr<T>& p) : m_p(p.Detach()) {}
  explicit CAutoVectorPtr(T* p) : m_p(p) {}
  ~CAutoVectorPtr() { Free(); }

  CAutoVectorPtr<T>& operator=(CAutoVectorPtr<T>& p) {
    if (m_p != p.m_p) Attach(p.Detach());
    return *this;
  }
  operator T*() const { return m_p; }

  bool Allocate(size_t nElements) {
    Free();
    m_p = new (std::nothrow) T[nElements];
    return m_p != NULL;
  }
  void Attach(T* p) {
    Free();
    m_p = p;
  }
  T* Detach() {
    T* p = m_p;
    m_p = NULL;
    return p;
  }
  void Free() {
    delete[] m_p;
    m_p = NULL;
  }
};

// A kernel handle, closed when it goes. NULL is "none" (not
// INVALID_HANDLE_VALUE: test CreateFile's result before attaching it).
class CHandle {
public:
  HANDLE m_h;

  CHandle() : m_h(NULL) {}
  CHandle(CHandle& h) : m_h(h.Detach()) {}
  explicit CHandle(HANDLE h) : m_h(h) {}
  ~CHandle() { Close(); }

  CHandle& operator=(CHandle& h) {
    if (this != &h) {
      Close();
      m_h = h.Detach();
    }
    return *this;
  }
  operator HANDLE() const { return m_h; }

  void Attach(HANDLE h) {
    Close();
    m_h = h;
  }
  HANDLE Detach() {
    HANDLE h = m_h;
    m_h = NULL;
    return h;
  }
  void Close() {
    if (m_h != NULL) {
      ::CloseHandle(m_h);
      m_h = NULL;
    }
  }
};

}  // namespace ATL

#endif  // __ATLALLOC_H__
