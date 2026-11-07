// afxtempl.h - collection templates: CArray, CList, CMap and the typed
// pointer wrappers.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#ifndef __AFXTEMPL_H__
#define __AFXTEMPL_H__

#ifndef __AFX_H__
#include <afx.h>
#endif
#include <type_traits>

// ---------------------------------------------------------------------
// Element helpers. Programs may specialise these for their own types.

template <class TYPE>
inline void AFXAPI ConstructElements(TYPE* pElements, INT_PTR nCount) {
  for (; nCount--; pElements++) ::new ((void*)pElements) TYPE();
}

template <class TYPE>
inline void AFXAPI DestructElements(TYPE* pElements, INT_PTR nCount) {
  for (; nCount--; pElements++) pElements->~TYPE();
}

template <class TYPE>
inline void AFXAPI CopyElements(TYPE* pDest, const TYPE* pSrc, INT_PTR nCount) {
  while (nCount--) *pDest++ = *pSrc++;
}

template <class TYPE, bool Raw> struct _AfxSerializer;
template <class TYPE> struct _AfxSerializer<TYPE, true> {
  static void Do(CArchive& ar, TYPE* p, INT_PTR n) {
    if (ar.IsStoring())
      ar.Write((void*)p, (UINT)(n * sizeof(TYPE)));
    else
      ar.EnsureRead((void*)p, (UINT)(n * sizeof(TYPE)));
  }
};
template <class TYPE> struct _AfxSerializer<TYPE, false> {
  static void Do(CArchive& ar, TYPE* p, INT_PTR n) {
    if (ar.IsStoring())
      for (; n--; p++) ar << *p;
    else
      for (; n--; p++) ar >> *p;
  }
};
template <class TYPE> struct _AfxIsObPtr { enum { value = 0 }; };
template <class T> struct _AfxIsObPtr<T*> {
  enum { value = std::is_base_of<CObject, T>::value };
};

template <class TYPE>
void AFXAPI SerializeElements(CArchive& ar, TYPE* pElements, INT_PTR nCount) {
  _AfxSerializer<TYPE, std::is_trivially_copyable<TYPE>::value &&
                           !_AfxIsObPtr<TYPE>::value>::Do(ar, pElements, nCount);
}

template <class TYPE>
inline void AFXAPI DumpElements(CDumpContext&, const TYPE*, INT_PTR) {}

template <class TYPE, class ARG_TYPE>
inline BOOL AFXAPI CompareElements(const TYPE* pElement1, const ARG_TYPE* pElement2) {
  return *pElement1 == *pElement2;
}

template <class ARG_KEY>
inline UINT AFXAPI HashKey(ARG_KEY key) {
  return (UINT)(((UINT_PTR)key) >> 4);
}
template <> inline UINT AFXAPI HashKey<LPCSTR>(LPCSTR key) {
  UINT nHash = 0;
  if (key) while (*key) nHash = (nHash << 5) + nHash + (unsigned char)*key++;
  return nHash;
}
template <> inline UINT AFXAPI HashKey<LPSTR>(LPSTR key) { return HashKey<LPCSTR>(key); }
template <> inline UINT AFXAPI HashKey<LPCWSTR>(LPCWSTR key) {
  UINT nHash = 0;
  if (key) while (*key) nHash = (nHash << 5) + nHash + *key++;
  return nHash;
}
template <> inline UINT AFXAPI HashKey<LPWSTR>(LPWSTR key) { return HashKey<LPCWSTR>(key); }
template <> inline UINT AFXAPI HashKey<const CStringA&>(const CStringA& key) {
  return HashKey<LPCSTR>(key);
}
template <> inline UINT AFXAPI HashKey<const CStringW&>(const CStringW& key) {
  return HashKey<LPCWSTR>(key);
}
template <> inline UINT AFXAPI HashKey<CStringA>(CStringA key) { return HashKey<LPCSTR>(key); }
template <> inline UINT AFXAPI HashKey<CStringW>(CStringW key) { return HashKey<LPCWSTR>(key); }
template <> inline UINT AFXAPI HashKey<__int64>(__int64 key) {
  return HashKey<DWORD>((DWORD)(key ^ (key >> 32)));
}
template <> inline UINT AFXAPI HashKey<unsigned __int64>(unsigned __int64 key) {
  return HashKey<DWORD>((DWORD)(key ^ (key >> 32)));
}

