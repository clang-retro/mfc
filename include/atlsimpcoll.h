// atlsimpcoll.h - ATL 3's simple collections: CSimpleArray, CSimpleValArray
// and CSimpleMap. Elements are constructed in place and compared with ==
// (or the helper given).
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#ifndef __ATLSIMPCOLL_H__
#define __ATLSIMPCOLL_H__

#include <windows.h>
#include <stdlib.h>
#include <string.h>
#include <new>

namespace ATL {

template <class T>
class CSimpleArrayEqualHelper {
public:
  static bool IsEqual(const T& t1, const T& t2) { return t1 == t2; }
};
template <class T>
class CSimpleArrayEqualHelperFalse {
public:
  static bool IsEqual(const T&, const T&) { return false; }
};

template <class TKey, class TVal>
class CSimpleMapEqualHelper {
public:
  static bool IsEqualKey(const TKey& k1, const TKey& k2) { return CSimpleArrayEqualHelper<TKey>::IsEqual(k1, k2); }
  static bool IsEqualValue(const TVal& v1, const TVal& v2) {
    return CSimpleArrayEqualHelper<TVal>::IsEqual(v1, v2);
  }
};
template <class TKey, class TVal>
class CSimpleMapEqualHelperFalse {
public:
  static bool IsEqualKey(const TKey& k1, const TKey& k2) { return CSimpleArrayEqualHelper<TKey>::IsEqual(k1, k2); }
  static bool IsEqualValue(const TVal&, const TVal&) { return false; }
};

// A growable array. m_aT, m_nSize and m_nAllocSize are public, as code
// written for ATL 3 reads them.
template <class T, class TEqual = CSimpleArrayEqualHelper<T>>
class CSimpleArray {
public:
  T* m_aT;
  int m_nSize;
  int m_nAllocSize;

  CSimpleArray() : m_aT(NULL), m_nSize(0), m_nAllocSize(0) {}
  CSimpleArray(const CSimpleArray<T, TEqual>& src) : m_aT(NULL), m_nSize(0), m_nAllocSize(0) {
    *this = src;
  }
  ~CSimpleArray() { RemoveAll(); }

  CSimpleArray<T, TEqual>& operator=(const CSimpleArray<T, TEqual>& src) {
    if (this == &src) return *this;
    RemoveAll();
    if (src.m_nSize > 0 && _Grow(src.m_nSize)) {
      for (int i = 0; i < src.m_nSize; i++) new (m_aT + i) T(src.m_aT[i]);
      m_nSize = src.m_nSize;
    }
    return *this;
  }

  int GetSize() const { return m_nSize; }
  BOOL Add(const T& t) {
    if (m_nSize == m_nAllocSize) {
      // t may be one of ours: copy it before the array moves.
      T tCopy(t);
      if (!_Grow(m_nAllocSize == 0 ? 1 : m_nAllocSize * 2)) return FALSE;
      new (m_aT + m_nSize) T(tCopy);
    } else {
      new (m_aT + m_nSize) T(t);
    }
    m_nSize++;
    return TRUE;
  }
  BOOL Remove(const T& t) {
    int nIndex = Find(t);
    return nIndex == -1 ? FALSE : RemoveAt(nIndex);
  }
  BOOL RemoveAt(int nIndex) {
    if (nIndex < 0 || nIndex >= m_nSize) return FALSE;
    m_aT[nIndex].~T();
    if (nIndex != m_nSize - 1)
      ::memmove((void*)(m_aT + nIndex), (void*)(m_aT + nIndex + 1),
                (size_t)(m_nSize - nIndex - 1) * sizeof(T));
    m_nSize--;
    return TRUE;
  }
  void RemoveAll() {
    if (m_aT != NULL) {
      for (int i = 0; i < m_nSize; i++) m_aT[i].~T();
      ::free(m_aT);
      m_aT = NULL;
    }
    m_nSize = 0;
    m_nAllocSize = 0;
  }
  const T& operator[](int nIndex) const { return m_aT[nIndex]; }
  T& operator[](int nIndex) { return m_aT[nIndex]; }
  T* GetData() const { return m_aT; }
  int Find(const T& t) const {
    for (int i = 0; i < m_nSize; i++)
      if (TEqual::IsEqual(m_aT[i], t)) return i;
    return -1;
  }
  BOOL SetAtIndex(int nIndex, const T& t) {
    if (nIndex < 0 || nIndex >= m_nSize) return FALSE;
    m_aT[nIndex] = t;
    return TRUE;
  }

private:
  // Elements move by their bytes, as in ATL.
  bool _Grow(int nNewAlloc) {
    T* aT = (T*)::realloc((void*)m_aT, (size_t)nNewAlloc * sizeof(T));
    if (aT == NULL) return false;
    m_aT = aT;
    m_nAllocSize = nNewAlloc;
    return true;
  }
};

// The same for values (ATL 3 kept a separate class for them).
template <class T, class TEqual = CSimpleArrayEqualHelper<T>>
class CSimpleValArray : public CSimpleArray<T, TEqual> {
public:
  BOOL Add(const T& t) { return CSimpleArray<T, TEqual>::Add(t); }
  BOOL Add(T t, int) { return CSimpleArray<T, TEqual>::Add(t); }
  T operator[](int nIndex) const { return this->m_aT[nIndex]; }
  T& operator[](int nIndex) { return this->m_aT[nIndex]; }
};

// Keys and values in two parallel arrays, looked up in order.
template <class TKey, class TVal, class TEqual = CSimpleMapEqualHelper<TKey, TVal>>
class CSimpleMap {
public:
  TKey* m_aKey;
  TVal* m_aVal;
  int m_nSize;

