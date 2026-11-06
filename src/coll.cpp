// The runtime classes and serialization of the non-template collections.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
#include "stdafx.h"

IMPLEMENT_SERIAL(CByteArray, CObject, 0)
void CByteArray::Serialize(CArchive& ar) {
  typedef CArray<BYTE, BYTE> Base;
  Base::Serialize(ar);
}

IMPLEMENT_SERIAL(CWordArray, CObject, 0)
void CWordArray::Serialize(CArchive& ar) {
  typedef CArray<WORD, WORD> Base;
  Base::Serialize(ar);
}

IMPLEMENT_SERIAL(CDWordArray, CObject, 0)
void CDWordArray::Serialize(CArchive& ar) {
  typedef CArray<DWORD, DWORD> Base;
  Base::Serialize(ar);
}

IMPLEMENT_SERIAL(CObArray, CObject, 0)
void CObArray::Serialize(CArchive& ar) {
  typedef CArray<CObject*, CObject*> Base;
  Base::Serialize(ar);
}

IMPLEMENT_SERIAL(CStringArray, CObject, 0)
void CStringArray::Serialize(CArchive& ar) {
  typedef CArray<CString, LPCTSTR> Base;
  Base::Serialize(ar);
}

IMPLEMENT_SERIAL(CObList, CObject, 0)
void CObList::Serialize(CArchive& ar) {
  typedef CList<CObject*, CObject*> Base;
  Base::Serialize(ar);
}

IMPLEMENT_SERIAL(CStringList, CObject, 0)
void CStringList::Serialize(CArchive& ar) {
  typedef CList<CString, LPCTSTR> Base;
  Base::Serialize(ar);
}

IMPLEMENT_SERIAL(CMapWordToOb, CObject, 0)
void CMapWordToOb::Serialize(CArchive& ar) {
  typedef CMap<WORD, WORD, CObject*, CObject*> Base;
  Base::Serialize(ar);
}

IMPLEMENT_SERIAL(CMapStringToOb, CObject, 0)
void CMapStringToOb::Serialize(CArchive& ar) {
  typedef CMap<CString, LPCTSTR, CObject*, CObject*> Base;
  Base::Serialize(ar);
}

IMPLEMENT_SERIAL(CMapStringToString, CObject, 0)
void CMapStringToString::Serialize(CArchive& ar) {
  typedef CMap<CString, LPCTSTR, CString, LPCTSTR> Base;
  Base::Serialize(ar);
}

IMPLEMENT_DYNAMIC(CUIntArray, CObject)
IMPLEMENT_DYNAMIC(CPtrArray, CObject)
IMPLEMENT_DYNAMIC(CPtrList, CObject)
IMPLEMENT_DYNAMIC(CMapWordToPtr, CObject)
IMPLEMENT_DYNAMIC(CMapPtrToWord, CObject)
IMPLEMENT_DYNAMIC(CMapPtrToPtr, CObject)
IMPLEMENT_DYNAMIC(CMapStringToPtr, CObject)