// ---------------------------------------------------------------------
// CArray

template <class TYPE, class ARG_TYPE = const TYPE&>
class CArray : public CObject {
public:
  CArray() : m_pData(NULL), m_nSize(0), m_nMaxSize(0), m_nGrowBy(0) {}
  ~CArray() {
    if (m_pData) {
      DestructElements(m_pData, m_nSize);
      ::operator delete((void*)m_pData);
    }
  }

  INT_PTR GetSize() const { return m_nSize; }
  INT_PTR GetCount() const { return m_nSize; }
  BOOL IsEmpty() const { return m_nSize == 0; }
  INT_PTR GetUpperBound() const { return m_nSize - 1; }
  void SetSize(INT_PTR nNewSize, INT_PTR nGrowBy = -1);

  void FreeExtra();
  void RemoveAll() { SetSize(0, -1); }

  const TYPE& GetAt(INT_PTR nIndex) const {
    if (nIndex < 0 || nIndex >= m_nSize) AfxThrowInvalidArgException();
    return m_pData[nIndex];
  }
  TYPE& GetAt(INT_PTR nIndex) {
    if (nIndex < 0 || nIndex >= m_nSize) AfxThrowInvalidArgException();
    return m_pData[nIndex];
  }
  void SetAt(INT_PTR nIndex, ARG_TYPE newElement) {
    if (nIndex < 0 || nIndex >= m_nSize) AfxThrowInvalidArgException();
    m_pData[nIndex] = newElement;
  }
  const TYPE& ElementAt(INT_PTR nIndex) const { return GetAt(nIndex); }
  TYPE& ElementAt(INT_PTR nIndex) { return GetAt(nIndex); }

  const TYPE* GetData() const { return m_pData; }
  TYPE* GetData() { return m_pData; }

  void SetAtGrow(INT_PTR nIndex, ARG_TYPE newElement);
  INT_PTR Add(ARG_TYPE newElement) {
    INT_PTR nIndex = m_nSize;
    SetAtGrow(nIndex, newElement);
    return nIndex;
  }
  INT_PTR Append(const CArray& src);
  void Copy(const CArray& src);

  const TYPE& operator[](INT_PTR nIndex) const { return GetAt(nIndex); }
  TYPE& operator[](INT_PTR nIndex) { return ElementAt(nIndex); }

  void InsertAt(INT_PTR nIndex, ARG_TYPE newElement, INT_PTR nCount = 1);
  void RemoveAt(INT_PTR nIndex, INT_PTR nCount = 1);
  void InsertAt(INT_PTR nStartIndex, CArray* pNewArray);

  void Serialize(CArchive& ar);
  void AssertValid() const {}

protected:
  TYPE* m_pData;
  INT_PTR m_nSize;
  INT_PTR m_nMaxSize;
  INT_PTR m_nGrowBy;

  // Moves n elements from src to raw memory dst and destroys the sources.
  static void MoveElements(TYPE* dst, TYPE* src, INT_PTR n) {
    if (std::is_trivially_copyable<TYPE>::value) {
      memmove((void*)dst, (void*)src, n * sizeof(TYPE));
    } else if (dst < src) {
      for (INT_PTR i = 0; i < n; i++) {
        ::new ((void*)(dst + i)) TYPE(src[i]);
        src[i].~TYPE();
      }
    } else {
      for (INT_PTR i = n; i-- > 0;) {
        ::new ((void*)(dst + i)) TYPE(src[i]);
        src[i].~TYPE();
      }
    }
  }
  void Reserve(INT_PTR nNewMax) {
    TYPE* pNew = (TYPE*)::operator new(nNewMax * sizeof(TYPE));
    if (m_pData) {
      MoveElements(pNew, m_pData, m_nSize);
      ::operator delete((void*)m_pData);
    }
    m_pData = pNew;
    m_nMaxSize = nNewMax;
  }
};

