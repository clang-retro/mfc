// CFile, CStdioFile, CMemFile, CFileFind and the path helpers.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
#include "stdafx.h"

IMPLEMENT_DYNAMIC(CFile, CObject)
IMPLEMENT_DYNAMIC(CStdioFile, CFile)
IMPLEMENT_DYNAMIC(CMemFile, CFile)
IMPLEMENT_DYNAMIC(CFileFind, CObject)

const HANDLE CFile::hFileNull = INVALID_HANDLE_VALUE;

// ---------------------------------------------------------------------
// Path helpers

UINT AFXAPI AfxGetFileName(LPCTSTR lpszPathName, LPTSTR lpszTitle, UINT nMax) {
  LPCTSTR lpszTemp = lpszPathName;
  for (LPCTSTR p = lpszPathName; *p; p = CharNext(p))
    if (*p == '\\' || *p == '/' || *p == ':') lpszTemp = CharNext(p);
  if (lpszTitle == NULL) return lstrlen(lpszTemp) + 1;
  lstrcpyn(lpszTitle, lpszTemp, nMax);
  return 0;
}

UINT AFXAPI AfxGetFileTitle(LPCTSTR lpszPathName, LPTSTR lpszTitle, UINT nMax) {
  // The name without its directory or extension.
  TCHAR name[MAX_PATH];
  AfxGetFileName(lpszPathName, name, MAX_PATH);
  LPTSTR dot = NULL;
  for (LPTSTR p = name; *p; p = CharNext(p))
    if (*p == '.') dot = p;
  if (dot && dot != name) *dot = 0;
  if (lpszTitle == NULL) return lstrlen(name) + 1;
  lstrcpyn(lpszTitle, name, nMax);
  return 0;
}

BOOL AFXAPI AfxFullPath(LPTSTR lpszPathOut, LPCTSTR lpszFileIn) {
  LPTSTR lpszFilePart;
  DWORD n = GetFullPathName(lpszFileIn, MAX_PATH, lpszPathOut, &lpszFilePart);
  if (n == 0 || n >= MAX_PATH) {
    lstrcpyn(lpszPathOut, lpszFileIn, MAX_PATH);
    return FALSE;
  }
  return TRUE;
}

BOOL AFXAPI AfxComparePath(LPCTSTR lpszPath1, LPCTSTR lpszPath2) {
  return lstrcmpi(lpszPath1, lpszPath2) == 0;
}

// ---------------------------------------------------------------------
// CFile

CFile::CFile() : m_hFile(INVALID_HANDLE_VALUE), m_bCloseOnDelete(FALSE) {}

CFile::CFile(HANDLE hFile) : m_hFile(hFile), m_bCloseOnDelete(FALSE) {}

CFile::CFile(LPCTSTR lpszFileName, UINT nOpenFlags)
    : m_hFile(INVALID_HANDLE_VALUE), m_bCloseOnDelete(FALSE) {
  CFileException e;
  if (!Open(lpszFileName, nOpenFlags, &e))
    AfxThrowFileException(e.m_cause, e.m_lOsError, e.m_strFileName);
}

CFile::~CFile() {
  if (m_hFile != INVALID_HANDLE_VALUE && m_bCloseOnDelete) Close();
}

CFile* CFile::Duplicate() const {
  CFile* pFile = new CFile();
  HANDLE hFile;
  if (!DuplicateHandle(GetCurrentProcess(), m_hFile, GetCurrentProcess(), &hFile, 0,
                       FALSE, DUPLICATE_SAME_ACCESS)) {
    delete pFile;
    CFileException::ThrowOsError((LONG)GetLastError(), m_strFileName);
  }
  pFile->m_hFile = hFile;
  pFile->m_bCloseOnDelete = m_bCloseOnDelete;
  pFile->m_strFileName = m_strFileName;
  return pFile;
}

