// atlcoll.h - ATL 7's collections: CAtlArray, CAtlList, CAtlMap (a hash
// table), CRBMap and CRBMultiMap (sorted), the arrays and lists of
// CAutoPtr and interface pointers, and the element traits that tell them
// how to copy, move, compare and hash their elements.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#ifndef __ATLCOLL_H__
#define __ATLCOLL_H__

#include <atlbase.h>
#include <map>
#include <new>
#include <type_traits>

#ifndef _ATL_POSITION_DEFINED
#define _ATL_POSITION_DEFINED
struct __POSITION {};
typedef __POSITION* POSITION;
#endif

namespace ATL {

// ---------------------------------------------------------------------
// Element traits

// How elements are passed, copied and moved. Moving is by their bytes,
// as in ATL (CAutoPtr, CComPtr and CString all allow it).
template <typename T>
class CElementTraitsBase {
public:
  typedef const T& INARGTYPE;
  typedef T& OUTARGTYPE;

  static void CopyElements(T* pDest, const T* pSrc, size_t nElements) {
    for (size_t i = 0; i < nElements; i++) pDest[i] = pSrc[i];
  }
  static void RelocateElements(T* pDest, T* pSrc, size_t nElements) {
    ::memmove((void*)pDest, (void*)pSrc, nElements * sizeof(T));
  }
};

template <typename T>
class CDefaultHashTraits {
public:
  static ULONG Hash(const T& element) {
    if constexpr (std::is_integral<T>::value || std::is_enum<T>::value) {
      unsigned long long v = (unsigned long long)element;
      return (ULONG)(v ^ (v >> 32));
    } else if constexpr (std::is_pointer<T>::value) {
      unsigned long long v = (unsigned long long)(ULONG_PTR)element;
      return (ULONG)((v >> 4) ^ (v >> 32));
    } else {
      // Anything else by its bytes (a GUID, a POINT).
      const BYTE* p = (const BYTE*)&element;
      ULONG h = 0;
      for (size_t i = 0; i < sizeof(T); i++) h = (h << 5) + h + p[i];
      return h;
    }
  }
};

template <typename T>
class CDefaultCompareTraits {
public:
  static bool CompareElements(const T& element1, const T& element2) { return element1 == element2; }
  static int CompareElementsOrdered(const T& element1, const T& element2) {
    return element1 < element2 ? -1 : element2 < element1 ? 1 : 0;
  }
};

template <typename T>
class CDefaultElementTraits : public CElementTraitsBase<T>,
                              public CDefaultHashTraits<T>,
                              public CDefaultCompareTraits<T> {};

template <typename T>
class CElementTraits : public CDefaultElementTraits<T> {};

template <typename T>
class CPrimitiveElementTraits : public CDefaultElementTraits<T> {
public:
  typedef T INARGTYPE;
  typedef T& OUTARGTYPE;
};

template <>
class CElementTraits<GUID> : public CElementTraitsBase<GUID> {
public:
  static ULONG Hash(const GUID& guid) {
    const DWORD* p = (const DWORD*)&guid;
    return p[0] ^ p[1] ^ p[2] ^ p[3];
  }
  static bool CompareElements(const GUID& g1, const GUID& g2) { return ::IsEqualGUID(g1, g2) != 0; }
  static int CompareElementsOrdered(const GUID& g1, const GUID& g2) {
    int n = ::memcmp(&g1, &g2, sizeof(GUID));
    return n < 0 ? -1 : n > 0 ? 1 : 0;
  }
};

template <>
class CElementTraits<CComBSTR> : public CElementTraitsBase<CComBSTR> {
public:
  static ULONG Hash(const CComBSTR& bstr) {
    ULONG h = 0;
    UINT n = bstr.m_str ? ::SysStringLen(bstr.m_str) : 0;
    for (UINT i = 0; i < n; i++) h = (h << 5) + h + (ULONG)bstr.m_str[i];
    return h;
  }
  static bool CompareElements(const CComBSTR& b1, const CComBSTR& b2) { return b1 == b2; }
  static int CompareElementsOrdered(const CComBSTR& b1, const CComBSTR& b2) {
    UINT n1 = b1.m_str ? ::SysStringLen(b1.m_str) : 0, n2 = b2.m_str ? ::SysStringLen(b2.m_str) : 0;
    for (UINT i = 0; i < n1 && i < n2; i++)
      if (b1.m_str[i] != b2.m_str[i]) return b1.m_str[i] < b2.m_str[i] ? -1 : 1;
    return n1 < n2 ? -1 : n1 > n2 ? 1 : 0;
  }
};

template <>
class CElementTraits<CComVariant> : public CElementTraitsBase<CComVariant> {
public:
  typedef const VARIANT& INARGTYPE;
  static bool CompareElements(INARGTYPE v1, INARGTYPE v2) { return CComVariant(v1) == v2; }
  static int CompareElementsOrdered(INARGTYPE v1, INARGTYPE v2) {
    CComVariant a(v1);
    return a < v2 ? -1 : a > v2 ? 1 : 0;
  }
};

// Strings (CString and the like): passed as their characters, hashed by
// them, and compared exactly (CStringElementTraits) or ignoring case
// (CStringElementTraitsI).
template <typename T>
class _CStringHashTraits {
public:
  static ULONG Hash(typename T::PCXSTR psz) {
    ULONG h = 0;
    for (; psz != NULL && *psz; psz++) h = (h << 5) + h + (ULONG)(typename std::make_unsigned<typename T::XCHAR>::type)*psz;
    return h;
  }
};

template <typename T>
class CStringElementTraits : public CElementTraitsBase<T>, public _CStringHashTraits<T> {
public:
  typedef typename T::PCXSTR INARGTYPE;
  typedef T& OUTARGTYPE;
  static bool CompareElements(INARGTYPE str1, INARGTYPE str2) { return T(str1).Compare(str2) == 0; }
  static int CompareElementsOrdered(INARGTYPE str1, INARGTYPE str2) {
    return T(str1).Compare(str2);
  }
};

template <typename T>
class CStringElementTraitsI : public CElementTraitsBase<T> {
public:
  typedef typename T::PCXSTR INARGTYPE;
  typedef T& OUTARGTYPE;
  static ULONG Hash(INARGTYPE psz) {
    T s(psz);
    s.MakeUpper();
    return _CStringHashTraits<T>::Hash(s);
  }
  static bool CompareElements(INARGTYPE str1, INARGTYPE str2) {
    return T(str1).CompareNoCase(str2) == 0;
  }
  static int CompareElementsOrdered(INARGTYPE str1, INARGTYPE str2) {
    return T(str1).CompareNoCase(str2);
  }
};

template <typename T>
class CStringRefElementTraits : public CElementTraitsBase<T> {
public:
  static ULONG Hash(const T& str) { return _CStringHashTraits<T>::Hash(str); }
  static bool CompareElements(const T& str1, const T& str2) { return str1 == str2; }
  static int CompareElementsOrdered(const T& str1, const T& str2) { return str1.Compare(str2); }
};

template <typename T>
class CAutoPtrElementTraits : public CDefaultElementTraits<CAutoPtr<T>> {
public:
  typedef CAutoPtr<T>& INARGTYPE;
  typedef T*& OUTARGTYPE;
};

template <class I, const IID* piid = &__uuidof(I)>
class CComQIPtrElementTraits : public CDefaultElementTraits<CComQIPtr<I, piid>> {
public:
  typedef I* INARGTYPE;
};

// ---------------------------------------------------------------------
// CAtlArray

template <typename E, class ETraits = CElementTraits<E>>
class CAtlArray {
public:
  typedef typename ETraits::INARGTYPE INARGTYPE;
  typedef typename ETraits::OUTARGTYPE OUTARGTYPE;

