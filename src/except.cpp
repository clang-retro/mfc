// Exceptions.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
#include "stdafx.h"

IMPLEMENT_DYNAMIC(CException, CObject)
IMPLEMENT_DYNAMIC(CSimpleException, CException)
IMPLEMENT_DYNAMIC(CMemoryException, CSimpleException)
IMPLEMENT_DYNAMIC(CNotSupportedException, CSimpleException)
IMPLEMENT_DYNAMIC(CInvalidArgException, CSimpleException)
IMPLEMENT_DYNAMIC(CUserException, CSimpleException)
IMPLEMENT_DYNAMIC(CResourceException, CSimpleException)
IMPLEMENT_DYNAMIC(CArchiveException, CException)
IMPLEMENT_DYNAMIC(CFileException, CException)

static BOOL CopyMessage(LPTSTR lpszError, UINT nMaxError, LPCTSTR text) {
  if (lpszError == NULL || nMaxError == 0) return FALSE;
  lstrcpyn(lpszError, text, nMaxError);
  return TRUE;
}

CException::CException() : m_bAutoDelete(TRUE), m_bReadyForDelete(FALSE) {}
CException::CException(BOOL bAutoDelete)
    : m_bAutoDelete(bAutoDelete), m_bReadyForDelete(FALSE) {}

void CException::Delete() {
  if (m_bAutoDelete > 0) delete this;
}

BOOL CException::GetErrorMessage(LPTSTR lpszError, UINT nMaxError, PUINT pnHelpContext) const {
  if (pnHelpContext) *pnHelpContext = 0;
  if (lpszError && nMaxError) *lpszError = 0;
  return FALSE;
}
BOOL CException::GetErrorMessage(LPTSTR lpszError, UINT nMaxError, PUINT pnHelpContext) {
  return const_cast<const CException*>(this)->GetErrorMessage(lpszError, nMaxError,
                                                              pnHelpContext);
}

int CException::ReportError(UINT nType, UINT nMessageID) {
  TCHAR szErrorMessage[512];
  CString strMessage;
  if (GetErrorMessage(szErrorMessage, _countof(szErrorMessage)))
    strMessage = szErrorMessage;
  else if (nMessageID)
    strMessage.LoadString(nMessageID);
  if (strMessage.IsEmpty()) strMessage = _T("An unsupported operation was attempted.");
  return AfxMessageBox(strMessage, nType);
}

static LPCTSTR SimpleText(const CSimpleException* e) {
  if (e->IsKindOf(RUNTIME_CLASS(CMemoryException))) return _T("Out of memory.");
  if (e->IsKindOf(RUNTIME_CLASS(CNotSupportedException)))
    return _T("An unsupported operation was attempted.");
  if (e->IsKindOf(RUNTIME_CLASS(CInvalidArgException)))
    return _T("Encountered an improper argument.");
  if (e->IsKindOf(RUNTIME_CLASS(CResourceException)))
    return _T("A required resource was unavailable.");
  return _T("Unknown error.");
}

BOOL CSimpleException::GetErrorMessage(LPTSTR lpszError, UINT nMaxError,
                                       PUINT pnHelpContext) const {
  if (pnHelpContext) *pnHelpContext = 0;
  if (m_nResourceID) {
    CString s;
    if (s.LoadString(m_nResourceID)) return CopyMessage(lpszError, nMaxError, s);
  }
  if (IsKindOf(RUNTIME_CLASS(CUserException))) {
    if (lpszError && nMaxError) *lpszError = 0;
    return FALSE;
  }
  return CopyMessage(lpszError, nMaxError, SimpleText(this));
}
BOOL CSimpleException::GetErrorMessage(LPTSTR lpszError, UINT nMaxError, PUINT pnHelpContext) {
  return const_cast<const CSimpleException*>(this)->GetErrorMessage(lpszError, nMaxError,
                                                                    pnHelpContext);
}

// The ones MFC throws often are kept as static objects, as MFC does, so
// that throwing out of memory does not need memory.
static CMemoryException g_memoryException(-1);

void AFXAPI AfxThrowMemoryException() { throw &g_memoryException; }
void AFXAPI AfxThrowNotSupportedException() { throw new CNotSupportedException; }
void AFXAPI AfxThrowInvalidArgException() { throw new CInvalidArgException; }
void AFXAPI AfxThrowUserException() { throw new CUserException; }
void AFXAPI AfxThrowResourceException() { throw new CResourceException; }

void AFXAPI AfxAbort() {
  AfxWinTerm();
  abort();
}

// ---------------------------------------------------------------------
// CArchiveException

CArchiveException::CArchiveException(int cause, LPCTSTR lpszArchiveName) : m_cause(cause) {
  m_strFileName[0] = 0;
  if (lpszArchiveName) lstrcpyn(m_strFileName, lpszArchiveName, MAX_PATH);
}

BOOL CArchiveException::GetErrorMessage(LPTSTR lpszError, UINT nMaxError,
                                        PUINT pnHelpContext) const {
  static const LPCTSTR texts[] = {
      _T("No error occurred."),
      _T("An unknown error occurred while accessing %1."),
      _T("An attempt was made to write to the reading %1."),
      _T("An attempt was made to access %1 past its end."),
      _T("An attempt was made to read from the writing %1."),
      _T("%1 has a bad format."),
      _T("%1 contained an unexpected object."),
      _T("%1 contains an incorrect schema."),
      _T("The buffer of %1 is full."),
  };
  if (pnHelpContext) *pnHelpContext = 0;
  LPCTSTR fmt = texts[(unsigned)m_cause < _countof(texts) ? m_cause : 1];
  CString name = m_strFileName[0] ? CString(m_strFileName) : CString(_T("an unnamed file"));
  CString s(fmt);
  s.Replace(_T("%1"), name);
  return CopyMessage(lpszError, nMaxError, s);
}
BOOL CArchiveException::GetErrorMessage(LPTSTR lpszError, UINT nMaxError, PUINT pnHelpContext) {
  return const_cast<const CArchiveException*>(this)->GetErrorMessage(lpszError, nMaxError,
                                                                     pnHelpContext);
}