BOOL CFile::Open(LPCTSTR lpszFileName, UINT nOpenFlags, CFileException* pException) {
  nOpenFlags &= ~(UINT)typeBinary;
  m_bCloseOnDelete = FALSE;
  m_hFile = INVALID_HANDLE_VALUE;
  m_strFileName.Empty();

  TCHAR szTemp[MAX_PATH];
  if (lpszFileName != NULL && AfxFullPath(szTemp, lpszFileName))
    m_strFileName = szTemp;
  else
    m_strFileName = lpszFileName;

  DWORD dwAccess = 0;
  switch (nOpenFlags & 3) {
  case modeRead: dwAccess = GENERIC_READ; break;
  case modeWrite: dwAccess = GENERIC_WRITE; break;
  case modeReadWrite: dwAccess = GENERIC_READ | GENERIC_WRITE; break;
  default: ASSERT(FALSE);
  }
  DWORD dwShareMode = 0;
  switch (nOpenFlags & 0x70) {
  default:
  case shareCompat:
  case shareExclusive: dwShareMode = 0; break;
  case shareDenyWrite: dwShareMode = FILE_SHARE_READ; break;
  case shareDenyRead: dwShareMode = FILE_SHARE_WRITE; break;
  case shareDenyNone: dwShareMode = FILE_SHARE_WRITE | FILE_SHARE_READ; break;
  }
  SECURITY_ATTRIBUTES sa;
  sa.nLength = sizeof(sa);
  sa.lpSecurityDescriptor = NULL;
  sa.bInheritHandle = (nOpenFlags & modeNoInherit) == 0;

  DWORD dwCreateFlag;
  if (nOpenFlags & modeCreate)
    dwCreateFlag = (nOpenFlags & modeNoTruncate) ? OPEN_ALWAYS : CREATE_ALWAYS;
  else
    dwCreateFlag = OPEN_EXISTING;

  DWORD dwFlags = FILE_ATTRIBUTE_NORMAL;
  if (nOpenFlags & osNoBuffer) dwFlags |= FILE_FLAG_NO_BUFFERING;
  if (nOpenFlags & osWriteThrough) dwFlags |= FILE_FLAG_WRITE_THROUGH;
  if (nOpenFlags & osRandomAccess) dwFlags |= FILE_FLAG_RANDOM_ACCESS;
  if (nOpenFlags & osSequentialScan) dwFlags |= FILE_FLAG_SEQUENTIAL_SCAN;

  HANDLE hFile = ::CreateFile(lpszFileName, dwAccess, dwShareMode, &sa, dwCreateFlag,
                              dwFlags, NULL);
  if (hFile == INVALID_HANDLE_VALUE) {
    if (pException != NULL) {
      pException->m_lOsError = (LONG)GetLastError();
      pException->m_cause = CFileException::OsErrorToException(pException->m_lOsError);
      pException->m_strFileName = lpszFileName;
    }
    return FALSE;
  }
  m_hFile = hFile;
  m_bCloseOnDelete = TRUE;
  return TRUE;
}

UINT CFile::Read(void* lpBuf, UINT nCount) {
  if (nCount == 0) return 0;
  DWORD dwRead;
  if (!::ReadFile(m_hFile, lpBuf, nCount, &dwRead, NULL))
    CFileException::ThrowOsError((LONG)GetLastError(), m_strFileName);
  return (UINT)dwRead;
}

void CFile::Write(const void* lpBuf, UINT nCount) {
  if (nCount == 0) return;
  DWORD nWritten;
  if (!::WriteFile(m_hFile, lpBuf, nCount, &nWritten, NULL))
    CFileException::ThrowOsError((LONG)GetLastError(), m_strFileName);
  if (nWritten != nCount) AfxThrowFileException(CFileException::diskFull, -1, m_strFileName);
}

ULONGLONG CFile::Seek(LONGLONG lOff, UINT nFrom) {
  LONG hi = (LONG)(lOff >> 32);
  DWORD lo = ::SetFilePointer(m_hFile, (LONG)lOff, &hi, (DWORD)nFrom);
  if (lo == (DWORD)-1 && GetLastError() != NO_ERROR)
    CFileException::ThrowOsError((LONG)GetLastError(), m_strFileName);
  return ((ULONGLONG)(DWORD)hi << 32) | lo;
}

ULONGLONG CFile::GetPosition() const {
  LONG hi = 0;
  DWORD lo = ::SetFilePointer(m_hFile, 0, &hi, FILE_CURRENT);
  if (lo == (DWORD)-1 && GetLastError() != NO_ERROR)
    CFileException::ThrowOsError((LONG)GetLastError(), m_strFileName);
  return ((ULONGLONG)(DWORD)hi << 32) | lo;
}

