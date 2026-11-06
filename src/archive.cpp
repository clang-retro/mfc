// CArchive: buffered binary I/O and object persistence.
//
// Objects are written in MFC's documented archive format (Technical Note
// 2), so archives made by MFC programs can be read and the other way round.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
#include "stdafx.h"

// Tags of the object format.
static const WORD wNullTag = 0;
static const WORD wNewClassTag = 0xFFFF;
static const WORD wClassTag = 0x8000;
static const DWORD dwBigClassTag = 0x80000000;
static const WORD wBigObjectTag = 0x7FFF;
static const DWORD nMaxMapCount = 0x3FFFFFFE;

namespace {
struct ClassEntry {
  CRuntimeClass* pClass;
  UINT nSchema;
};
}  // namespace

CArchive::CArchive(CFile* pFile, UINT nMode, int nBufSize, void* lpBuf)
    : m_pDocument(NULL), m_bForceFlat(TRUE), m_bDirectBuffer(FALSE), m_bBlocking(FALSE),
      m_nObjectSchema((UINT)-1), m_nMode(nMode), m_bUserBuf(lpBuf != NULL),
      m_nBufSize(nBufSize < 128 ? 128 : nBufSize), m_pFile(pFile), m_nMapCount(1),
      m_pLoadArray(NULL), m_nLoadArraySize(0), m_pStoreMap(NULL), m_nStoreMapSize(0),
      m_nStoreMapCount(0), m_nGrowSize(1024), m_nHashSize(2053) {
  m_strFileName = pFile->GetFilePath();
  m_lpBufStart = lpBuf ? (BYTE*)lpBuf : (BYTE*)malloc(m_nBufSize);
  if (!m_lpBufStart) AfxThrowMemoryException();
  m_lpBufCur = m_lpBufStart;
  // Loading starts with an empty buffer; storing with a full free one.
  m_lpBufMax = IsLoading() ? m_lpBufStart : m_lpBufStart + m_nBufSize;
}

CArchive::~CArchive() {
  if (m_pFile != NULL && !(m_nMode & bNoFlushOnDelete)) {
    try {
      Close();
    } catch (CException* e) {
      e->Delete();
    }
  }
  Abort();
}

void CArchive::Abort() {
  if (!m_bUserBuf) free(m_lpBufStart);
  m_lpBufStart = m_lpBufCur = m_lpBufMax = NULL;
  if (m_pLoadArray) {
    for (UINT i = 0; i < m_nMapCount; i++)
      if ((UINT_PTR)m_pLoadArray[i] & 1) free((void*)((UINT_PTR)m_pLoadArray[i] & ~(UINT_PTR)1));
    free(m_pLoadArray);
  }
  m_pLoadArray = NULL;
  free(m_pStoreMap);
  m_pStoreMap = NULL;
  m_pFile = NULL;
}

void CArchive::Close() {
  Flush();
  m_pFile = NULL;
}

void CArchive::Flush() {
  if (m_pFile == NULL) return;
  if (IsLoading()) {
    // Give back what was read ahead, so the file is where the archive is.
    if (m_lpBufMax != m_lpBufCur)
      m_pFile->Seek(-(LONGLONG)(m_lpBufMax - m_lpBufCur), CFile::current);
    m_lpBufCur = m_lpBufMax;
  } else {
    if (m_lpBufCur != m_lpBufStart)
      m_pFile->Write(m_lpBufStart, (UINT)(m_lpBufCur - m_lpBufStart));
    m_lpBufCur = m_lpBufStart;
  }
}

void CArchive::FillBuffer(UINT nBytesNeeded) {
  // Keep what is left at the start of the buffer and read more after it.
  UINT nUnused = (UINT)(m_lpBufMax - m_lpBufCur);
  if (nUnused > 0) memmove(m_lpBufStart, m_lpBufCur, nUnused);
  UINT nRead = nUnused;
  UINT nTotal = (UINT)m_nBufSize;
  while (nRead < nBytesNeeded) {
    UINT n = m_pFile->Read(m_lpBufStart + nRead, nTotal - nRead);
    if (n == 0) break;
    nRead += n;
  }
  m_lpBufCur = m_lpBufStart;
  m_lpBufMax = m_lpBufStart + nRead;
}