template <class TYPE, class ARG_TYPE>
void CArray<TYPE, ARG_TYPE>::SetSize(INT_PTR nNewSize, INT_PTR nGrowBy) {
  if (nNewSize < 0) AfxThrowInvalidArgException();
  if (nGrowBy >= 0) m_nGrowBy = nGrowBy;
  if (nNewSize == 0) {
    if (m_pData) {
      DestructElements(m_pData, m_nSize);
      ::operator delete((void*)m_pData);
      m_pData = NULL;
    }
    m_nSize = m_nMaxSize = 0;
  } else if (nNewSize <= m_nMaxSize) {
    if (nNewSize > m_nSize)
      ConstructElements(m_pData + m_nSize, nNewSize - m_nSize);
    else if (m_nSize > nNewSize)
      DestructElements(m_pData + nNewSize, m_nSize - nNewSize);
    m_nSize = nNewSize;
  } else {
    INT_PTR grow = m_nGrowBy;
    if (grow == 0) {
      grow = m_nSize / 8;
      grow = grow < 4 ? 4 : (grow > 1024 ? 1024 : grow);
    }
    INT_PTR nNewMax = m_nMaxSize + grow;
    if (nNewSize > nNewMax) nNewMax = nNewSize;
    Reserve(nNewMax);
    ConstructElements(m_pData + m_nSize, nNewSize - m_nSize);
    m_nSize = nNewSize;
  }
}

template <class TYPE, class ARG_TYPE>
void CArray<TYPE, ARG_TYPE>::FreeExtra() {
  if (m_nSize != m_nMaxSize) {
    if (m_nSize == 0) {
      ::operator delete((void*)m_pData);
      m_pData = NULL;
      m_nMaxSize = 0;
    } else {
      Reserve(m_nSize);
    }
  }
}

template <class TYPE, class ARG_TYPE>
void CArray<TYPE, ARG_TYPE>::SetAtGrow(INT_PTR nIndex, ARG_TYPE newElement) {
  if (nIndex < 0) AfxThrowInvalidArgException();
  if (nIndex >= m_nSize) {
    // newElement may refer into our own buffer, which SetSize can move.
    TYPE tmp(newElement);
    SetSize(nIndex + 1, -1);
    m_pData[nIndex] = tmp;
  } else {
    m_pData[nIndex] = newElement;
  }
}

template <class TYPE, class ARG_TYPE>
INT_PTR CArray<TYPE, ARG_TYPE>::Append(const CArray& src) {
  if (this == &src) AfxThrowInvalidArgException();
  INT_PTR nOldSize = m_nSize;
  SetSize(m_nSize + src.m_nSize);
  CopyElements<TYPE>(m_pData + nOldSize, src.m_pData, src.m_nSize);
  return nOldSize;
}

template <class TYPE, class ARG_TYPE>
void CArray<TYPE, ARG_TYPE>::Copy(const CArray& src) {
  if (this != &src) {
    SetSize(src.m_nSize);
    CopyElements<TYPE>(m_pData, src.m_pData, src.m_nSize);
  }
}

template <class TYPE, class ARG_TYPE>
void CArray<TYPE, ARG_TYPE>::InsertAt(INT_PTR nIndex, ARG_TYPE newElement,
                                      INT_PTR nCount) {
  if (nIndex < 0 || nCount <= 0) AfxThrowInvalidArgException();
  TYPE tmp(newElement);
  if (nIndex >= m_nSize) {
    SetSize(nIndex + nCount, -1);
  } else {
    INT_PTR nOldSize = m_nSize;
    SetSize(m_nSize + nCount, -1);
    // Drop the new default elements at the end, then shift up.
    DestructElements(m_pData + nOldSize, nCount);
    MoveElements(m_pData + nIndex + nCount, m_pData + nIndex, nOldSize - nIndex);
    ConstructElements(m_pData + nIndex, nCount);
  }
  while (nCount--) m_pData[nIndex++] = tmp;
}

template <class TYPE, class ARG_TYPE>
void CArray<TYPE, ARG_TYPE>::RemoveAt(INT_PTR nIndex, INT_PTR nCount) {
  if (nIndex < 0 || nCount < 0 || nIndex + nCount > m_nSize)
    AfxThrowInvalidArgException();
  INT_PTR nMoveCount = m_nSize - (nIndex + nCount);
  DestructElements(m_pData + nIndex, nCount);
  if (nMoveCount) MoveElements(m_pData + nIndex, m_pData + nIndex + nCount, nMoveCount);
  m_nSize -= nCount;
}

