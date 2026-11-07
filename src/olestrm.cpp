// olestrm.cpp - COleStreamFile: a CFile over an IStream.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include "afximpl.h"
#include <afxole.h>

IMPLEMENT_DYNAMIC(COleStreamFile, CFile)

// The CFileException cause that matches a storage error.
static int FileCause(SCODE sc) {
  switch (sc) {
  case STG_E_FILENOTFOUND:
  case STG_E_FILEALREADYEXISTS:
    return CFileException::fileNotFound;
  case STG_E_PATHNOTFOUND:
  case STG_E_INVALIDNAME:
    return CFileException::badPath;
  case STG_E_TOOMANYOPENFILES:
    return CFileException::tooManyOpenFiles;
  case STG_E_ACCESSDENIED:
  case STG_E_INVALIDFUNCTION:
    return CFileException::accessDenied;
  case STG_E_INVALIDHANDLE:
  case STG_E_INVALIDHEADER:
  case STG_E_DOCFILECORRUPT:
  case STG_E_OLDFORMAT:
  case STG_E_OLDDLL:
    return CFileException::invalidFile;
  case STG_E_SEEKERROR:
    return CFileException::badSeek;
  case STG_E_WRITEFAULT:
  case STG_E_READFAULT:
    return CFileException::hardIO;
  case STG_E_SHAREVIOLATION:
    return CFileException::sharingViolation;
  case STG_E_LOCKVIOLATION:
    return CFileException::lockViolation;
  case STG_E_MEDIUMFULL:
  case STG_E_INSUFFICIENTMEMORY:
  case E_OUTOFMEMORY:
    return CFileException::diskFull;
  default:
    return CFileException::genericException;
  }
}

void AFXAPI _AfxThrowOleFileException(SCODE sc) {
  AfxThrowFileException(FileCause(sc), (LONG)sc);
}

static void FillError(CFileException* pError, SCODE sc, LPCTSTR lpszName) {
  if (pError == NULL) return;
  pError->m_cause = FileCause(sc);
  pError->m_lOsError = (LONG)sc;
  pError->m_strFileName = lpszName;
}

COleStreamFile::COleStreamFile(LPSTREAM lpStream) : m_lpStream(lpStream) {
  if (m_lpStream != NULL) m_hFile = (HANDLE)m_lpStream;
}

COleStreamFile::~COleStreamFile() {
  if (m_lpStream != NULL && m_bCloseOnDelete) Close();
  else if (m_lpStream != NULL) Detach();
}

void COleStreamFile::Attach(LPSTREAM lpStream) {
  m_lpStream = lpStream;
  m_hFile = (HANDLE)lpStream;
  m_bCloseOnDelete = FALSE;
}

LPSTREAM COleStreamFile::Detach() {
  LPSTREAM lpStream = m_lpStream;
  m_lpStream = NULL;
  m_hFile = INVALID_HANDLE_VALUE;
  m_bCloseOnDelete = FALSE;
  return lpStream;
}

// CFile's flags have the same values as the STGM ones for access, sharing
// and creation.
static DWORD StgFlags(DWORD nOpenFlags) {
  DWORD grf = nOpenFlags & (STGM_READ | STGM_WRITE | STGM_READWRITE);
  return grf | STGM_SHARE_EXCLUSIVE;
}

BOOL COleStreamFile::OpenStream(LPSTORAGE lpStorage, LPCTSTR lpszStreamName, DWORD nOpenFlags,
                                CFileException* pError) {
  CStringW strName(lpszStreamName);
  LPSTREAM lpStream = NULL;
  SCODE sc = lpStorage->OpenStream(strName, NULL, StgFlags(nOpenFlags), 0, &lpStream);
  if (FAILED(sc)) {
    FillError(pError, sc, lpszStreamName);
    return FALSE;
  }
  Attach(lpStream);
  m_bCloseOnDelete = TRUE;
  m_strFileName = lpszStreamName;
  return TRUE;
}

BOOL COleStreamFile::CreateStream(LPSTORAGE lpStorage, LPCTSTR lpszStreamName,
                                  DWORD nOpenFlags, CFileException* pError) {
  CStringW strName(lpszStreamName);
  LPSTREAM lpStream = NULL;
  DWORD grf = StgFlags(nOpenFlags);
  // modeCreate replaces a stream that is there; without it, an existing
  // stream is an error.
  grf |= (nOpenFlags & modeCreate) ? STGM_CREATE : STGM_FAILIFTHERE;
  SCODE sc = lpStorage->CreateStream(strName, grf, 0, 0, &lpStream);
  if (FAILED(sc)) {
    FillError(pError, sc, lpszStreamName);
    return FALSE;
  }
  Attach(lpStream);
  m_bCloseOnDelete = TRUE;
  m_strFileName = lpszStreamName;
  return TRUE;
}