ULONGLONG CFile::SeekToEnd() { return Seek(0, end); }
void CFile::SeekToBegin() { Seek(0, begin); }

void CFile::Flush() {
  if (m_hFile == INVALID_HANDLE_VALUE) return;
  if (!::FlushFileBuffers(m_hFile))
    CFileException::ThrowOsError((LONG)GetLastError(), m_strFileName);
}

void CFile::Close() {
  BOOL bError = FALSE;
  if (m_hFile != INVALID_HANDLE_VALUE) bError = !::CloseHandle(m_hFile);
  m_hFile = INVALID_HANDLE_VALUE;
  m_bCloseOnDelete = FALSE;
  m_strFileName.Empty();
  if (bError) CFileException::ThrowOsError((LONG)GetLastError(), m_strFileName);
}

void CFile::Abort() {
  if (m_hFile != INVALID_HANDLE_VALUE) {
    ::CloseHandle(m_hFile);
    m_hFile = INVALID_HANDLE_VALUE;
  }
  m_strFileName.Empty();
}

void CFile::LockRange(ULONGLONG dwPos, ULONGLONG dwCount) {
  if (!::LockFile(m_hFile, (DWORD)dwPos, (DWORD)(dwPos >> 32), (DWORD)dwCount,
                  (DWORD)(dwCount >> 32)))
    CFileException::ThrowOsError((LONG)GetLastError(), m_strFileName);
}

void CFile::UnlockRange(ULONGLONG dwPos, ULONGLONG dwCount) {
  if (!::UnlockFile(m_hFile, (DWORD)dwPos, (DWORD)(dwPos >> 32), (DWORD)dwCount,
                    (DWORD)(dwCount >> 32)))
    CFileException::ThrowOsError((LONG)GetLastError(), m_strFileName);
}

void CFile::SetLength(ULONGLONG dwNewLen) {
  Seek((LONGLONG)dwNewLen, begin);
  if (!::SetEndOfFile(m_hFile))
    CFileException::ThrowOsError((LONG)GetLastError(), m_strFileName);
}

ULONGLONG CFile::GetLength() const {
  DWORD hi = 0;
  DWORD lo = ::GetFileSize(m_hFile, &hi);
  if (lo == INVALID_FILE_SIZE && GetLastError() != NO_ERROR)
    CFileException::ThrowOsError((LONG)GetLastError(), m_strFileName);
  return ((ULONGLONG)hi << 32) | lo;
}

UINT CFile::GetBufferPtr(UINT, UINT, void**, void**) { return 0; }

BOOL CFile::GetStatus(CFileStatus& rStatus) const {
  memset(&rStatus, 0, sizeof(CFileStatus));
  lstrcpyn(rStatus.m_szFullName, m_strFileName, MAX_PATH);
  if (m_hFile == INVALID_HANDLE_VALUE) return TRUE;
  FILETIME ftCreate, ftAccess, ftModify;
  if (!::GetFileTime(m_hFile, &ftCreate, &ftAccess, &ftModify)) return FALSE;
  DWORD hi = 0;
  DWORD lo = ::GetFileSize(m_hFile, &hi);
  rStatus.m_size = ((ULONGLONG)hi << 32) | lo;
  if (m_strFileName.IsEmpty()) {
    rStatus.m_attribute = 0;
  } else {
    DWORD dw = ::GetFileAttributes(m_strFileName);
    rStatus.m_attribute = dw == (DWORD)-1 ? 0 : (BYTE)dw;
  }
  rStatus.m_ctime = CTime(ftCreate);
  rStatus.m_atime = CTime(ftAccess);
  rStatus.m_mtime = CTime(ftModify);
  if (rStatus.m_ctime.GetTime() == 0) rStatus.m_ctime = rStatus.m_mtime;
  if (rStatus.m_atime.GetTime() == 0) rStatus.m_atime = rStatus.m_mtime;
  return TRUE;
}