template <class TYPE, class ARG_TYPE>
void CArray<TYPE, ARG_TYPE>::InsertAt(INT_PTR nStartIndex, CArray* pNewArray) {
  if (pNewArray == NULL || nStartIndex < 0) AfxThrowInvalidArgException();
  if (pNewArray->GetSize() > 0) {
    InsertAt(nStartIndex, pNewArray->GetAt(0), pNewArray->GetSize());
    for (INT_PTR i = 0; i < pNewArray->GetSize(); i++)
      SetAt(nStartIndex + i, pNewArray->GetAt(i));
  }
}

template <class TYPE, class ARG_TYPE>
void CArray<TYPE, ARG_TYPE>::Serialize(CArchive& ar) {
  CObject::Serialize(ar);
  if (ar.IsStoring()) {
    ar.WriteCount(m_nSize);
  } else {
    DWORD_PTR nOldSize = ar.ReadCount();
    SetSize(nOldSize, -1);
  }
  SerializeElements<TYPE>(ar, m_pData, m_nSize);
}

// ---------------------------------------------------------------------
// CList

template <class TYPE, class ARG_TYPE = const TYPE&>
class CList : public CObject {
protected:
  struct CNode {
    CNode* pNext;
    CNode* pPrev;
    TYPE data;
  };

public:
  explicit CList(INT_PTR nBlockSize = 10)
      : m_pNodeHead(NULL), m_pNodeTail(NULL), m_nCount(0), m_nBlockSize(nBlockSize) {}
  ~CList() { RemoveAll(); }

  INT_PTR GetCount() const { return m_nCount; }
  INT_PTR GetSize() const { return m_nCount; }
  BOOL IsEmpty() const { return m_nCount == 0; }

  TYPE& GetHead() { Check(m_pNodeHead); return m_pNodeHead->data; }
  const TYPE& GetHead() const { Check(m_pNodeHead); return m_pNodeHead->data; }
  TYPE& GetTail() { Check(m_pNodeTail); return m_pNodeTail->data; }
  const TYPE& GetTail() const { Check(m_pNodeTail); return m_pNodeTail->data; }

  TYPE RemoveHead() {
    Check(m_pNodeHead);
    CNode* p = m_pNodeHead;
    TYPE ret = p->data;
    m_pNodeHead = p->pNext;
    if (m_pNodeHead) m_pNodeHead->pPrev = NULL; else m_pNodeTail = NULL;
    FreeNode(p);
    return ret;
  }
  TYPE RemoveTail() {
    Check(m_pNodeTail);
    CNode* p = m_pNodeTail;
    TYPE ret = p->data;
    m_pNodeTail = p->pPrev;
    if (m_pNodeTail) m_pNodeTail->pNext = NULL; else m_pNodeHead = NULL;
    FreeNode(p);
    return ret;
  }

  POSITION AddHead(ARG_TYPE newElement) {
    CNode* p = NewNode(NULL, m_pNodeHead, newElement);
    if (m_pNodeHead) m_pNodeHead->pPrev = p; else m_pNodeTail = p;
    m_pNodeHead = p;
    return (POSITION)p;
  }
  POSITION AddTail(ARG_TYPE newElement) {
    CNode* p = NewNode(m_pNodeTail, NULL, newElement);
    if (m_pNodeTail) m_pNodeTail->pNext = p; else m_pNodeHead = p;
    m_pNodeTail = p;
    return (POSITION)p;
  }
  void AddHead(CList* pNewList) {
    POSITION pos = pNewList->GetTailPosition();
    while (pos) AddHead(pNewList->GetPrev(pos));
  }
  void AddTail(CList* pNewList) {
    POSITION pos = pNewList->GetHeadPosition();
    while (pos) AddTail(pNewList->GetNext(pos));
  }

  void RemoveAll() {
    CNode* p = m_pNodeHead;
    while (p) {
      CNode* n = p->pNext;
      p->data.~TYPE();
      ::operator delete((void*)p);
      p = n;
    }
    m_pNodeHead = m_pNodeTail = NULL;
    m_nCount = 0;
  }