  CAtlArray() : m_pData(NULL), m_nSize(0), m_nMaxSize(0), m_nGrowBy(0) {}
  CAtlArray(const CAtlArray&) = delete;
  CAtlArray& operator=(const CAtlArray&) = delete;
  ~CAtlArray() { RemoveAll(); }

  size_t GetCount() const { return m_nSize; }
  bool IsEmpty() const { return m_nSize == 0; }
  bool SetCount(size_t nNewSize, int nGrowBy = -1) {
    if (nGrowBy != -1) m_nGrowBy = nGrowBy;
    if (nNewSize == 0) {
      RemoveAll();
      return true;
    }
    if (nNewSize > m_nMaxSize && !_Grow(nNewSize)) return false;
    for (size_t i = m_nSize; i < nNewSize; i++) new (m_pData + i) E();
    for (size_t i = nNewSize; i < m_nSize; i++) m_pData[i].~E();
    m_nSize = nNewSize;
    return true;
  }
  void FreeExtra() {
    if (m_nSize == m_nMaxSize) return;
    if (m_nSize == 0) {
      ::free(m_pData);
      m_pData = NULL;
      m_nMaxSize = 0;
      return;
    }
    E* p = (E*)::malloc(m_nSize * sizeof(E));
    if (p == NULL) return;
    ETraits::RelocateElements(p, m_pData, m_nSize);
    ::free(m_pData);
    m_pData = p;
    m_nMaxSize = m_nSize;
  }
  void RemoveAll() {
    for (size_t i = 0; i < m_nSize; i++) m_pData[i].~E();
    ::free(m_pData);
    m_pData = NULL;
    m_nSize = m_nMaxSize = 0;
  }

  const E& GetAt(size_t iElement) const {
    if (iElement >= m_nSize) AtlThrow(E_INVALIDARG);
    return m_pData[iElement];
  }
  E& GetAt(size_t iElement) {
    if (iElement >= m_nSize) AtlThrow(E_INVALIDARG);
    return m_pData[iElement];
  }
  void SetAt(size_t iElement, INARGTYPE element) {
    if (iElement >= m_nSize) AtlThrow(E_INVALIDARG);
    m_pData[iElement] = element;
  }
  const E& operator[](size_t iElement) const { return GetAt(iElement); }
  E& operator[](size_t iElement) { return GetAt(iElement); }
  const E* GetData() const { return m_pData; }
  E* GetData() { return m_pData; }