BOOL PASCAL CFile::GetStatus(LPCTSTR lpszFileName, CFileStatus& rStatus) {
  memset(&rStatus, 0, sizeof(CFileStatus));
  if (!AfxFullPath(rStatus.m_szFullName, lpszFileName)) {
    rStatus.m_szFullName[0] = 0;
    return FALSE;
  }
  WIN32_FIND_DATA findFileData;
  HANDLE hFind = FindFirstFile(lpszFileName, &findFileData);
  if (hFind == INVALID_HANDLE_VALUE) return FALSE;
  FindClose(hFind);
  rStatus.m_attribute = (BYTE)(findFileData.dwFileAttributes & ~FILE_ATTRIBUTE_NORMAL);
  rStatus.m_size = ((ULONGLONG)findFileData.nFileSizeHigh << 32) | findFileData.nFileSizeLow;
  rStatus.m_mtime = CTime(findFileData.ftLastWriteTime);
  rStatus.m_ctime = CTime(findFileData.ftCreationTime);
  rStatus.m_atime = CTime(findFileData.ftLastAccessTime);
  if (rStatus.m_ctime.GetTime() == 0) rStatus.m_ctime = rStatus.m_mtime;
  if (rStatus.m_atime.GetTime() == 0) rStatus.m_atime = rStatus.m_mtime;
  return TRUE;
}

void PASCAL CFile::SetStatus(LPCTSTR lpszFileName, const CFileStatus& status) {
  DWORD wAttr = GetFileAttributes(lpszFileName);
  if (wAttr == (DWORD)-1) CFileException::ThrowOsError((LONG)GetLastError(), lpszFileName);
  if ((DWORD)status.m_attribute != wAttr && (wAttr & readOnly)) {
    if (!SetFileAttributes(lpszFileName, (DWORD)status.m_attribute))
      CFileException::ThrowOsError((LONG)GetLastError(), lpszFileName);
  }
  if (status.m_mtime.GetTime() != 0) {
    HANDLE h = CreateFile(lpszFileName, GENERIC_READ | GENERIC_WRITE, 0, NULL,
                          OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE)
      CFileException::ThrowOsError((LONG)GetLastError(), lpszFileName);
    FILETIME ft;
    LONGLONG t = status.m_mtime.GetTime() * 10000000LL + 116444736000000000LL;
    ft.dwLowDateTime = (DWORD)t;
    ft.dwHighDateTime = (DWORD)(t >> 32);
    SetFileTime(h, NULL, NULL, &ft);
    CloseHandle(h);
  }
  if ((DWORD)status.m_attribute != wAttr && !(wAttr & readOnly)) {
    if (!SetFileAttributes(lpszFileName, (DWORD)status.m_attribute))
      CFileException::ThrowOsError((LONG)GetLastError(), lpszFileName);
  }
}

void PASCAL CFile::Rename(LPCTSTR lpszOldName, LPCTSTR lpszNewName) {
  if (!::MoveFile(lpszOldName, lpszNewName))
    CFileException::ThrowOsError((LONG)GetLastError(), lpszOldName);
}

void PASCAL CFile::Remove(LPCTSTR lpszFileName) {
  if (!::DeleteFile(lpszFileName))
    CFileException::ThrowOsError((LONG)GetLastError(), lpszFileName);
}

CString CFile::GetFileName() const {
  TCHAR sz[MAX_PATH];
  AfxGetFileName(m_strFileName, sz, MAX_PATH);
  return sz;
}

CString CFile::GetFileTitle() const {
  TCHAR sz[MAX_PATH];
  AfxGetFileTitle(m_strFileName, sz, MAX_PATH);
  return sz;
}

CString CFile::GetFilePath() const { return m_strFileName; }
void CFile::SetFilePath(LPCTSTR lpszNewName) { m_strFileName = lpszNewName; }

void CFile::AssertValid() const {}

// ---------------------------------------------------------------------
// CStdioFile
//
// The stream is always opened in binary mode, and text mode (the default,
// as in MFC) turns "\r\n" into "\n" here, so that every C library gives the
// same result.

CStdioFile::CStdioFile() : m_pStream(NULL), m_bTextMode(TRUE) {}

CStdioFile::CStdioFile(FILE* pOpenStream)
    : CFile(INVALID_HANDLE_VALUE), m_pStream(pOpenStream), m_bTextMode(TRUE) {}