  POSITION GetHeadPosition() const { return (POSITION)m_pNodeHead; }
  POSITION GetTailPosition() const { return (POSITION)m_pNodeTail; }
  TYPE& GetNext(POSITION& rPosition) {
    CNode* p = (CNode*)rPosition;
    rPosition = (POSITION)p->pNext;
    return p->data;
  }
  const TYPE& GetNext(POSITION& rPosition) const {
    CNode* p = (CNode*)rPosition;
    rPosition = (POSITION)p->pNext;
    return p->data;
  }
  TYPE& GetPrev(POSITION& rPosition) {
    CNode* p = (CNode*)rPosition;
    rPosition = (POSITION)p->pPrev;
    return p->data;
  }
  const TYPE& GetPrev(POSITION& rPosition) const {
    CNode* p = (CNode*)rPosition;
    rPosition = (POSITION)p->pPrev;
    return p->data;
  }
  TYPE& GetAt(POSITION position) { return ((CNode*)position)->data; }
  const TYPE& GetAt(POSITION position) const { return ((CNode*)position)->data; }
  void SetAt(POSITION pos, ARG_TYPE newElement) { ((CNode*)pos)->data = newElement; }
  void RemoveAt(POSITION position) {
    CNode* p = (CNode*)position;
    if (p == m_pNodeHead) m_pNodeHead = p->pNext; else p->pPrev->pNext = p->pNext;
    if (p == m_pNodeTail) m_pNodeTail = p->pPrev; else p->pNext->pPrev = p->pPrev;
    FreeNode(p);
  }

  POSITION InsertBefore(POSITION position, ARG_TYPE newElement) {
    if (position == NULL) return AddHead(newElement);
    CNode* pOld = (CNode*)position;
    CNode* p = NewNode(pOld->pPrev, pOld, newElement);
    if (pOld->pPrev) pOld->pPrev->pNext = p; else m_pNodeHead = p;
    pOld->pPrev = p;
    return (POSITION)p;
  }
  POSITION InsertAfter(POSITION position, ARG_TYPE newElement) {
    if (position == NULL) return AddTail(newElement);
    CNode* pOld = (CNode*)position;
    CNode* p = NewNode(pOld, pOld->pNext, newElement);
    if (pOld->pNext) pOld->pNext->pPrev = p; else m_pNodeTail = p;
    pOld->pNext = p;
    return (POSITION)p;
  }

  POSITION Find(ARG_TYPE searchValue, POSITION startAfter = NULL) const {
    CNode* p = startAfter ? ((CNode*)startAfter)->pNext : m_pNodeHead;
    for (; p; p = p->pNext)
      if (CompareElements<TYPE>(&p->data, &searchValue)) return (POSITION)p;
    return NULL;
  }
  POSITION FindIndex(INT_PTR nIndex) const {
    if (nIndex >= m_nCount || nIndex < 0) return NULL;
    CNode* p = m_pNodeHead;
    while (nIndex--) p = p->pNext;
    return (POSITION)p;
  }

  void Serialize(CArchive& ar) {
    CObject::Serialize(ar);
    if (ar.IsStoring()) {
      ar.WriteCount(m_nCount);
      for (CNode* p = m_pNodeHead; p; p = p->pNext)
        SerializeElements<TYPE>(ar, &p->data, 1);
    } else {
      DWORD_PTR n = ar.ReadCount();
      while (n--) {
        TYPE newData = TYPE();
        SerializeElements<TYPE>(ar, &newData, 1);
        AddTail(newData);
      }
    }
  }
  void AssertValid() const {}

protected:
  CNode* m_pNodeHead;
  CNode* m_pNodeTail;
  INT_PTR m_nCount;
  INT_PTR m_nBlockSize;

  static void Check(const CNode* p) { if (!p) AfxThrowInvalidArgException(); }
  CNode* NewNode(CNode* pPrev, CNode* pNext, ARG_TYPE value) {
    CNode* p = (CNode*)::operator new(sizeof(CNode));
    ::new ((void*)&p->data) TYPE(value);
    p->pPrev = pPrev;
    p->pNext = pNext;
    m_nCount++;
    return p;
  }
  void FreeNode(CNode* p) {
    p->data.~TYPE();
    ::operator delete((void*)p);
    m_nCount--;
  }
};

// ---------------------------------------------------------------------
// CMap

template <class KEY, class ARG_KEY, class VALUE, class ARG_VALUE>
class CMap : public CObject {
public:
  class CPair {
  public:
    const KEY key;
    VALUE value;