  void SetAtGrow(size_t iElement, INARGTYPE element) {
    if (iElement >= m_nSize) {
      E tmp(element);  // element may be one of ours
      if (!SetCount(iElement + 1)) AtlThrow(E_OUTOFMEMORY);
      m_pData[iElement] = tmp;
    } else {
      m_pData[iElement] = element;
    }
  }
  size_t Add() {
    size_t i = m_nSize;
    if (!SetCount(m_nSize + 1)) AtlThrow(E_OUTOFMEMORY);
    return i;
  }
  size_t Add(INARGTYPE element) {
    size_t i = m_nSize;
    if (m_nSize == m_nMaxSize) {
      E tmp(element);
      if (!_Grow(m_nSize + 1)) AtlThrow(E_OUTOFMEMORY);
      new (m_pData + i) E(tmp);
    } else {
      new (m_pData + i) E(element);
    }
    m_nSize++;
    return i;
  }
  size_t Append(const CAtlArray<E, ETraits>& aSrc) {
    size_t nOld = m_nSize, n = aSrc.m_nSize;
    if (!SetCount(nOld + n)) AtlThrow(E_OUTOFMEMORY);
    ETraits::CopyElements(m_pData + nOld, aSrc.m_pData, n);
    return nOld;
  }
  void Copy(const CAtlArray<E, ETraits>& aSrc) {
    if (this == &aSrc) return;
    if (!SetCount(aSrc.m_nSize)) AtlThrow(E_OUTOFMEMORY);
    ETraits::CopyElements(m_pData, aSrc.m_pData, aSrc.m_nSize);
  }
  void InsertAt(size_t iElement, INARGTYPE element, size_t nCount = 1) {
    E tmp(element);
    _OpenGap(iElement, nCount);
    for (size_t i = 0; i < nCount; i++) new (m_pData + iElement + i) E(tmp);
  }
  void InsertArrayAt(size_t iStart, const CAtlArray<E, ETraits>* paNew) {
    if (paNew == NULL || paNew->m_nSize == 0) return;
    if (paNew == this) AtlThrow(E_INVALIDARG);
    _OpenGap(iStart, paNew->m_nSize);
    for (size_t i = 0; i < paNew->m_nSize; i++) new (m_pData + iStart + i) E(paNew->m_pData[i]);
  }
  void RemoveAt(size_t iElement, size_t nCount = 1) {
    if (iElement + nCount > m_nSize) AtlThrow(E_INVALIDARG);
    for (size_t i = 0; i < nCount; i++) m_pData[iElement + i].~E();
    size_t nMove = m_nSize - (iElement + nCount);
    if (nMove) ETraits::RelocateElements(m_pData + iElement, m_pData + iElement + nCount, nMove);
    m_nSize -= nCount;
  }

private:
  E* m_pData;
  size_t m_nSize;
  size_t m_nMaxSize;
  int m_nGrowBy;

  bool _Grow(size_t nNewSize) {
    size_t nGrowBy = m_nGrowBy > 0 ? (size_t)m_nGrowBy : (m_nSize / 2 < 4 ? 4 : m_nSize / 2);
    size_t nNewMax = m_nMaxSize + nGrowBy;
    if (nNewMax < nNewSize) nNewMax = nNewSize;
    E* p = (E*)::malloc(nNewMax * sizeof(E));
    if (p == NULL) return false;
    if (m_nSize) ETraits::RelocateElements(p, m_pData, m_nSize);
    ::free(m_pData);
    m_pData = p;
    m_nMaxSize = nNewMax;
    return true;
  }
  // Room for nCount elements at iElement (past the end grows the array
  // with default elements first); the gap is raw memory.
  void _OpenGap(size_t iElement, size_t nCount) {
    if (iElement > m_nSize && !SetCount(iElement)) AtlThrow(E_OUTOFMEMORY);
    if (m_nSize + nCount > m_nMaxSize && !_Grow(m_nSize + nCount)) AtlThrow(E_OUTOFMEMORY);
    if (iElement < m_nSize)
      ETraits::RelocateElements(m_pData + iElement + nCount, m_pData + iElement, m_nSize - iElement);
    m_nSize += nCount;
  }
};

template <typename E>
class CAutoPtrArray : public CAtlArray<CAutoPtr<E>, CAutoPtrElementTraits<E>> {};

template <class I, const IID* piid = &__uuidof(I)>
class CInterfaceArray : public CAtlArray<CComQIPtr<I, piid>, CComQIPtrElementTraits<I, piid>> {};

// ---------------------------------------------------------------------
// CAtlList

template <typename E, class ETraits = CElementTraits<E>>
class CAtlList {
public:
  typedef typename ETraits::INARGTYPE INARGTYPE;

private:
  struct CNode : public __POSITION {
    CNode* m_pNext;
    CNode* m_pPrev;
    E m_element;
    CNode() : m_pNext(NULL), m_pPrev(NULL), m_element() {}
    CNode(INARGTYPE element) : m_pNext(NULL), m_pPrev(NULL), m_element(element) {}
  };

public:
  CAtlList(UINT nBlockSize = 10) : m_pHead(NULL), m_pTail(NULL), m_nElements(0) { (void)nBlockSize; }
  CAtlList(const CAtlList&) = delete;
  CAtlList& operator=(const CAtlList&) = delete;
  ~CAtlList() { RemoveAll(); }