CStdioFile::CStdioFile(LPCTSTR lpszFileName, UINT nOpenFlags)
    : m_pStream(NULL), m_bTextMode(TRUE) {
  CFileException e;
  if (!Open(lpszFileName, nOpenFlags, &e))
    AfxThrowFileException(e.m_cause, e.m_lOsError, e.m_strFileName);
}

CStdioFile::~CStdioFile() {
  if (m_pStream != NULL && m_bCloseOnDelete) Close();
}

BOOL CStdioFile::Open(LPCTSTR lpszFileName, UINT nOpenFlags, CFileException* pException) {
  m_pStream = NULL;
  m_bTextMode = !(nOpenFlags & typeBinary);
  // CFile::Open creates or truncates the file as asked and reports errors
  // the MFC way; the C stream is then opened on the existing file.
  if (!CFile::Open(lpszFileName, nOpenFlags & ~(UINT)(typeText | typeBinary), pException))
    return FALSE;
  ::CloseHandle(m_hFile);
  m_hFile = INVALID_HANDLE_VALUE;
  const char* mode = (nOpenFlags & 3) == modeRead ? "rb" : "r+b";
  CStringA nameA(lpszFileName);
  m_pStream = fopen(nameA, mode);
  if (m_pStream == NULL) {
    if (pException) {
      pException->m_lOsError = errno;
      pException->m_cause = CFileException::ErrnoToException(errno);
      pException->m_strFileName = lpszFileName;
    }
    m_strFileName.Empty();
    return FALSE;
  }
  m_bCloseOnDelete = TRUE;
  return TRUE;
}

UINT CStdioFile::Read(void* lpBuf, UINT nCount) {
  if (nCount == 0) return 0;
  UINT nRead = (UINT)fread(lpBuf, 1, nCount, m_pStream);
  if (nRead == 0 && ferror(m_pStream)) {
    clearerr(m_pStream);
    AfxThrowFileException(CFileException::genericException, -1, m_strFileName);
  }
  return nRead;
}

void CStdioFile::Write(const void* lpBuf, UINT nCount) {
  if (fwrite(lpBuf, 1, nCount, m_pStream) != nCount)
    AfxThrowFileException(CFileException::genericException, -1, m_strFileName);
}

void CStdioFile::WriteString(LPCTSTR lpsz) {
  CStringA s(lpsz);
  bool text = m_bTextMode != FALSE;
  for (int i = 0; i < s.GetLength(); i++) {
    if (text && s[i] == '\n') fputc('\r', m_pStream);
    if (fputc((unsigned char)s[i], m_pStream) == EOF)
      AfxThrowFileException(CFileException::diskFull, -1, m_strFileName);
  }
}

LPTSTR CStdioFile::ReadString(LPTSTR lpsz, UINT nMax) {
  CStringA s;
  int c;
  UINT n = 0;
  while (n + 1 < nMax && (c = fgetc(m_pStream)) != EOF) {
    if (c == '\r') {
      int d = fgetc(m_pStream);
      if (d == '\n') c = '\n';
      else if (d != EOF) ungetc(d, m_pStream);
    }
    s += (char)c;
    n++;
    if (c == '\n') break;
  }
  if (n == 0) return NULL;
  lstrcpyn(lpsz, CString(s), nMax);
  return lpsz;
}

BOOL CStdioFile::ReadString(CString& rString) {
  CStringA s;
  int c;
  bool any = false;
  while ((c = fgetc(m_pStream)) != EOF) {
    any = true;
    if (c == '\r') {
      int d = fgetc(m_pStream);
      if (d == '\n') break;
      if (d != EOF) ungetc(d, m_pStream);
    }
    if (c == '\n') break;
    s += (char)c;
  }
  rString = s;
  return any;
}

ULONGLONG CStdioFile::Seek(LONGLONG lOff, UINT nFrom) {
  static const int origin[] = {SEEK_SET, SEEK_CUR, SEEK_END};
  if (fseek(m_pStream, (long)lOff, origin[nFrom % 3]) != 0)
    AfxThrowFileException(CFileException::badSeek, -1, m_strFileName);
  return (ULONGLONG)ftell(m_pStream);
}

ULONGLONG CStdioFile::GetPosition() const {
  long pos = ftell(m_pStream);
  if (pos == -1) AfxThrowFileException(CFileException::invalidFile, -1, m_strFileName);
  return (ULONGLONG)pos;
}