UINT CArchive::Read(void* lpBuf, UINT nMax) {
  if (nMax == 0) return 0;
  if (!IsLoading()) AfxThrowArchiveException(CArchiveException::writeOnly, m_strFileName);
  BYTE* p = (BYTE*)lpBuf;
  UINT nLeft = nMax;
  while (nLeft) {
    UINT nAvail = (UINT)(m_lpBufMax - m_lpBufCur);
    if (nAvail == 0) {
      if (nLeft >= (UINT)m_nBufSize) {
        // Large reads go straight to the file.
        UINT n = m_pFile->Read(p, nLeft);
        p += n;
        nLeft -= n;
        if (n == 0) break;
        continue;
      }
      FillBuffer(nLeft);
      nAvail = (UINT)(m_lpBufMax - m_lpBufCur);
      if (nAvail == 0) break;
    }
    UINT n = nAvail < nLeft ? nAvail : nLeft;
    memcpy(p, m_lpBufCur, n);
    m_lpBufCur += n;
    p += n;
    nLeft -= n;
  }
  return nMax - nLeft;
}

void CArchive::EnsureRead(void* lpBuf, UINT nCount) {
  if (Read(lpBuf, nCount) != nCount)
    AfxThrowArchiveException(CArchiveException::endOfFile, m_strFileName);
}

void CArchive::Write(const void* lpBuf, UINT nMax) {
  if (nMax == 0) return;
  if (IsLoading()) AfxThrowArchiveException(CArchiveException::readOnly, m_strFileName);
  const BYTE* p = (const BYTE*)lpBuf;
  while (nMax) {
    UINT nRoom = (UINT)(m_lpBufMax - m_lpBufCur);
    if (nRoom == 0) {
      Flush();
      nRoom = (UINT)(m_lpBufMax - m_lpBufCur);
      if (nMax >= (UINT)m_nBufSize) {
        m_pFile->Write(p, nMax);
        return;
      }
    }
    UINT n = nRoom < nMax ? nRoom : nMax;
    memcpy(m_lpBufCur, p, n);
    m_lpBufCur += n;
    p += n;
    nMax -= n;
  }
}

DWORD_PTR CArchive::ReadCount() {
  WORD wCount;
  *this >> wCount;
  if (wCount != 0xFFFF) return wCount;
  DWORD dwCount;
  *this >> dwCount;
  return dwCount;
}

void CArchive::WriteCount(DWORD_PTR dwCount) {
  if (dwCount < 0xFFFF) {
    *this << (WORD)dwCount;
  } else {
    *this << (WORD)0xFFFF;
    *this << (DWORD)dwCount;
  }
}

UINT CArchive::GetObjectSchema() {
  UINT n = m_nObjectSchema;
  m_nObjectSchema = (UINT)-1;
  return n;
}

void CArchive::SetStoreParams(UINT nHashSize, UINT) { m_nHashSize = nHashSize; }
void CArchive::SetLoadParams(UINT nGrowBy) { m_nGrowSize = nGrowBy ? nGrowBy : 1024; }

// ---------------------------------------------------------------------
// Text lines

void CArchive::WriteString(LPCTSTR lpsz) {
  Write(lpsz, lstrlen(lpsz) * sizeof(TCHAR));
}

LPTSTR CArchive::ReadString(LPTSTR lpsz, UINT nMax) {
  UINT n = 0;
  TCHAR ch;
  while (n + 1 < nMax) {
    if (Read(&ch, sizeof ch) != sizeof ch) break;
    if (ch == '\n' || ch == '\r') {
      if (ch == '\r') Read(&ch, sizeof ch);  // the '\n' after it
      lpsz[n++] = '\n';
      break;
    }
    lpsz[n++] = ch;
  }
  lpsz[n] = 0;
  return n ? lpsz : NULL;
}

BOOL CArchive::ReadString(CString& rString) {
  rString.Empty();
  TCHAR ch;
  bool any = false;
  while (Read(&ch, sizeof ch) == sizeof ch) {
    any = true;
    if (ch == '\r') continue;
    if (ch == '\n') break;
    rString += ch;
  }
  return any;
}

// ---------------------------------------------------------------------
// Object maps