  size_t GetCount() const { return m_nElements; }
  bool IsEmpty() const { return m_nElements == 0; }

  E& GetHead() { return _Check(m_pHead)->m_element; }
  const E& GetHead() const { return _Check(m_pHead)->m_element; }
  E& GetTail() { return _Check(m_pTail)->m_element; }
  const E& GetTail() const { return _Check(m_pTail)->m_element; }

  E RemoveHead() {
    CNode* p = _Check(m_pHead);
    E e(p->m_element);
    _Free(p);
    return e;
  }
  E RemoveTail() {
    CNode* p = _Check(m_pTail);
    E e(p->m_element);
    _Free(p);
    return e;
  }
  void RemoveHeadNoReturn() { _Free(_Check(m_pHead)); }
  void RemoveTailNoReturn() { _Free(_Check(m_pTail)); }

  POSITION AddHead() { return _Link(new CNode(), NULL, m_pHead); }
  POSITION AddHead(INARGTYPE element) { return _Link(new CNode(element), NULL, m_pHead); }
  POSITION AddTail() { return _Link(new CNode(), m_pTail, NULL); }
  POSITION AddTail(INARGTYPE element) { return _Link(new CNode(element), m_pTail, NULL); }
  void AddHeadList(const CAtlList<E, ETraits>* plNew) {
    for (CNode* p = plNew->m_pTail; p != NULL; p = p->m_pPrev) AddHead(p->m_element);
  }
  void AddTailList(const CAtlList<E, ETraits>* plNew) {
    for (CNode* p = plNew->m_pHead; p != NULL; p = p->m_pNext) AddTail(p->m_element);
  }
  void RemoveAll() {
    while (m_pHead != NULL) _Free(m_pHead);
  }

  POSITION GetHeadPosition() const { return m_pHead; }
  POSITION GetTailPosition() const { return m_pTail; }
  E& GetNext(POSITION& pos) {
    CNode* p = (CNode*)pos;
    pos = p->m_pNext;
    return p->m_element;
  }
  const E& GetNext(POSITION& pos) const {
    CNode* p = (CNode*)pos;
    pos = p->m_pNext;
    return p->m_element;
  }
  E& GetPrev(POSITION& pos) {
    CNode* p = (CNode*)pos;
    pos = p->m_pPrev;
    return p->m_element;
  }
  const E& GetPrev(POSITION& pos) const {
    CNode* p = (CNode*)pos;
    pos = p->m_pPrev;
    return p->m_element;
  }
  E& GetAt(POSITION pos) { return ((CNode*)pos)->m_element; }
  const E& GetAt(POSITION pos) const { return ((CNode*)pos)->m_element; }
  void SetAt(POSITION pos, INARGTYPE element) { ((CNode*)pos)->m_element = element; }
  void RemoveAt(POSITION pos) { _Free((CNode*)pos); }

  POSITION InsertBefore(POSITION pos, INARGTYPE element) {
    if (pos == NULL) return AddHead(element);
    CNode* pOld = (CNode*)pos;
    return _Link(new CNode(element), pOld->m_pPrev, pOld);
  }
  POSITION InsertAfter(POSITION pos, INARGTYPE element) {
    if (pos == NULL) return AddTail(element);
    CNode* pOld = (CNode*)pos;
    return _Link(new CNode(element), pOld, pOld->m_pNext);
  }

  POSITION Find(INARGTYPE element, POSITION posStartAfter = NULL) const {
    CNode* p = posStartAfter == NULL ? m_pHead : ((CNode*)posStartAfter)->m_pNext;
    for (; p != NULL; p = p->m_pNext)
      if (ETraits::CompareElements(p->m_element, element)) return p;
    return NULL;
  }
  POSITION FindIndex(size_t iElement) const {
    if (iElement >= m_nElements) return NULL;
    CNode* p = m_pHead;
    while (iElement--) p = p->m_pNext;
    return p;
  }