void AFXAPI AfxThrowArchiveException(int cause, LPCTSTR lpszArchiveName) {
  throw new CArchiveException(cause, lpszArchiveName);
}

// ---------------------------------------------------------------------
// CFileException

CFileException::CFileException(int cause, LONG lOsError, LPCTSTR lpszArchiveName)
    : m_cause(cause), m_lOsError(lOsError) {
  m_strFileName[0] = 0;
  if (lpszArchiveName) lstrcpyn(m_strFileName, lpszArchiveName, MAX_PATH);
}

BOOL CFileException::GetErrorMessage(LPTSTR lpszError, UINT nMaxError,
                                     PUINT pnHelpContext) const {
  static const LPCTSTR texts[] = {
      _T("No error occurred."),
      _T("An unknown error occurred while accessing %1."),
      _T("%1 was not found."),
      _T("%1 contains an invalid path."),
      _T("%1 could not be opened because there are too many open files."),
      _T("Access to %1 was denied."),
      _T("An invalid file handle was associated with %1."),
      _T("%1 could not be removed because it is the current directory."),
      _T("%1 could not be created because the directory is full."),
      _T("Seek failed on %1"),
      _T("A hardware I/O error was reported while accessing %1."),
      _T("A sharing violation occurred while accessing %1."),
      _T("A locking violation occurred while accessing %1."),
      _T("Disk full while accessing %1."),
      _T("An attempt was made to access %1 past its end."),
  };
  if (pnHelpContext) *pnHelpContext = 0;
  LPCTSTR fmt = texts[(unsigned)m_cause < _countof(texts) ? m_cause : 1];
  CString name = m_strFileName[0] ? CString(m_strFileName) : CString(_T("an unnamed file"));
  CString s(fmt);
  s.Replace(_T("%1"), name);
  return CopyMessage(lpszError, nMaxError, s);
}
BOOL CFileException::GetErrorMessage(LPTSTR lpszError, UINT nMaxError, PUINT pnHelpContext) {
  return const_cast<const CFileException*>(this)->GetErrorMessage(lpszError, nMaxError,
                                                                  pnHelpContext);
}

int PASCAL CFileException::OsErrorToException(LONG lOsError) {
  switch (lOsError) {
  case NO_ERROR: return none;
  case ERROR_FILE_NOT_FOUND: return fileNotFound;
  case ERROR_PATH_NOT_FOUND:
  case ERROR_INVALID_DRIVE:
  case ERROR_BAD_PATHNAME:
  case ERROR_INVALID_NAME:
  case ERROR_FILENAME_EXCED_RANGE:
  case ERROR_BAD_NETPATH:
    return badPath;
  case ERROR_TOO_MANY_OPEN_FILES: return tooManyOpenFiles;
  case ERROR_ACCESS_DENIED:
  case ERROR_WRITE_PROTECT:
  case ERROR_FILE_EXISTS:
  case ERROR_ALREADY_EXISTS:
  case ERROR_NETWORK_ACCESS_DENIED:
    return accessDenied;
  case ERROR_INVALID_HANDLE: return invalidFile;
  case ERROR_CURRENT_DIRECTORY: return removeCurrentDir;
  case ERROR_CANNOT_MAKE: return directoryFull;
  case ERROR_SEEK:
  case ERROR_NEGATIVE_SEEK:
    return badSeek;
  case ERROR_WRITE_FAULT:
  case ERROR_READ_FAULT:
  case ERROR_GEN_FAILURE:
  case ERROR_CRC:
  case ERROR_SECTOR_NOT_FOUND:
  case ERROR_NOT_READY:
  case ERROR_IO_DEVICE:
    return hardIO;
  case ERROR_SHARING_VIOLATION: return sharingViolation;
  case ERROR_LOCK_VIOLATION:
  case ERROR_LOCK_FAILED:
  case ERROR_DRIVE_LOCKED:
    return lockViolation;
  case ERROR_DISK_FULL:
  case ERROR_HANDLE_DISK_FULL:
    return diskFull;
  case ERROR_HANDLE_EOF: return endOfFile;
  default: return genericException;
  }
}

int PASCAL CFileException::ErrnoToException(int nErrno) {
  switch (nErrno) {
  case 0: return none;
  case 2: return fileNotFound;     // ENOENT
  case 13: return accessDenied;    // EACCES
  case 24: return tooManyOpenFiles;  // EMFILE
  case 9: return invalidFile;      // EBADF
  case 28: return diskFull;        // ENOSPC
  case 22: return badSeek;         // EINVAL
  case 18: return badPath;         // EXDEV
  case 36: return lockViolation;   // EDEADLOCK
  default: return genericException;
  }
}

void PASCAL CFileException::ThrowOsError(LONG lOsError, LPCTSTR lpszFileName) {
  if (lOsError != 0) AfxThrowFileException(OsErrorToException(lOsError), lOsError, lpszFileName);
}

void PASCAL CFileException::ThrowErrno(int nErrno, LPCTSTR lpszFileName) {
  if (nErrno != 0) AfxThrowFileException(ErrnoToException(nErrno), nErrno, lpszFileName);
}

void AFXAPI AfxThrowFileException(int cause, LONG lOsError, LPCTSTR lpszFileName) {
  throw new CFileException(cause, lOsError, lpszFileName);
}