ULONGLONG CStdioFile::GetLength() const {
  long cur = ftell(m_pStream);
  fseek(m_pStream, 0, SEEK_END);
  long len = ftell(m_pStream);
  fseek(m_pStream, cur, SEEK_SET);
  return (ULONGLONG)len;
}

void CStdioFile::Flush() {
  if (m_pStream != NULL && fflush(m_pStream) != 0)
    AfxThrowFileException(CFileException::diskFull, -1, m_strFileName);
}

void CStdioFile::Close() {
  int nErr = 0;
  if (m_pStream != NULL) nErr = fclose(m_pStream);
  m_hFile = INVALID_HANDLE_VALUE;
  m_bCloseOnDelete = FALSE;
  m_pStream = NULL;
  if (nErr != 0) AfxThrowFileException(CFileException::diskFull, -1, m_strFileName);
  m_strFileName.Empty();
}

void CStdioFile::Abort() {
  if (m_pStream != NULL && m_bCloseOnDelete) fclose(m_pStream);
  m_hFile = INVALID_HANDLE_VALUE;
  m_pStream = NULL;
  m_bCloseOnDelete = FALSE;
}

CFile* CStdioFile::Duplicate() const {
  AfxThrowNotSupportedException();
  return NULL;
}
void CStdioFile::LockRange(ULONGLONG, ULONGLONG) { AfxThrowNotSupportedException(); }
void CStdioFile::UnlockRange(ULONGLONG, ULONGLONG) { AfxThrowNotSupportedException(); }

// ---------------------------------------------------------------------
// CMemFile

CMemFile::CMemFile(UINT nGrowBytes)
    : m_nGrowBytes(nGrowBytes), m_nPosition(0), m_nBufferSize(0), m_nFileSize(0),
      m_lpBuffer(NULL), m_bAutoDelete(TRUE) {}

CMemFile::CMemFile(BYTE* lpBuffer, UINT nBufferSize, UINT nGrowBytes)
    : m_nGrowBytes(0), m_nPosition(0), m_nBufferSize(0), m_nFileSize(0), m_lpBuffer(NULL),
      m_bAutoDelete(FALSE) {
  Attach(lpBuffer, nBufferSize, nGrowBytes);
}

CMemFile::~CMemFile() {
  if (m_lpBuffer) Close();
}

void CMemFile::Attach(BYTE* lpBuffer, UINT nBufferSize, UINT nGrowBytes) {
  m_nGrowBytes = nGrowBytes;
  m_nPosition = 0;
  m_nBufferSize = nBufferSize;
  m_nFileSize = nGrowBytes == 0 ? nBufferSize : 0;
  m_lpBuffer = lpBuffer;
  m_bAutoDelete = FALSE;
}

BYTE* CMemFile::Detach() {
  BYTE* lpBuffer = m_lpBuffer;
  m_lpBuffer = NULL;
  m_nFileSize = 0;
  m_nBufferSize = 0;
  m_nPosition = 0;
  return lpBuffer;
}

BYTE* CMemFile::Alloc(SIZE_T nBytes) { return (BYTE*)malloc(nBytes); }
BYTE* CMemFile::Realloc(BYTE* lpMem, SIZE_T nBytes) { return (BYTE*)realloc(lpMem, nBytes); }
BYTE* CMemFile::Memcpy(BYTE* lpMemTarget, const BYTE* lpMemSource, SIZE_T nBytes) {
  return (BYTE*)memcpy(lpMemTarget, lpMemSource, nBytes);
}
void CMemFile::Free(BYTE* lpMem) { free(lpMem); }

ULONGLONG CMemFile::GetPosition() const { return m_nPosition; }
ULONGLONG CMemFile::GetLength() const { return m_nFileSize; }

void CMemFile::GrowFile(SIZE_T dwNewLen) {
  if (dwNewLen > m_nBufferSize) {
    if (m_nGrowBytes == 0) AfxThrowMemoryException();
    SIZE_T dwNewBufferSize = m_nBufferSize;
    while (dwNewBufferSize < dwNewLen) dwNewBufferSize += m_nGrowBytes;
    BYTE* lpNew = m_lpBuffer == NULL ? Alloc(dwNewBufferSize) : Realloc(m_lpBuffer, dwNewBufferSize);
    if (lpNew == NULL) AfxThrowMemoryException();
    m_lpBuffer = lpNew;
    m_nBufferSize = dwNewBufferSize;
  }
}