  void MoveToHead(POSITION pos) {
    CNode* p = (CNode*)pos;
    _Unlink(p);
    _Link(p, NULL, m_pHead);
  }
  void MoveToTail(POSITION pos) {
    CNode* p = (CNode*)pos;
    _Unlink(p);
    _Link(p, m_pTail, NULL);
  }
  void SwapElements(POSITION pos1, POSITION pos2) {
    if (pos1 == pos2) return;
    CNode* p1 = (CNode*)pos1;
    CNode* p2 = (CNode*)pos2;
    // By relinking, so that the positions follow their elements.
    CNode* pPrev2 = p2->m_pPrev;
    if (pPrev2 == p1) {  // p1 just before p2
      _Unlink(p2);
      _Link(p2, p1->m_pPrev, p1);
      return;
    }
    if (p1->m_pPrev == p2) {
      _Unlink(p1);
      _Link(p1, p2->m_pPrev, p2);
      return;
    }
    CNode* pPrev1 = p1->m_pPrev;
    _Unlink(p1);
    _Unlink(p2);
    _Link(p2, pPrev1, pPrev1 ? pPrev1->m_pNext : m_pHead);
    _Link(p1, pPrev2, pPrev2 ? pPrev2->m_pNext : m_pHead);
  }

private:
  CNode* m_pHead;
  CNode* m_pTail;
  size_t m_nElements;

  static CNode* _Check(CNode* p) {
    if (p == NULL) AtlThrow(E_INVALIDARG);
    return p;
  }
  POSITION _Link(CNode* p, CNode* pPrev, CNode* pNext) {
    p->m_pPrev = pPrev;
    p->m_pNext = pNext;
    if (pPrev) pPrev->m_pNext = p; else m_pHead = p;
    if (pNext) pNext->m_pPrev = p; else m_pTail = p;
    m_nElements++;
    return p;
  }
  void _Unlink(CNode* p) {
    if (p->m_pPrev) p->m_pPrev->m_pNext = p->m_pNext; else m_pHead = p->m_pNext;
    if (p->m_pNext) p->m_pNext->m_pPrev = p->m_pPrev; else m_pTail = p->m_pPrev;
    m_nElements--;
  }
  void _Free(CNode* p) {
    _Unlink(p);
    delete p;
  }
};

template <typename E>
class CAutoPtrList : public CAtlList<CAutoPtr<E>, CAutoPtrElementTraits<E>> {};

template <class I, const IID* piid = &__uuidof(I)>
class CInterfaceList : public CAtlList<CComQIPtr<I, piid>, CComQIPtrElementTraits<I, piid>> {};

// ---------------------------------------------------------------------
// CAtlMap: a hash table with chained bins, rehashed as it grows.

template <typename K, typename V, class KTraits = CElementTraits<K>,
          class VTraits = CElementTraits<V>>
class CAtlMap {
public:
  typedef typename KTraits::INARGTYPE KINARGTYPE;
  typedef typename KTraits::OUTARGTYPE KOUTARGTYPE;
  typedef typename VTraits::INARGTYPE VINARGTYPE;
  typedef typename VTraits::OUTARGTYPE VOUTARGTYPE;

  class CPair : public __POSITION {
  protected:
    CPair(KINARGTYPE key) : m_key(key) {}

  public:
    const K m_key;
    V m_value;
  };

private:
  class CNode : public CPair {
  public:
    CNode(KINARGTYPE key, UINT nHash) : CPair(key), m_pNext(NULL), m_nHash(nHash) {}
    CNode* m_pNext;
    UINT m_nHash;
  };

public:
  CAtlMap(UINT nBins = 17, float fOptimalLoad = 0.75f, float fLoThreshold = 0.25f,
          float fHiThreshold = 2.25f, UINT nBlockSize = 10)
      : m_ppBins(NULL), m_nBins(nBins), m_nElements(0), m_nLockCount(0),
        m_fOptimalLoad(fOptimalLoad), m_fLoThreshold(fLoThreshold), m_fHiThreshold(fHiThreshold) {
    (void)nBlockSize;
  }
  CAtlMap(const CAtlMap&) = delete;
  CAtlMap& operator=(const CAtlMap&) = delete;
  ~CAtlMap() { RemoveAll(); }

  size_t GetCount() const { return m_nElements; }
  bool IsEmpty() const { return m_nElements == 0; }

  bool Lookup(KINARGTYPE key, VOUTARGTYPE value) const {
    const CNode* p = _Find(key);
    if (p == NULL) return false;
    value = p->m_value;
    return true;
  }
  const CPair* Lookup(KINARGTYPE key) const { return _Find(key); }
  CPair* Lookup(KINARGTYPE key) { return _Find(key); }
  V& operator[](KINARGTYPE key) { return _FindOrAdd(key)->m_value; }

  POSITION SetAt(KINARGTYPE key, VINARGTYPE value) {
    CNode* p = _FindOrAdd(key);
    p->m_value = value;
    return p;
  }
  void SetValueAt(POSITION pos, VINARGTYPE value) { ((CNode*)pos)->m_value = value; }