  protected:
    CPair(ARG_KEY keyval) : key(keyval), value() {}
  };

protected:
  class CAssoc : public CPair {
    friend class CMap<KEY, ARG_KEY, VALUE, ARG_VALUE>;
    CAssoc* pNext;
    UINT nHashValue;

  public:
    CAssoc(ARG_KEY key) : CPair(key), pNext(NULL), nHashValue(0) {}
  };

public:
  explicit CMap(INT_PTR nBlockSize = 10)
      : m_pHashTable(NULL), m_nHashTableSize(17), m_nCount(0), m_nBlockSize(nBlockSize) {}
  ~CMap() { RemoveAll(); }

  INT_PTR GetCount() const { return m_nCount; }
  INT_PTR GetSize() const { return m_nCount; }
  BOOL IsEmpty() const { return m_nCount == 0; }

  BOOL Lookup(ARG_KEY key, VALUE& rValue) const {
    UINT nHashBucket, nHashValue;
    CAssoc* p = GetAssocAt(key, nHashBucket, nHashValue);
    if (!p) return FALSE;
    rValue = p->value;
    return TRUE;
  }
  const CPair* PLookup(ARG_KEY key) const {
    UINT b, h;
    return GetAssocAt(key, b, h);
  }
  CPair* PLookup(ARG_KEY key) {
    UINT b, h;
    return GetAssocAt(key, b, h);
  }

  VALUE& operator[](ARG_KEY key) {
    UINT nHashBucket, nHashValue;
    CAssoc* p = GetAssocAt(key, nHashBucket, nHashValue);
    if (p == NULL) {
      if (m_pHashTable == NULL) {
        InitHashTable(m_nHashTableSize);
        nHashBucket = nHashValue % m_nHashTableSize;
      }
      p = new CAssoc(key);
      p->nHashValue = nHashValue;
      p->pNext = m_pHashTable[nHashBucket];
      m_pHashTable[nHashBucket] = p;
      m_nCount++;
    }
    return p->value;
  }
  void SetAt(ARG_KEY key, ARG_VALUE newValue) { (*this)[key] = newValue; }

  BOOL RemoveKey(ARG_KEY key) {
    if (m_pHashTable == NULL) return FALSE;
    UINT nHashValue = HashKey<ARG_KEY>(key);
    CAssoc** ppPrev = &m_pHashTable[nHashValue % m_nHashTableSize];
    for (CAssoc* p = *ppPrev; p; p = p->pNext) {
      if (p->nHashValue == nHashValue && CompareElements(&p->key, &key)) {
        *ppPrev = p->pNext;
        delete p;
        m_nCount--;
        return TRUE;
      }
      ppPrev = &p->pNext;
    }
    return FALSE;
  }
  void RemoveAll() {
    if (m_pHashTable) {
      for (UINT i = 0; i < m_nHashTableSize; i++) {
        CAssoc* p = m_pHashTable[i];
        while (p) {
          CAssoc* n = p->pNext;
          delete p;
          p = n;
        }
      }
      delete[] m_pHashTable;
      m_pHashTable = NULL;
    }
    m_nCount = 0;
  }

  POSITION GetStartPosition() const {
    return m_nCount == 0 ? NULL : BEFORE_START_POSITION;
  }
  void GetNextAssoc(POSITION& rNextPosition, KEY& rKey, VALUE& rValue) const {
    CAssoc* p = (CAssoc*)rNextPosition;
    if (p == (CAssoc*)BEFORE_START_POSITION) p = FirstAssoc();
    rNextPosition = (POSITION)NextAssoc(p);
    rKey = p->key;
    rValue = p->value;
  }
  const CPair* PGetFirstAssoc() const { return FirstAssoc(); }
  CPair* PGetFirstAssoc() { return FirstAssoc(); }
  const CPair* PGetNextAssoc(const CPair* pAssoc) const { return NextAssoc((CAssoc*)pAssoc); }
  CPair* PGetNextAssoc(const CPair* pAssoc) { return NextAssoc((CAssoc*)pAssoc); }