  typedef TKey _ArrayKeyType;
  typedef TVal _ArrayElementType;

  CSimpleMap() : m_aKey(NULL), m_aVal(NULL), m_nSize(0) {}
  CSimpleMap(const CSimpleMap&) = delete;
  CSimpleMap& operator=(const CSimpleMap&) = delete;
  ~CSimpleMap() { RemoveAll(); }

  int GetSize() const { return m_nSize; }
  BOOL Add(const TKey& key, const TVal& val) {
    TKey* pKey = (TKey*)::realloc((void*)m_aKey, (size_t)(m_nSize + 1) * sizeof(TKey));
    if (pKey == NULL) return FALSE;
    m_aKey = pKey;
    TVal* pVal = (TVal*)::realloc((void*)m_aVal, (size_t)(m_nSize + 1) * sizeof(TVal));
    if (pVal == NULL) return FALSE;
    m_aVal = pVal;
    new (m_aKey + m_nSize) TKey(key);
    new (m_aVal + m_nSize) TVal(val);
    m_nSize++;
    return TRUE;
  }
  BOOL Remove(const TKey& key) {
    int nIndex = FindKey(key);
    return nIndex == -1 ? FALSE : RemoveAt(nIndex);
  }
  BOOL RemoveAt(int nIndex) {
    if (nIndex < 0 || nIndex >= m_nSize) return FALSE;
    m_aKey[nIndex].~TKey();
    m_aVal[nIndex].~TVal();
    if (nIndex != m_nSize - 1) {
      ::memmove((void*)(m_aKey + nIndex), (void*)(m_aKey + nIndex + 1),
                (size_t)(m_nSize - nIndex - 1) * sizeof(TKey));
      ::memmove((void*)(m_aVal + nIndex), (void*)(m_aVal + nIndex + 1),
                (size_t)(m_nSize - nIndex - 1) * sizeof(TVal));
    }
    m_nSize--;
    return TRUE;
  }
  void RemoveAll() {
    for (int i = 0; i < m_nSize; i++) {
      m_aKey[i].~TKey();
      m_aVal[i].~TVal();
    }
    ::free(m_aKey);
    ::free(m_aVal);
    m_aKey = NULL;
    m_aVal = NULL;
    m_nSize = 0;
  }
  BOOL SetAt(const TKey& key, const TVal& val) {
    int nIndex = FindKey(key);
    if (nIndex == -1) return FALSE;
    m_aVal[nIndex] = val;
    return TRUE;
  }
  // The value for key, or a value-initialized one.
  TVal Lookup(const TKey& key) const {
    int nIndex = FindKey(key);
    return nIndex == -1 ? TVal() : m_aVal[nIndex];
  }
  TKey ReverseLookup(const TVal& val) const {
    int nIndex = FindVal(val);
    return nIndex == -1 ? TKey() : m_aKey[nIndex];
  }
  TKey& GetKeyAt(int nIndex) const { return m_aKey[nIndex]; }
  TVal& GetValueAt(int nIndex) const { return m_aVal[nIndex]; }
  int FindKey(const TKey& key) const {
    for (int i = 0; i < m_nSize; i++)
      if (TEqual::IsEqualKey(m_aKey[i], key)) return i;
    return -1;
  }
  int FindVal(const TVal& val) const {
    for (int i = 0; i < m_nSize; i++)
      if (TEqual::IsEqualValue(m_aVal[i], val)) return i;
    return -1;
  }
  BOOL SetAtIndex(int nIndex, const TKey& key, const TVal& val) {
    if (nIndex < 0 || nIndex >= m_nSize) return FALSE;
    m_aKey[nIndex] = key;
    m_aVal[nIndex] = val;
    return TRUE;
  }
};

}  // namespace ATL

#endif  // __ATLSIMPCOLL_H__