  bool RemoveKey(KINARGTYPE key) {
    if (m_ppBins == NULL) return false;
    UINT nHash = (UINT)KTraits::Hash(key);
    CNode** pp = &m_ppBins[nHash % m_nBins];
    for (; *pp != NULL; pp = &(*pp)->m_pNext) {
      if ((*pp)->m_nHash == nHash && KTraits::CompareElements((*pp)->m_key, key)) {
        _Remove(pp);
        return true;
      }
    }
    return false;
  }
  void RemoveAll() {
    if (m_ppBins != NULL) {
      for (UINT i = 0; i < m_nBins; i++) {
        CNode* p = m_ppBins[i];
        while (p != NULL) {
          CNode* pNext = p->m_pNext;
          delete p;
          p = pNext;
        }
      }
      delete[] m_ppBins;
      m_ppBins = NULL;
    }
    m_nElements = 0;
  }
  void RemoveAtPos(POSITION pos) {
    CNode* pNode = (CNode*)pos;
    CNode** pp = &m_ppBins[pNode->m_nHash % m_nBins];
    while (*pp != pNode) pp = &(*pp)->m_pNext;
    _Remove(pp);
  }

  POSITION GetStartPosition() const { return _FirstFrom(0); }
  void GetNextAssoc(POSITION& pos, KOUTARGTYPE key, VOUTARGTYPE value) const {
    CNode* p = (CNode*)pos;
    key = p->m_key;
    value = p->m_value;
    pos = _Next(p);
  }
  const CPair* GetNext(POSITION& pos) const {
    CNode* p = (CNode*)pos;
    pos = _Next(p);
    return p;
  }
  CPair* GetNext(POSITION& pos) {
    CNode* p = (CNode*)pos;
    pos = _Next(p);
    return p;
  }
  const K& GetNextKey(POSITION& pos) const { return GetNext(pos)->m_key; }
  const V& GetNextValue(POSITION& pos) const { return GetNext(pos)->m_value; }
  V& GetNextValue(POSITION& pos) { return GetNext(pos)->m_value; }
  void GetAt(POSITION pos, KOUTARGTYPE key, VOUTARGTYPE value) const {
    key = ((CNode*)pos)->m_key;
    value = ((CNode*)pos)->m_value;
  }
  CPair* GetAt(POSITION pos) { return (CNode*)pos; }
  const CPair* GetAt(POSITION pos) const { return (CNode*)pos; }
  const K& GetKeyAt(POSITION pos) const { return ((CNode*)pos)->m_key; }
  const V& GetValueAt(POSITION pos) const { return ((CNode*)pos)->m_value; }
  V& GetValueAt(POSITION pos) { return ((CNode*)pos)->m_value; }

  UINT GetHashTableSize() const { return m_nBins; }
  bool InitHashTable(UINT nBins, bool bAllocNow = true) {
    if (m_nElements != 0) return false;
    delete[] m_ppBins;
    m_ppBins = NULL;
    m_nBins = nBins;
    if (bAllocNow) _AllocBins();
    return true;
  }
  void Rehash(UINT nBins = 0) {
    if (nBins == 0) nBins = _PickSize((size_t)(m_nElements / m_fOptimalLoad));
    if (nBins == m_nBins && m_ppBins != NULL) return;
    if (m_ppBins == NULL) {
      m_nBins = nBins;
      return;
    }
    CNode** ppNew = new CNode*[nBins]();
    for (UINT i = 0; i < m_nBins; i++) {
      CNode* p = m_ppBins[i];
      while (p != NULL) {
        CNode* pNext = p->m_pNext;
        UINT b = p->m_nHash % nBins;
        p->m_pNext = ppNew[b];
        ppNew[b] = p;
        p = pNext;
      }
    }
    delete[] m_ppBins;
    m_ppBins = ppNew;
    m_nBins = nBins;
  }
  void SetOptimalLoad(float fOptimalLoad, float fLoThreshold, float fHiThreshold,
                      bool bRehashNow = false) {
    m_fOptimalLoad = fOptimalLoad;
    m_fLoThreshold = fLoThreshold;
    m_fHiThreshold = fHiThreshold;
    if (bRehashNow) Rehash();
  }
  void DisableAutoRehash() { m_nLockCount++; }
  void EnableAutoRehash() {
    if (m_nLockCount > 0) m_nLockCount--;
  }

private:
  CNode** m_ppBins;
  UINT m_nBins;
  size_t m_nElements;
  UINT m_nLockCount;
  float m_fOptimalLoad, m_fLoThreshold, m_fHiThreshold;