  UINT GetHashTableSize() const { return m_nHashTableSize; }
  void InitHashTable(UINT hashSize, BOOL bAllocNow = TRUE) {
    if (m_pHashTable) {
      // Rehash what is there into the new table.
      CAssoc** pOld = m_pHashTable;
      UINT nOld = m_nHashTableSize;
      m_pHashTable = new CAssoc*[hashSize]();
      m_nHashTableSize = hashSize;
      for (UINT i = 0; i < nOld; i++) {
        CAssoc* p = pOld[i];
        while (p) {
          CAssoc* n = p->pNext;
          UINT b = p->nHashValue % hashSize;
          p->pNext = m_pHashTable[b];
          m_pHashTable[b] = p;
          p = n;
        }
      }
      delete[] pOld;
      return;
    }
    m_nHashTableSize = hashSize;
    if (bAllocNow) m_pHashTable = new CAssoc*[hashSize]();
  }

  void Serialize(CArchive& ar) {
    CObject::Serialize(ar);
    if (ar.IsStoring()) {
      ar.WriteCount(m_nCount);
      for (CAssoc* p = FirstAssoc(); p; p = NextAssoc(p)) {
        SerializeElements<KEY>(ar, (KEY*)&p->key, 1);
        SerializeElements<VALUE>(ar, &p->value, 1);
      }
    } else {
      DWORD_PTR nNewCount = ar.ReadCount();
      while (nNewCount--) {
        KEY newKey = KEY();
        VALUE newValue = VALUE();
        SerializeElements<KEY>(ar, &newKey, 1);
        SerializeElements<VALUE>(ar, &newValue, 1);
        SetAt(newKey, newValue);
      }
    }
  }
  void AssertValid() const {}

protected:
  CAssoc** m_pHashTable;
  UINT m_nHashTableSize;
  INT_PTR m_nCount;
  INT_PTR m_nBlockSize;

  CAssoc* GetAssocAt(ARG_KEY key, UINT& nHashBucket, UINT& nHashValue) const {
    nHashValue = HashKey<ARG_KEY>(key);
    nHashBucket = nHashValue % m_nHashTableSize;
    if (m_pHashTable == NULL) return NULL;
    for (CAssoc* p = m_pHashTable[nHashBucket]; p; p = p->pNext)
      if (p->nHashValue == nHashValue && CompareElements(&p->key, &key)) return p;
    return NULL;
  }
  CAssoc* FirstAssoc() const {
    if (!m_pHashTable) return NULL;
    for (UINT i = 0; i < m_nHashTableSize; i++)
      if (m_pHashTable[i]) return m_pHashTable[i];
    return NULL;
  }
  CAssoc* NextAssoc(CAssoc* p) const {
    if (p->pNext) return p->pNext;
    for (UINT i = p->nHashValue % m_nHashTableSize + 1; i < m_nHashTableSize; i++)
      if (m_pHashTable[i]) return m_pHashTable[i];
    return NULL;
  }
};

// ---------------------------------------------------------------------
// Typed pointer collections

template <class BASE_CLASS, class TYPE>
class CTypedPtrArray : public BASE_CLASS {
public:
  TYPE GetAt(INT_PTR nIndex) const { return (TYPE)BASE_CLASS::GetAt(nIndex); }
  TYPE& ElementAt(INT_PTR nIndex) { return (TYPE&)BASE_CLASS::ElementAt(nIndex); }
  void SetAt(INT_PTR nIndex, TYPE ptr) { BASE_CLASS::SetAt(nIndex, ptr); }
  void SetAtGrow(INT_PTR nIndex, TYPE newElement) { BASE_CLASS::SetAtGrow(nIndex, newElement); }
  INT_PTR Add(TYPE newElement) { return BASE_CLASS::Add(newElement); }
  INT_PTR Append(const CTypedPtrArray<BASE_CLASS, TYPE>& src) { return BASE_CLASS::Append(src); }
  void Copy(const CTypedPtrArray<BASE_CLASS, TYPE>& src) { BASE_CLASS::Copy(src); }
  void InsertAt(INT_PTR nIndex, TYPE newElement, INT_PTR nCount = 1) {
    BASE_CLASS::InsertAt(nIndex, newElement, nCount);
  }
  void InsertAt(INT_PTR nStartIndex, CTypedPtrArray<BASE_CLASS, TYPE>* pNewArray) {
    BASE_CLASS::InsertAt(nStartIndex, pNewArray);
  }
  TYPE operator[](INT_PTR nIndex) const { return (TYPE)BASE_CLASS::operator[](nIndex); }
  TYPE& operator[](INT_PTR nIndex) { return (TYPE&)BASE_CLASS::operator[](nIndex); }
};

