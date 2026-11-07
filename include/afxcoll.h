// afxcoll.h - the non-template collection classes.
//
// Each is a CArray, CList or CMap of the element type, with the runtime
// class and serialization that MFC gives it.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#ifndef __AFXCOLL_H__
#define __AFXCOLL_H__

#ifndef __AFX_H__
#include <afx.h>
#endif
#include <afxtempl.h>

#define _AFX_COLL_ARRAY(name, T, ARG) \
  class name : public CArray<T, ARG> { \
  public: \
    typedef T BASE_TYPE; \
    typedef ARG BASE_ARG_TYPE; \
    name() {} \
    virtual ~name() {}

#define _AFX_COLL_LIST(name, T, ARG) \
  class name : public CList<T, ARG> { \
  public: \
    typedef T BASE_TYPE; \
    typedef ARG BASE_ARG_TYPE; \
    explicit name(INT_PTR nBlockSize = 10) : CList<T, ARG>(nBlockSize) {} \
    virtual ~name() {}

#define _AFX_COLL_MAP(name, K, AK, V, AV) \
  class name : public CMap<K, AK, V, AV> { \
  public: \
    typedef K BASE_KEY; \
    typedef AK BASE_ARG_KEY; \
    typedef V BASE_VALUE; \
    typedef AV BASE_ARG_VALUE; \
    explicit name(INT_PTR nBlockSize = 10) : CMap<K, AK, V, AV>(nBlockSize) {} \
    virtual ~name() {}

_AFX_COLL_ARRAY(CByteArray, BYTE, BYTE)
  DECLARE_SERIAL(CByteArray)
  void Serialize(CArchive& ar);
};

_AFX_COLL_ARRAY(CWordArray, WORD, WORD)
  DECLARE_SERIAL(CWordArray)
  void Serialize(CArchive& ar);
};

_AFX_COLL_ARRAY(CDWordArray, DWORD, DWORD)
  DECLARE_SERIAL(CDWordArray)
  void Serialize(CArchive& ar);
};

_AFX_COLL_ARRAY(CUIntArray, UINT, UINT)
  DECLARE_DYNAMIC(CUIntArray)
};

_AFX_COLL_ARRAY(CPtrArray, void*, void*)
  DECLARE_DYNAMIC(CPtrArray)
};

_AFX_COLL_ARRAY(CObArray, CObject*, CObject*)
  DECLARE_SERIAL(CObArray)
  void Serialize(CArchive& ar);
};

_AFX_COLL_ARRAY(CStringArray, CString, LPCTSTR)
  DECLARE_SERIAL(CStringArray)
  void Serialize(CArchive& ar);
  INT_PTR Add(LPCTSTR newElement) { return CArray<CString, LPCTSTR>::Add(newElement); }
  INT_PTR Add(const CString& newElement) {
    INT_PTR n = GetSize();
    SetAtGrow(n, newElement);
    return n;
  }
};

_AFX_COLL_LIST(CPtrList, void*, void*)
  DECLARE_DYNAMIC(CPtrList)
};

_AFX_COLL_LIST(CObList, CObject*, CObject*)
  DECLARE_SERIAL(CObList)
  void Serialize(CArchive& ar);
};

_AFX_COLL_LIST(CStringList, CString, LPCTSTR)
  DECLARE_SERIAL(CStringList)
  void Serialize(CArchive& ar);
};

_AFX_COLL_MAP(CMapWordToPtr, WORD, WORD, void*, void*)
  DECLARE_DYNAMIC(CMapWordToPtr)
};

_AFX_COLL_MAP(CMapPtrToWord, void*, void*, WORD, WORD)
  DECLARE_DYNAMIC(CMapPtrToWord)
};

_AFX_COLL_MAP(CMapPtrToPtr, void*, void*, void*, void*)
  DECLARE_DYNAMIC(CMapPtrToPtr)
};

_AFX_COLL_MAP(CMapWordToOb, WORD, WORD, CObject*, CObject*)
  DECLARE_SERIAL(CMapWordToOb)
  void Serialize(CArchive& ar);
};

_AFX_COLL_MAP(CMapStringToPtr, CString, LPCTSTR, void*, void*)
  DECLARE_DYNAMIC(CMapStringToPtr)
};

_AFX_COLL_MAP(CMapStringToOb, CString, LPCTSTR, CObject*, CObject*)
  DECLARE_SERIAL(CMapStringToOb)
  void Serialize(CArchive& ar);
};

_AFX_COLL_MAP(CMapStringToString, CString, LPCTSTR, CString, LPCTSTR)
  DECLARE_SERIAL(CMapStringToString)
  void Serialize(CArchive& ar);
};

#undef _AFX_COLL_ARRAY
#undef _AFX_COLL_LIST
#undef _AFX_COLL_MAP

#endif // __AFXCOLL_H__