  void _AllocBins() { m_ppBins = new CNode*[m_nBins](); }
  static UINT _PickSize(size_t n) {
    static const UINT primes[] = {17, 23, 29, 37, 41, 53, 67, 83, 103, 131, 163, 211, 257, 331,
                                  409, 521, 647, 821, 1031, 1291, 1627, 2053, 2591, 3251, 4099,
                                  5167, 6521, 8209, 10331, 13007, 16411, 20663, 26017, 32771,
                                  41299, 52021, 65537, 82571, 104033, 131101, 165161, 208067,
                                  262147, 330287, 416147, 524309, 660563, 832291, 1048583};
    for (UINT p : primes)
      if (p >= n) return p;
    return (UINT)(n | 1);
  }
  CNode* _Find(KINARGTYPE key) const {
    if (m_ppBins == NULL) return NULL;
    UINT nHash = (UINT)KTraits::Hash(key);
    for (CNode* p = m_ppBins[nHash % m_nBins]; p != NULL; p = p->m_pNext)
      if (p->m_nHash == nHash && KTraits::CompareElements(p->m_key, key)) return p;
    return NULL;
  }
  CNode* _FindOrAdd(KINARGTYPE key) {
    CNode* p = _Find(key);
    if (p != NULL) return p;
    if (m_ppBins == NULL) _AllocBins();
    UINT nHash = (UINT)KTraits::Hash(key);
    p = new CNode(key, nHash);
    UINT b = nHash % m_nBins;
    p->m_pNext = m_ppBins[b];
    m_ppBins[b] = p;
    m_nElements++;
    if (m_nLockCount == 0 && m_nElements > (size_t)(m_nBins * m_fHiThreshold)) Rehash();
    return p;
  }
  void _Remove(CNode** pp) {
    CNode* p = *pp;
    *pp = p->m_pNext;
    delete p;
    m_nElements--;
    if (m_nLockCount == 0 && m_nBins > 17 && m_nElements < (size_t)(m_nBins * m_fLoThreshold))
      Rehash();
  }
  POSITION _FirstFrom(UINT iBin) const {
    if (m_ppBins == NULL) return NULL;
    for (UINT i = iBin; i < m_nBins; i++)
      if (m_ppBins[i] != NULL) return m_ppBins[i];
    return NULL;
  }
  POSITION _Next(CNode* p) const {
    if (p->m_pNext != NULL) return p->m_pNext;
    return _FirstFrom(p->m_nHash % m_nBins + 1);
  }
};

// ---------------------------------------------------------------------
// CRBMap and CRBMultiMap: kept in key order.

template <typename K, typename V, class KTraits = CElementTraits<K>,
          class VTraits = CElementTraits<V>>
class CRBTree {
public:
  typedef typename KTraits::INARGTYPE KINARGTYPE;
  typedef typename KTraits::OUTARGTYPE KOUTARGTYPE;
  typedef typename VTraits::INARGTYPE VINARGTYPE;
  typedef typename VTraits::OUTARGTYPE VOUTARGTYPE;

  class CPair : public __POSITION {
  protected:
    CPair(KINARGTYPE key, VINARGTYPE value) : m_key(key), m_value(value) {}

  public:
    const K m_key;
    V m_value;
  };

protected:
  struct _Less {
    bool operator()(const K& a, const K& b) const { return KTraits::CompareElementsOrdered(a, b) < 0; }
  };
  class CNode;
  typedef std::multimap<K, CNode*, _Less> _Tree;
  class CNode : public CPair {
  public:
    CNode(KINARGTYPE key, VINARGTYPE value) : CPair(key, value) {}
    typename _Tree::iterator m_it;
  };

public:
  CRBTree() {}
  CRBTree(const CRBTree&) = delete;
  CRBTree& operator=(const CRBTree&) = delete;
  ~CRBTree() { RemoveAll(); }

  size_t GetCount() const { return m_tree.size(); }
  bool IsEmpty() const { return m_tree.empty(); }

  POSITION GetHeadPosition() const { return m_tree.empty() ? NULL : m_tree.begin()->second; }
  POSITION GetTailPosition() const { return m_tree.empty() ? NULL : m_tree.rbegin()->second; }
  POSITION FindFirstKeyAfter(KINARGTYPE key) const {
    auto it = m_tree.upper_bound(K(key));
    return it == m_tree.end() ? NULL : it->second;
  }

  CPair* GetNext(POSITION& pos) {
    CNode* p = (CNode*)pos;
    pos = _Next(p);
    return p;
  }
  const CPair* GetNext(POSITION& pos) const {
    CNode* p = (CNode*)pos;
    pos = _Next(p);
    return p;
  }
  CPair* GetPrev(POSITION& pos) {
    CNode* p = (CNode*)pos;
    pos = _Prev(p);
    return p;
  }
  const CPair* GetPrev(POSITION& pos) const {
    CNode* p = (CNode*)pos;
    pos = _Prev(p);
    return p;
  }
  void GetNextAssoc(POSITION& pos, KOUTARGTYPE key, VOUTARGTYPE value) const {
    CNode* p = (CNode*)pos;
    key = p->m_key;
    value = p->m_value;
    pos = _Next(p);
  }
  const K& GetNextKey(POSITION& pos) const { return GetNext(pos)->m_key; }
  const V& GetNextValue(POSITION& pos) const { return GetNext(pos)->m_value; }
  V& GetNextValue(POSITION& pos) { return GetNext(pos)->m_value; }
  CPair* GetAt(POSITION pos) { return (CNode*)pos; }
  const CPair* GetAt(POSITION pos) const { return (CNode*)pos; }
  void GetAt(POSITION pos, KOUTARGTYPE key, VOUTARGTYPE value) const {
    key = ((CNode*)pos)->m_key;
    value = ((CNode*)pos)->m_value;
  }
  const K& GetKeyAt(POSITION pos) const { return ((CNode*)pos)->m_key; }
  const V& GetValueAt(POSITION pos) const { return ((CNode*)pos)->m_value; }
  V& GetValueAt(POSITION pos) { return ((CNode*)pos)->m_value; }
  void SetValueAt(POSITION pos, VINARGTYPE value) { ((CNode*)pos)->m_value = value; }