DWORD CArchive::LookupStore(const void* key) const {
  if (!m_pStoreMap) return 0;
  UINT h = (UINT)(((UINT_PTR)key >> 4) % m_nStoreMapSize);
  for (;;) {
    if (m_pStoreMap[h].key == NULL) return 0;
    if (m_pStoreMap[h].key == key) return m_pStoreMap[h].value;
    h = (h + 1) % m_nStoreMapSize;
  }
}

void CArchive::AddStore(const void* key, DWORD value) {
  if (m_nStoreMapCount * 2 >= m_nStoreMapSize) {
    StoreEntry* pOld = m_pStoreMap;
    UINT nOld = m_nStoreMapSize;
    m_nStoreMapSize = nOld ? nOld * 2 + 1 : (m_nHashSize ? m_nHashSize : 2053);
    m_pStoreMap = (StoreEntry*)calloc(m_nStoreMapSize, sizeof(StoreEntry));
    if (!m_pStoreMap) AfxThrowMemoryException();
    m_nStoreMapCount = 0;
    for (UINT i = 0; i < nOld; i++)
      if (pOld[i].key) AddStore(pOld[i].key, pOld[i].value);
    free(pOld);
  }
  UINT h = (UINT)(((UINT_PTR)key >> 4) % m_nStoreMapSize);
  while (m_pStoreMap[h].key != NULL) h = (h + 1) % m_nStoreMapSize;
  m_pStoreMap[h].key = key;
  m_pStoreMap[h].value = value;
  m_nStoreMapCount++;
}

void CArchive::AddLoad(void* p) {
  if (m_nMapCount >= m_nLoadArraySize) {
    UINT nNew = m_nLoadArraySize + m_nGrowSize;
    void** pNew = (void**)realloc(m_pLoadArray, nNew * sizeof(void*));
    if (!pNew) AfxThrowMemoryException();
    m_pLoadArray = pNew;
    m_nLoadArraySize = nNew;
  }
  m_pLoadArray[m_nMapCount++] = p;
}

void CArchive::MapObject(const CObject* pOb) {
  if (IsStoring()) {
    if (pOb && !LookupStore(pOb)) AddStore(pOb, m_nMapCount++);
  } else {
    if (!m_pLoadArray) {
      m_nMapCount = 0;
      AddLoad(NULL);
    }
    AddLoad((void*)pOb);
  }
}

// ---------------------------------------------------------------------
// Classes and objects

void CRuntimeClass::Store(CArchive& ar) const {
  WORD nLen = (WORD)lstrlenA(m_lpszClassName);
  ar << (WORD)m_wSchema << nLen;
  ar.Write(m_lpszClassName, nLen);
}

CRuntimeClass* PASCAL CRuntimeClass::Load(CArchive& ar, UINT* pwSchemaNum) {
  WORD nLen, wTemp;
  char szClassName[64];
  ar >> wTemp;
  *pwSchemaNum = wTemp;
  ar >> nLen;
  if (nLen >= _countof(szClassName) || ar.Read(szClassName, nLen) != nLen) return NULL;
  szClassName[nLen] = 0;
  return FromName(szClassName);
}

void CArchive::WriteClass(const CRuntimeClass* pClassRef) {
  if (!IsStoring()) AfxThrowArchiveException(CArchiveException::readOnly, m_strFileName);
  if (pClassRef == NULL || !pClassRef->IsSerializable())
    AfxThrowNotSupportedException();
  DWORD nClassIndex = LookupStore(pClassRef);
  if (nClassIndex != 0) {
    if (nClassIndex < wBigObjectTag) {
      *this << (WORD)(wClassTag | nClassIndex);
    } else {
      *this << wBigObjectTag;
      *this << (dwBigClassTag | nClassIndex);
    }
  } else {
    *this << wNewClassTag;
    pClassRef->Store(*this);
    if (m_nMapCount >= nMaxMapCount)
      AfxThrowArchiveException(CArchiveException::badIndex, m_strFileName);
    AddStore(pClassRef, m_nMapCount++);
  }
}