void CMemFile::SetLength(ULONGLONG dwNewLen) {
  if (dwNewLen > m_nBufferSize) GrowFile((SIZE_T)dwNewLen);
  if (dwNewLen < m_nPosition) m_nPosition = (SIZE_T)dwNewLen;
  m_nFileSize = (SIZE_T)dwNewLen;
}

UINT CMemFile::Read(void* lpBuf, UINT nCount) {
  if (nCount == 0 || m_nPosition >= m_nFileSize) return 0;
  UINT nRead = (UINT)(m_nPosition + nCount > m_nFileSize ? m_nFileSize - m_nPosition : nCount);
  Memcpy((BYTE*)lpBuf, m_lpBuffer + m_nPosition, nRead);
  m_nPosition += nRead;
  return nRead;
}

void CMemFile::Write(const void* lpBuf, UINT nCount) {
  if (nCount == 0) return;
  if (m_nPosition + nCount > m_nBufferSize) GrowFile(m_nPosition + nCount);
  Memcpy(m_lpBuffer + m_nPosition, (const BYTE*)lpBuf, nCount);
  m_nPosition += nCount;
  if (m_nPosition > m_nFileSize) m_nFileSize = m_nPosition;
}

ULONGLONG CMemFile::Seek(LONGLONG lOff, UINT nFrom) {
  LONGLONG lNewPos = (LONGLONG)m_nPosition;
  if (nFrom == begin) lNewPos = lOff;
  else if (nFrom == current) lNewPos += lOff;
  else if (nFrom == end) lNewPos = (LONGLONG)m_nFileSize + lOff;
  else return (ULONGLONG)-1;
  if (lNewPos < 0) AfxThrowFileException(CFileException::badSeek);
  m_nPosition = (SIZE_T)lNewPos;
  return m_nPosition;
}

void CMemFile::Flush() {}

void CMemFile::Close() {
  m_nGrowBytes = 0;
  m_nPosition = 0;
  m_nBufferSize = 0;
  m_nFileSize = 0;
  if (m_lpBuffer && m_bAutoDelete) Free(m_lpBuffer);
  m_lpBuffer = NULL;
}

void CMemFile::Abort() { Close(); }

void CMemFile::LockRange(ULONGLONG, ULONGLONG) { AfxThrowNotSupportedException(); }
void CMemFile::UnlockRange(ULONGLONG, ULONGLONG) { AfxThrowNotSupportedException(); }
CFile* CMemFile::Duplicate() const {
  AfxThrowNotSupportedException();
  return NULL;
}

BOOL CMemFile::GetStatus(CFileStatus& rStatus) const {
  memset(&rStatus, 0, sizeof rStatus);
  rStatus.m_size = m_nFileSize;
  return TRUE;
}

UINT CMemFile::GetBufferPtr(UINT nCommand, UINT nCount, void** ppBufStart, void** ppBufMax) {
  if (nCommand == bufferCheck) return 0;  // no direct buffering
  (void)nCount;
  (void)ppBufStart;
  (void)ppBufMax;
  return 0;
}

// ---------------------------------------------------------------------
// CFileFind

CFileFind::CFileFind()
    : m_pFoundInfo(NULL), m_pNextInfo(NULL), m_hContext(NULL), m_chDirSeparator('\\') {}

CFileFind::~CFileFind() { Close(); }

void CFileFind::Close() {
  delete m_pFoundInfo;
  m_pFoundInfo = NULL;
  delete m_pNextInfo;
  m_pNextInfo = NULL;
  if (m_hContext != NULL && m_hContext != INVALID_HANDLE_VALUE) FindClose(m_hContext);
  m_hContext = NULL;
}