BOOL COleStreamFile::CreateMemoryStream(CFileException* pError) {
  LPSTREAM lpStream = NULL;
  SCODE sc = ::CreateStreamOnHGlobal(NULL, TRUE, &lpStream);
  if (FAILED(sc)) {
    FillError(pError, sc, NULL);
    return FALSE;
  }
  Attach(lpStream);
  m_bCloseOnDelete = TRUE;
  return TRUE;
}

static void Check(SCODE sc) {
  if (FAILED(sc)) _AfxThrowOleFileException(sc);
}

ULONGLONG COleStreamFile::GetPosition() const {
  LARGE_INTEGER li;
  li.QuadPart = 0;
  ULARGE_INTEGER pos;
  Check(m_lpStream->Seek(li, STREAM_SEEK_CUR, &pos));
  return pos.QuadPart;
}

ULONGLONG COleStreamFile::Seek(LONGLONG lOff, UINT nFrom) {
  LARGE_INTEGER li;
  li.QuadPart = lOff;
  ULARGE_INTEGER pos;
  // CFile::begin, current and end match STREAM_SEEK_SET, CUR and END.
  Check(m_lpStream->Seek(li, nFrom, &pos));
  return pos.QuadPart;
}

void COleStreamFile::SetLength(ULONGLONG dwNewLen) {
  ULARGE_INTEGER li;
  li.QuadPart = dwNewLen;
  Check(m_lpStream->SetSize(li));
}

ULONGLONG COleStreamFile::GetLength() const {
  STATSTG st;
  Check(m_lpStream->Stat(&st, STATFLAG_NONAME));
  return st.cbSize.QuadPart;
}

UINT COleStreamFile::Read(void* lpBuf, UINT nCount) {
  ULONG nRead = 0;
  Check(m_lpStream->Read(lpBuf, nCount, &nRead));
  return nRead;
}

void COleStreamFile::Write(const void* lpBuf, UINT nCount) {
  ULONG nWritten = 0;
  Check(m_lpStream->Write(lpBuf, nCount, &nWritten));
  if (nWritten != nCount) AfxThrowFileException(CFileException::diskFull);
}

void COleStreamFile::LockRange(ULONGLONG dwPos, ULONGLONG dwCount) {
  ULARGE_INTEGER pos, count;
  pos.QuadPart = dwPos;
  count.QuadPart = dwCount;
  Check(m_lpStream->LockRegion(pos, count, LOCK_EXCLUSIVE));
}

void COleStreamFile::UnlockRange(ULONGLONG dwPos, ULONGLONG dwCount) {
  ULARGE_INTEGER pos, count;
  pos.QuadPart = dwPos;
  count.QuadPart = dwCount;
  Check(m_lpStream->UnlockRegion(pos, count, LOCK_EXCLUSIVE));
}

void COleStreamFile::Abort() {
  if (m_lpStream != NULL) {
    m_lpStream->Revert();
    m_lpStream->Release();
  }
  Detach();
  m_strFileName.Empty();
}

void COleStreamFile::Flush() { Check(m_lpStream->Commit(STGC_DEFAULT)); }

void COleStreamFile::Close() {
  if (m_lpStream != NULL) {
    m_lpStream->Commit(STGC_DEFAULT);
    m_lpStream->Release();
  }
  Detach();
  m_strFileName.Empty();
}

CFile* COleStreamFile::Duplicate() const {
  LPSTREAM lpStream = NULL;
  Check(m_lpStream->Clone(&lpStream));
  COleStreamFile* pFile = new COleStreamFile(lpStream);
  pFile->m_bCloseOnDelete = TRUE;
  pFile->m_strFileName = m_strFileName;
  return pFile;
}

static void FileTimeToCTime(const FILETIME& ft, CTime& t) {
  if (ft.dwLowDateTime == 0 && ft.dwHighDateTime == 0) t = CTime();
  else t = CTime(ft);
}

BOOL COleStreamFile::GetStatus(CFileStatus& rStatus) const {
  STATSTG st;
  if (m_lpStream == NULL || FAILED(m_lpStream->Stat(&st, STATFLAG_NONAME))) return FALSE;
  FileTimeToCTime(st.mtime, rStatus.m_mtime);
  FileTimeToCTime(st.ctime, rStatus.m_ctime);
  FileTimeToCTime(st.atime, rStatus.m_atime);
  rStatus.m_size = st.cbSize.QuadPart;
  rStatus.m_attribute = 0;
  lstrcpyn(rStatus.m_szFullName, m_strFileName, MAX_PATH);
  return TRUE;
}