  void RemoveAt(POSITION pos) {
    CNode* p = (CNode*)pos;
    m_tree.erase(p->m_it);
    delete p;
  }
  void RemoveAll() {
    for (auto& e : m_tree) delete e.second;
    m_tree.clear();
  }

protected:
  _Tree m_tree;

  CNode* _Insert(KINARGTYPE key, VINARGTYPE value) {
    CNode* p = new CNode(key, value);
    // Equal keys go after the ones there, in the order they came.
    p->m_it = m_tree.insert(m_tree.upper_bound(p->m_key), typename _Tree::value_type(p->m_key, p));
    return p;
  }
  CNode* _FindFirst(KINARGTYPE key) const {
    auto it = m_tree.find(K(key));
    return it == m_tree.end() ? NULL : it->second;
  }
  POSITION _Next(CNode* p) const {
    auto it = p->m_it;
    ++it;
    return it == m_tree.end() ? NULL : it->second;
  }
  POSITION _Prev(CNode* p) const {
    auto it = p->m_it;
    if (it == m_tree.begin()) return NULL;
    --it;
    return it->second;
  }
};

template <typename K, typename V, class KTraits = CElementTraits<K>,
          class VTraits = CElementTraits<V>>
class CRBMap : public CRBTree<K, V, KTraits, VTraits> {
  typedef CRBTree<K, V, KTraits, VTraits> _Base;

public:
  typedef typename _Base::KINARGTYPE KINARGTYPE;
  typedef typename _Base::VINARGTYPE VINARGTYPE;
  typedef typename _Base::VOUTARGTYPE VOUTARGTYPE;
  typedef typename _Base::CPair CPair;

  explicit CRBMap(size_t nBlockSize = 10) { (void)nBlockSize; }

  bool Lookup(KINARGTYPE key, VOUTARGTYPE value) const {
    auto* p = this->_FindFirst(key);
    if (p == NULL) return false;
    value = p->m_value;
    return true;
  }
  const CPair* Lookup(KINARGTYPE key) const { return this->_FindFirst(key); }
  CPair* Lookup(KINARGTYPE key) { return this->_FindFirst(key); }
  POSITION SetAt(KINARGTYPE key, VINARGTYPE value) {
    auto* p = this->_FindFirst(key);
    if (p != NULL) {
      p->m_value = value;
      return p;
    }
    return this->_Insert(key, value);
  }
  bool RemoveKey(KINARGTYPE key) {
    auto* p = this->_FindFirst(key);
    if (p == NULL) return false;
    this->RemoveAt(p);
    return true;
  }
};

template <typename K, typename V, class KTraits = CElementTraits<K>,
          class VTraits = CElementTraits<V>>
class CRBMultiMap : public CRBTree<K, V, KTraits, VTraits> {
  typedef CRBTree<K, V, KTraits, VTraits> _Base;

public:
  typedef typename _Base::KINARGTYPE KINARGTYPE;
  typedef typename _Base::VINARGTYPE VINARGTYPE;
  typedef typename _Base::CPair CPair;

  explicit CRBMultiMap(size_t nBlockSize = 10) { (void)nBlockSize; }

  POSITION Insert(KINARGTYPE key, VINARGTYPE value) { return this->_Insert(key, value); }
  size_t RemoveKey(KINARGTYPE key) {
    size_t n = 0;
    auto range = this->m_tree.equal_range(K(key));
    for (auto it = range.first; it != range.second;) {
      delete it->second;
      it = this->m_tree.erase(it);
      n++;
    }
    return n;
  }
  POSITION FindFirstWithKey(KINARGTYPE key) const { return this->_FindFirstEqual(key); }
  const CPair* GetNextWithKey(POSITION& pos, KINARGTYPE key) const {
    const CPair* p = this->GetNext(pos);
    if (pos != NULL && KTraits::CompareElementsOrdered(this->GetKeyAt(pos), key) != 0) pos = NULL;
    return p;
  }
  CPair* GetNextWithKey(POSITION& pos, KINARGTYPE key) {
    CPair* p = this->GetNext(pos);
    if (pos != NULL && KTraits::CompareElementsOrdered(this->GetKeyAt(pos), key) != 0) pos = NULL;
    return p;
  }
  const V& GetNextValueWithKey(POSITION& pos, KINARGTYPE key) const {
    return GetNextWithKey(pos, key)->m_value;
  }
  V& GetNextValueWithKey(POSITION& pos, KINARGTYPE key) { return GetNextWithKey(pos, key)->m_value; }

private:
  POSITION _FindFirstEqual(KINARGTYPE key) const {
    auto it = this->m_tree.lower_bound(K(key));
    if (it == this->m_tree.end() || KTraits::CompareElementsOrdered(it->first, key) != 0) return NULL;
    return it->second;
  }
};

}  // namespace ATL

#endif  // __ATLCOLL_H__