BOOL CFileFind::FindFile(LPCTSTR pstrName, DWORD) {
  Close();
  if (pstrName == NULL) pstrName = _T("*.*");
  m_pNextInfo = new WIN32_FIND_DATA;
  m_hContext = ::FindFirstFile(pstrName, m_pNextInfo);
  if (m_hContext == INVALID_HANDLE_VALUE) {
    DWORD dwTemp = ::GetLastError();
    Close();
    ::SetLastError(dwTemp);
    return FALSE;
  }
  TCHAR full[MAX_PATH];
  LPTSTR filePart = NULL;
  if (GetFullPathName(pstrName, MAX_PATH, full, &filePart) == 0) {
    Close();
    return FALSE;
  }
  m_strRoot = filePart ? CString(full, (int)(filePart - full)) : CString(full);
  // The root keeps no trailing separator, except for a drive root.
  int n = m_strRoot.GetLength();
  if (n > 0 && (m_strRoot[n - 1] == '\\' || m_strRoot[n - 1] == '/') &&
      !(n == 3 && m_strRoot[1] == ':'))
    m_strRoot.Truncate(n - 1);
  return TRUE;
}

BOOL CFileFind::FindNextFile() {
  if (m_hContext == NULL) return FALSE;
  if (m_pFoundInfo == NULL) m_pFoundInfo = new WIN32_FIND_DATA;
  WIN32_FIND_DATA* pTemp = m_pFoundInfo;
  m_pFoundInfo = m_pNextInfo;
  m_pNextInfo = pTemp;
  return ::FindNextFile(m_hContext, m_pNextInfo);
}

ULONGLONG CFileFind::GetLength() const {
  if (!m_pFoundInfo) return 0;
  return ((ULONGLONG)m_pFoundInfo->nFileSizeHigh << 32) | m_pFoundInfo->nFileSizeLow;
}

CString CFileFind::GetFileName() const {
  return m_pFoundInfo ? CString(m_pFoundInfo->cFileName) : CString();
}

CString CFileFind::GetFilePath() const {
  CString strResult = m_strRoot;
  int n = strResult.GetLength();
  if (n == 0 || (strResult[n - 1] != '\\' && strResult[n - 1] != '/'))
    strResult += m_chDirSeparator;
  strResult += GetFileName();
  return strResult;
}

CString CFileFind::GetFileTitle() const {
  TCHAR sz[MAX_PATH];
  AfxGetFileTitle(GetFileName(), sz, MAX_PATH);
  return sz;
}

CString CFileFind::GetFileURL() const { return _T("file://") + GetFilePath(); }
CString CFileFind::GetRoot() const { return m_strRoot; }

BOOL CFileFind::GetLastWriteTime(FILETIME* pTimeStamp) const {
  if (!m_pFoundInfo || !pTimeStamp) return FALSE;
  *pTimeStamp = m_pFoundInfo->ftLastWriteTime;
  return TRUE;
}
BOOL CFileFind::GetLastAccessTime(FILETIME* pTimeStamp) const {
  if (!m_pFoundInfo || !pTimeStamp) return FALSE;
  *pTimeStamp = m_pFoundInfo->ftLastAccessTime;
  return TRUE;
}
BOOL CFileFind::GetCreationTime(FILETIME* pTimeStamp) const {
  if (!m_pFoundInfo || !pTimeStamp) return FALSE;
  *pTimeStamp = m_pFoundInfo->ftCreationTime;
  return TRUE;
}
BOOL CFileFind::GetLastWriteTime(CTime& refTime) const {
  if (!m_pFoundInfo) return FALSE;
  refTime = CTime(m_pFoundInfo->ftLastWriteTime);
  return TRUE;
}
BOOL CFileFind::GetLastAccessTime(CTime& refTime) const {
  if (!m_pFoundInfo) return FALSE;
  refTime = CTime(m_pFoundInfo->ftLastAccessTime);
  return TRUE;
}
BOOL CFileFind::GetCreationTime(CTime& refTime) const {
  if (!m_pFoundInfo) return FALSE;
  refTime = CTime(m_pFoundInfo->ftCreationTime);
  return TRUE;
}

BOOL CFileFind::MatchesMask(DWORD dwMask) const {
  return m_pFoundInfo != NULL && (m_pFoundInfo->dwFileAttributes & dwMask) != 0;
}

BOOL CFileFind::IsDots() const {
  if (!m_pFoundInfo || !IsDirectory()) return FALSE;
  LPCTSTR p = m_pFoundInfo->cFileName;
  return p[0] == '.' && (p[1] == 0 || (p[1] == '.' && p[2] == 0));
}