template <class BASE_CLASS, class TYPE>
class _CTypedPtrList : public BASE_CLASS {
public:
  explicit _CTypedPtrList(INT_PTR nBlockSize = 10) : BASE_CLASS(nBlockSize) {}
  TYPE& GetHead() { return (TYPE&)BASE_CLASS::GetHead(); }
  TYPE GetHead() const { return (TYPE)BASE_CLASS::GetHead(); }
  TYPE& GetTail() { return (TYPE&)BASE_CLASS::GetTail(); }
  TYPE GetTail() const { return (TYPE)BASE_CLASS::GetTail(); }
  TYPE RemoveHead() { return (TYPE)BASE_CLASS::RemoveHead(); }
  TYPE RemoveTail() { return (TYPE)BASE_CLASS::RemoveTail(); }
  TYPE& GetNext(POSITION& rPosition) { return (TYPE&)BASE_CLASS::GetNext(rPosition); }
  TYPE GetNext(POSITION& rPosition) const { return (TYPE)BASE_CLASS::GetNext(rPosition); }
  TYPE& GetPrev(POSITION& rPosition) { return (TYPE&)BASE_CLASS::GetPrev(rPosition); }
  TYPE GetPrev(POSITION& rPosition) const { return (TYPE)BASE_CLASS::GetPrev(rPosition); }
  TYPE& GetAt(POSITION position) { return (TYPE&)BASE_CLASS::GetAt(position); }
  TYPE GetAt(POSITION position) const { return (TYPE)BASE_CLASS::GetAt(position); }
  void SetAt(POSITION pos, TYPE newElement) { BASE_CLASS::SetAt(pos, newElement); }
};

template <class BASE_CLASS, class TYPE>
class CTypedPtrList : public _CTypedPtrList<BASE_CLASS, TYPE> {
public:
  explicit CTypedPtrList(INT_PTR nBlockSize = 10) : _CTypedPtrList<BASE_CLASS, TYPE>(nBlockSize) {}
  POSITION AddHead(TYPE newElement) { return BASE_CLASS::AddHead(newElement); }
  POSITION AddTail(TYPE newElement) { return BASE_CLASS::AddTail(newElement); }
  void AddHead(CTypedPtrList<BASE_CLASS, TYPE>* pNewList) { BASE_CLASS::AddHead(pNewList); }
  void AddTail(CTypedPtrList<BASE_CLASS, TYPE>* pNewList) { BASE_CLASS::AddTail(pNewList); }
  POSITION InsertBefore(POSITION p, TYPE e) { return BASE_CLASS::InsertBefore(p, e); }
  POSITION InsertAfter(POSITION p, TYPE e) { return BASE_CLASS::InsertAfter(p, e); }
};

template <class BASE_CLASS, class KEY, class VALUE>
class CTypedPtrMap : public BASE_CLASS {
public:
  explicit CTypedPtrMap(INT_PTR nBlockSize = 10) : BASE_CLASS(nBlockSize) {}
  BOOL Lookup(typename BASE_CLASS::BASE_ARG_KEY key, VALUE& rValue) const {
    typename BASE_CLASS::BASE_VALUE v;
    BOOL b = BASE_CLASS::Lookup(key, v);
    rValue = (VALUE)v;
    return b;
  }
  VALUE& operator[](typename BASE_CLASS::BASE_ARG_KEY key) {
    return (VALUE&)BASE_CLASS::operator[](key);
  }
  void SetAt(KEY key, VALUE newValue) { BASE_CLASS::SetAt(key, newValue); }
  BOOL RemoveKey(KEY key) { return BASE_CLASS::RemoveKey(key); }
  void GetNextAssoc(POSITION& rPosition, KEY& rKey, VALUE& rValue) const {
    typename BASE_CLASS::BASE_KEY k;
    typename BASE_CLASS::BASE_VALUE v;
    BASE_CLASS::GetNextAssoc(rPosition, k, v);
    rKey = (KEY)k;
    rValue = (VALUE)v;
  }
};

#endif // __AFXTEMPL_H__