void CArchive::WriteObject(const CObject* pOb) {
  if (pOb == NULL) {
    *this << wNullTag;
    return;
  }
  DWORD nObIndex = LookupStore(pOb);
  if (nObIndex != 0) {
    if (nObIndex < wBigObjectTag) {
      *this << (WORD)nObIndex;
    } else {
      *this << wBigObjectTag;
      *this << nObIndex;
    }
    return;
  }
  CRuntimeClass* pClassRef = pOb->GetRuntimeClass();
  WriteClass(pClassRef);
  if (m_nMapCount >= nMaxMapCount)
    AfxThrowArchiveException(CArchiveException::badIndex, m_strFileName);
  AddStore(pOb, m_nMapCount++);
  ((CObject*)pOb)->Serialize(*this);
}

CRuntimeClass* CArchive::ReadClass(const CRuntimeClass* pClassRefRequested, UINT* pSchema,
                                   DWORD* pObTag) {
  if (!IsLoading()) AfxThrowArchiveException(CArchiveException::writeOnly, m_strFileName);
  if (pClassRefRequested != NULL && !pClassRefRequested->IsSerializable())
    AfxThrowNotSupportedException();
  if (m_pLoadArray == NULL) {
    m_nMapCount = 0;
    AddLoad(NULL);  // index 0 is the null object
  }

  WORD wTag;
  *this >> wTag;
  DWORD obTag;
  if (wTag == wBigObjectTag)
    *this >> obTag;
  else
    obTag = ((DWORD)(wTag & wClassTag) << 16) | (wTag & ~wClassTag);

  if (!(obTag & dwBigClassTag)) {
    // An object, not a class.
    if (pObTag == NULL) AfxThrowArchiveException(CArchiveException::badIndex, m_strFileName);
    *pObTag = obTag;
    return NULL;
  }

  CRuntimeClass* pClassRef;
  UINT nSchema;
  if (wTag == wNewClassTag) {
    pClassRef = CRuntimeClass::Load(*this, &nSchema);
    if (pClassRef == NULL)
      AfxThrowArchiveException(CArchiveException::badClass, m_strFileName);
    if ((pClassRef->m_wSchema & ~VERSIONABLE_SCHEMA) != nSchema) {
      if (!(pClassRef->m_wSchema & VERSIONABLE_SCHEMA))
        AfxThrowArchiveException(CArchiveException::badSchema, m_strFileName);
    }
    // Classes in the load array keep their schema beside them: the
    // pointer is to a small record.
    // Classes go in the load array as tagged pointers (low bit set) to
    // a record with their schema; objects are never at odd addresses.
    ClassEntry* e = (ClassEntry*)malloc(sizeof(ClassEntry));
    if (!e) AfxThrowMemoryException();
    e->pClass = pClassRef;
    e->nSchema = nSchema;
    AddLoad((void*)((UINT_PTR)e | 1));
  } else {
    DWORD nClassIndex = obTag & ~dwBigClassTag;
    if (nClassIndex == 0 || nClassIndex >= m_nMapCount)
      AfxThrowArchiveException(CArchiveException::badIndex, m_strFileName);
    UINT_PTR v = (UINT_PTR)m_pLoadArray[nClassIndex];
    if (!(v & 1)) AfxThrowArchiveException(CArchiveException::badIndex, m_strFileName);
    ClassEntry* e = (ClassEntry*)(v & ~(UINT_PTR)1);
    pClassRef = e->pClass;
    nSchema = e->nSchema;
  }

  if (pClassRefRequested != NULL && !pClassRef->IsDerivedFrom(pClassRefRequested))
    AfxThrowArchiveException(CArchiveException::badClass, m_strFileName);
  if (pSchema != NULL) *pSchema = nSchema;
  else m_nObjectSchema = nSchema;
  if (pObTag != NULL) *pObTag = obTag;
  return pClassRef;
}

void CArchive::SerializeClass(const CRuntimeClass* pClassRef) {
  if (IsStoring())
    WriteClass(pClassRef);
  else
    ReadClass(pClassRef, &m_nObjectSchema);
}

CObject* CArchive::ReadObject(const CRuntimeClass* pClassRefRequested) {
  UINT nSchema;
  DWORD obTag;
  CRuntimeClass* pClassRef = ReadClass(pClassRefRequested, &nSchema, &obTag);
  CObject* pOb;
  if (pClassRef == NULL) {
    if (obTag >= m_nMapCount)
      AfxThrowArchiveException(CArchiveException::badIndex, m_strFileName);
    if ((UINT_PTR)m_pLoadArray[obTag] & 1)
      AfxThrowArchiveException(CArchiveException::badIndex, m_strFileName);
    pOb = (CObject*)m_pLoadArray[obTag];
    if (pOb != NULL && pClassRefRequested != NULL && !pOb->IsKindOf(pClassRefRequested))
      AfxThrowArchiveException(CArchiveException::badClass, m_strFileName);
  } else {
    pOb = pClassRef->CreateObject();
    if (pOb == NULL) AfxThrowMemoryException();
    AddLoad(pOb);
    UINT nSchemaSave = m_nObjectSchema;
    m_nObjectSchema = nSchema;
    pOb->Serialize(*this);
    m_nObjectSchema = nSchemaSave;
  }
  return pOb;
}

// ---------------------------------------------------------------------
// Strings: a length (BYTE, or 0xFF and a WORD, or 0xFF 0xFFFF and a
// DWORD), with 0xFF 0xFFFE in front for UTF-16 strings.

static void WriteStringLength(CArchive& ar, DWORD nLength, bool bUnicode) {
  if (bUnicode) {
    ar << (BYTE)0xff;
    ar << (WORD)0xfffe;
  }
  if (nLength < 255) {
    ar << (BYTE)nLength;
  } else if (nLength < 0xfffe) {
    ar << (BYTE)0xff;
    ar << (WORD)nLength;
  } else {
    ar << (BYTE)0xff;
    ar << (WORD)0xffff;
    ar << (DWORD)nLength;
  }
}

static DWORD ReadStringLength(CArchive& ar, int& nCharSize) {
  nCharSize = 1;
  for (;;) {
    BYTE bLen;
    ar >> bLen;
    if (bLen < 0xff) return bLen;
    WORD wLen;
    ar >> wLen;
    if (wLen == 0xfffe) {
      nCharSize = 2;
      continue;
    }
    if (wLen < 0xffff) return wLen;
    DWORD dwLen;
    ar >> dwLen;
    return dwLen;
  }
}

CArchive& AFXAPI operator<<(CArchive& ar, const CStringA& str) {
  WriteStringLength(ar, str.GetLength(), false);
  ar.Write(str.GetString(), str.GetLength());
  return ar;
}

CArchive& AFXAPI operator<<(CArchive& ar, const CStringW& str) {
  WriteStringLength(ar, str.GetLength(), true);
  ar.Write(str.GetString(), str.GetLength() * sizeof(WCHAR));
  return ar;
}

template <class S>
static void ReadAnyString(CArchive& ar, S& str) {
  int nCharSize;
  DWORD n = ReadStringLength(ar, nCharSize);
  if (nCharSize == 1) {
    CStringA a;
    char* p = a.GetBuffer((int)n);
    ar.EnsureRead(p, n);
    a.ReleaseBufferSetLength((int)n);
    str = a;
  } else {
    CStringW w;
    WCHAR* p = w.GetBuffer((int)n);
    ar.EnsureRead(p, n * sizeof(WCHAR));
    w.ReleaseBufferSetLength((int)n);
    str = w;
  }
}

CArchive& AFXAPI operator>>(CArchive& ar, CStringA& str) {
  ReadAnyString(ar, str);
  return ar;
}

CArchive& AFXAPI operator>>(CArchive& ar, CStringW& str) {
  ReadAnyString(ar, str);
  return ar;
}

CArchive& AFXAPI operator<<(CArchive& ar, CTime t) { return ar << (LONGLONG)t.GetTime(); }
CArchive& AFXAPI operator>>(CArchive& ar, CTime& t) {
  LONGLONG v;
  ar >> v;
  t = CTime((__time64_t)v);
  return ar;
}
CArchive& AFXAPI operator<<(CArchive& ar, CTimeSpan t) {
  return ar << (LONGLONG)t.GetTimeSpan();
}
CArchive& AFXAPI operator>>(CArchive& ar, CTimeSpan& t) {
  LONGLONG v;
  ar >> v;
  t = CTimeSpan((__time64_t)v);
  return ar;
}
