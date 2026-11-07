// inet.cpp - the internet classes over WinINet: URL parsing,
// CInternetException, CInternetSession, the connections, CInternetFile
// and CHttpFile, gopher locators and the FTP and gopher finders.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include "afximpl.h"
#include <afxwin.h>
#include <afxinet.h>

IMPLEMENT_DYNAMIC(CInternetException, CException)
IMPLEMENT_DYNAMIC(CInternetSession, CObject)
IMPLEMENT_DYNAMIC(CInternetConnection, CObject)
IMPLEMENT_DYNAMIC(CFtpConnection, CInternetConnection)
IMPLEMENT_DYNAMIC(CHttpConnection, CInternetConnection)
IMPLEMENT_DYNAMIC(CGopherConnection, CInternetConnection)
IMPLEMENT_DYNAMIC(CInternetFile, CStdioFile)
IMPLEMENT_DYNAMIC(CHttpFile, CInternetFile)
IMPLEMENT_DYNAMIC(CGopherFile, CInternetFile)
IMPLEMENT_DYNAMIC(CFtpFileFind, CFileFind)
IMPLEMENT_DYNAMIC(CGopherFileFind, CFileFind)

// ---------------------------------------------------------------------
// URLs

BOOL AFXAPI AfxParseURLEx(LPCTSTR pstrURL, DWORD& dwServiceType, CString& strServer,
                          CString& strObject, INTERNET_PORT& nPort, CString& strUsername,
                          CString& strPassword, DWORD dwFlags) {
  dwServiceType = AFX_INET_SERVICE_UNK;
  strServer.Empty();
  strObject.Empty();
  strUsername.Empty();
  strPassword.Empty();
  nPort = 0;
  if (pstrURL == NULL || *pstrURL == 0) return FALSE;

  // Room for every part; none can be longer than the URL.
  int nLen = lstrlen(pstrURL) + 1;
  CString strScheme, strHost, strUser, strPass, strPath, strExtra;
  URL_COMPONENTS uc;
  memset(&uc, 0, sizeof(uc));
  uc.dwStructSize = sizeof(uc);
  uc.lpszScheme = strScheme.GetBuffer(nLen);
  uc.dwSchemeLength = nLen;
  uc.lpszHostName = strHost.GetBuffer(nLen);
  uc.dwHostNameLength = nLen;
  uc.lpszUserName = strUser.GetBuffer(nLen);
  uc.dwUserNameLength = nLen;
  uc.lpszPassword = strPass.GetBuffer(nLen);
  uc.dwPasswordLength = nLen;
  uc.lpszUrlPath = strPath.GetBuffer(nLen);
  uc.dwUrlPathLength = nLen;
  uc.lpszExtraInfo = strExtra.GetBuffer(nLen);
  uc.dwExtraInfoLength = nLen;
  BOOL bOK = ::InternetCrackUrl(pstrURL, 0, dwFlags, &uc);
  strScheme.ReleaseBuffer(bOK ? (int)uc.dwSchemeLength : 0);
  strHost.ReleaseBuffer(bOK ? (int)uc.dwHostNameLength : 0);
  strUser.ReleaseBuffer(bOK ? (int)uc.dwUserNameLength : 0);
  strPass.ReleaseBuffer(bOK ? (int)uc.dwPasswordLength : 0);
  strPath.ReleaseBuffer(bOK ? (int)uc.dwUrlPathLength : 0);
  strExtra.ReleaseBuffer(bOK ? (int)uc.dwExtraInfoLength : 0);
  if (!bOK) return FALSE;

  switch (uc.nScheme) {
  case INTERNET_SCHEME_HTTP: dwServiceType = AFX_INET_SERVICE_HTTP; break;
  case INTERNET_SCHEME_HTTPS: dwServiceType = AFX_INET_SERVICE_HTTPS; break;
  case INTERNET_SCHEME_FTP: dwServiceType = AFX_INET_SERVICE_FTP; break;
  case INTERNET_SCHEME_GOPHER: dwServiceType = AFX_INET_SERVICE_GOPHER; break;
  case INTERNET_SCHEME_FILE: dwServiceType = AFX_INET_SERVICE_FILE; break;
  case INTERNET_SCHEME_NEWS: dwServiceType = AFX_INET_SERVICE_NEWS; break;
  case INTERNET_SCHEME_MAILTO: dwServiceType = AFX_INET_SERVICE_MAILTO; break;
  default: {
    // The schemes WinINet has no number for.
    static const struct {
      LPCTSTR pszName;
      DWORD dwService;
    } others[] = {
        {_T("mid"), AFX_INET_SERVICE_MID},       {_T("cid"), AFX_INET_SERVICE_CID},
        {_T("nntp"), AFX_INET_SERVICE_NNTP},     {_T("prospero"), AFX_INET_SERVICE_PROSPERO},
        {_T("telnet"), AFX_INET_SERVICE_TELNET}, {_T("wais"), AFX_INET_SERVICE_WAIS},
        {_T("afs"), AFX_INET_SERVICE_AFS},
    };
    for (size_t i = 0; i < _countof(others); i++)
      if (strScheme.CompareNoCase(others[i].pszName) == 0) dwServiceType = others[i].dwService;
    break;
  }
  }
  strServer = strHost;
  strObject = strPath + strExtra;
  if (strObject.IsEmpty() && dwServiceType != AFX_INET_SERVICE_MAILTO) strObject = _T("/");
  strUsername = strUser;
  strPassword = strPass;
  nPort = uc.nPort;
  return TRUE;
}

BOOL AFXAPI AfxParseURL(LPCTSTR pstrURL, DWORD& dwServiceType, CString& strServer,
                        CString& strObject, INTERNET_PORT& nPort) {
  CString strUser, strPass;
  return AfxParseURLEx(pstrURL, dwServiceType, strServer, strObject, nPort, strUser, strPass, 0);
}

// ---------------------------------------------------------------------
// CInternetException

CInternetException::CInternetException(DWORD dwError) : m_dwContext(0), m_dwError(dwError) {}

CInternetException::~CInternetException() {}

BOOL CInternetException::GetErrorMessage(LPTSTR lpstrError, UINT nMaxError,
                                         PUINT pnHelpContext) const {
  if (pnHelpContext != NULL) *pnHelpContext = 0;
  if (lpstrError == NULL || nMaxError == 0) return FALSE;
  *lpstrError = 0;
  if (m_dwError == ERROR_INTERNET_EXTENDED_ERROR && !m_strResponse.IsEmpty()) {
    lstrcpyn(lpstrError, m_strResponse, nMaxError);
    return TRUE;
  }
  // WinINet's messages are in its own module.
  DWORD n = ::FormatMessage(FORMAT_MESSAGE_FROM_HMODULE | FORMAT_MESSAGE_IGNORE_INSERTS,
                            ::GetModuleHandle(_T("wininet.dll")), m_dwError, 0, lpstrError,
                            nMaxError, NULL);
  if (n == 0)
    n = ::FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL,
                        m_dwError, 0, lpstrError, nMaxError, NULL);
  if (n == 0) {
    wsprintf(lpstrError, _T("Internet error %lu"), (unsigned long)m_dwError);
    return TRUE;
  }
  // Without the line break FormatMessage ends with.
  while (n > 0 && (lpstrError[n - 1] == '\n' || lpstrError[n - 1] == '\r')) lpstrError[--n] = 0;
  return TRUE;
}

BOOL CInternetException::GetErrorMessage(LPTSTR lpstrError, UINT nMaxError,
                                         PUINT pnHelpContext) {
  return const_cast<const CInternetException*>(this)->GetErrorMessage(lpstrError, nMaxError,
                                                                      pnHelpContext);
}

void AFXAPI AfxThrowInternetException(DWORD_PTR dwContext, DWORD dwError) {
  if (dwError == 0) dwError = ::GetLastError();
  CInternetException* pException = new CInternetException(dwError);
  pException->m_dwContext = dwContext;
  if (dwError == ERROR_INTERNET_EXTENDED_ERROR) {
    DWORD dwCode = 0, dwLen = 0;
    ::InternetGetLastResponseInfo(&dwCode, NULL, &dwLen);
    if (dwLen > 0) {
      dwLen++;
      ::InternetGetLastResponseInfo(&dwCode, pException->m_strResponse.GetBuffer(dwLen), &dwLen);
      pException->m_strResponse.ReleaseBuffer();
      pException->m_strResponse.TrimRight();
    }
  }
  throw pException;
}

// ---------------------------------------------------------------------
// Options, shared by the session, the connections and the files.

static BOOL QueryStringOption(HINTERNET h, DWORD dwOption, CString& str) {
  DWORD dwLen = 0;
  ::InternetQueryOption(h, dwOption, NULL, &dwLen);
  if (dwLen == 0) return FALSE;
  // dwLen is in bytes.
  DWORD cch = dwLen / sizeof(TCHAR) + 1;
  LPTSTR p = str.GetBuffer(cch);
  dwLen = cch * sizeof(TCHAR);
  BOOL bOK = ::InternetQueryOption(h, dwOption, p, &dwLen);
  str.ReleaseBuffer(bOK ? -1 : 0);
  return bOK;
}

static BOOL QueryDwordOption(HINTERNET h, DWORD dwOption, DWORD& dwValue) {
  DWORD dwLen = sizeof(dwValue);
  return ::InternetQueryOption(h, dwOption, &dwValue, &dwLen);
}

// ---------------------------------------------------------------------
// CInternetSession

// The sessions whose status callback is on, for the callback to find.
static CPtrList* g_pCallbackSessions = NULL;
static CRITICAL_SECTION g_csSessions;
static LONG g_nSessionsInit = 0;

static void LockSessions() {
  if (::InterlockedExchange(&g_nSessionsInit, 1) == 0) ::InitializeCriticalSection(&g_csSessions);
  ::EnterCriticalSection(&g_csSessions);
}

static void UnlockSessions() { ::LeaveCriticalSection(&g_csSessions); }

CInternetSession* PASCAL CInternetSession::FromInternetHandle(HINTERNET hInternet) {
  CInternetSession* pFound = NULL;
  LockSessions();
  if (g_pCallbackSessions != NULL) {
    // Up from a request or connection to its session handle.
    HINTERNET h = hInternet;
    for (int nDepth = 0; h != NULL && pFound == NULL && nDepth < 8; nDepth++) {
      for (POSITION pos = g_pCallbackSessions->GetHeadPosition(); pos != NULL;) {
        CInternetSession* p = (CInternetSession*)g_pCallbackSessions->GetNext(pos);
        if (p->m_hSession == h) pFound = p;
      }
      HINTERNET hParent = NULL;
      DWORD dwLen = sizeof(hParent);
      if (pFound != NULL ||
          !::InternetQueryOption(h, INTERNET_OPTION_PARENT_HANDLE, &hParent, &dwLen))
        break;
      h = hParent;
    }
    if (pFound == NULL && g_pCallbackSessions->GetCount() == 1)
      pFound = (CInternetSession*)g_pCallbackSessions->GetHead();
  }
  UnlockSessions();
  return pFound;
}

static void CALLBACK AfxInternetStatusCallback(HINTERNET hInternet, DWORD_PTR dwContext,
                                               DWORD dwInternetStatus, LPVOID lpvStatusInformation,
                                               DWORD dwStatusInformationLength) {
  CInternetSession* pSession = CInternetSession::FromInternetHandle(hInternet);
  if (pSession != NULL)
    pSession->OnStatusCallback(dwContext, dwInternetStatus, lpvStatusInformation,
                               dwStatusInformationLength);
}

CInternetSession::CInternetSession(LPCTSTR pstrAgent, DWORD_PTR dwContext, DWORD dwAccessType,
                                   LPCTSTR pstrProxyName, LPCTSTR pstrProxyBypass,
                                   DWORD dwFlags)
    : m_dwContext(dwContext), m_hSession(NULL), m_pOldCallback(NULL), m_bCallbackEnabled(FALSE) {
  // The agent defaults to the program's name.
  CString strAgent;
  if (pstrAgent == NULL) {
    TCHAR szPath[MAX_PATH];
    DWORD n = ::GetModuleFileName(NULL, szPath, MAX_PATH);
    strAgent = n > 0 && n < MAX_PATH ? szPath : _T("MFC");
    int i = strAgent.ReverseFind('\\');
    if (i >= 0) strAgent = strAgent.Mid(i + 1);
    i = strAgent.ReverseFind('.');
    if (i > 0) strAgent = strAgent.Left(i);
    pstrAgent = strAgent;
  }
  m_hSession = ::InternetOpen(pstrAgent, dwAccessType, pstrProxyName, pstrProxyBypass, dwFlags);
  if (m_hSession == NULL) AfxThrowInternetException(m_dwContext);
}

CInternetSession::~CInternetSession() { Close(); }

void CInternetSession::Close() {
  if (m_bCallbackEnabled) EnableStatusCallback(FALSE);
  if (m_hSession != NULL) {
    ::InternetCloseHandle(m_hSession);
    m_hSession = NULL;
  }
}

BOOL CInternetSession::EnableStatusCallback(BOOL bEnable) {
  if (m_hSession == NULL) return FALSE;
  if (bEnable == m_bCallbackEnabled) return TRUE;
  LockSessions();
  if (bEnable) {
    if (g_pCallbackSessions == NULL) g_pCallbackSessions = new CPtrList;
    g_pCallbackSessions->AddTail(this);
  } else if (g_pCallbackSessions != NULL) {
    POSITION pos = g_pCallbackSessions->Find(this);
    if (pos != NULL) g_pCallbackSessions->RemoveAt(pos);
    if (g_pCallbackSessions->IsEmpty()) {
      delete g_pCallbackSessions;
      g_pCallbackSessions = NULL;
    }
  }
  UnlockSessions();
  INTERNET_STATUS_CALLBACK pOld = ::InternetSetStatusCallback(
      m_hSession, bEnable ? AfxInternetStatusCallback : m_pOldCallback);
  if (pOld == INTERNET_INVALID_STATUS_CALLBACK) return FALSE;
  if (bEnable) m_pOldCallback = pOld;
  m_bCallbackEnabled = bEnable;
  return TRUE;
}

void CInternetSession::OnStatusCallback(DWORD_PTR, DWORD, LPVOID, DWORD) {}

BOOL CInternetSession::QueryOption(DWORD dwOption, LPVOID lpBuffer, LPDWORD lpdwBufLen) const {
  return ::InternetQueryOption(m_hSession, dwOption, lpBuffer, lpdwBufLen);
}

BOOL CInternetSession::QueryOption(DWORD dwOption, DWORD& dwValue) const {
  return QueryDwordOption(m_hSession, dwOption, dwValue);
}

BOOL CInternetSession::QueryOption(DWORD dwOption, CString& refString) const {
  return QueryStringOption(m_hSession, dwOption, refString);
}

BOOL CInternetSession::SetOption(DWORD dwOption, LPVOID lpBuffer, DWORD dwBufferLength, DWORD) {
  return ::InternetSetOption(m_hSession, dwOption, lpBuffer, dwBufferLength);
}

BOOL CInternetSession::SetOption(DWORD dwOption, DWORD dwValue, DWORD) {
  return ::InternetSetOption(m_hSession, dwOption, &dwValue, sizeof(dwValue));
}

CStdioFile* CInternetSession::OpenURL(LPCTSTR pstrURL, DWORD_PTR dwContext, DWORD dwFlags,
                                      LPCTSTR pstrHeaders, DWORD dwHeadersLength) {
  DWORD dwService;
  CString strServer, strObject;
  INTERNET_PORT nPort;
  if (!AfxParseURL(pstrURL, dwService, strServer, strObject, nPort))
    AfxThrowInternetException(dwContext, ERROR_INTERNET_INVALID_URL);

  if (dwService == AFX_INET_SERVICE_FILE) {
    // A local file, opened directly. "/C:/dir/x" is "C:/dir/x".
    CString strPath = strObject;
    if (strPath.GetLength() >= 3 && (strPath[0] == '/' || strPath[0] == '\\') && strPath[2] == ':')
      strPath = strPath.Mid(1);
    CStdioFile* pFile = new CStdioFile;
    CFileException fe;
    if (!pFile->Open(strPath, CFile::modeRead | CFile::shareDenyWrite | CFile::typeBinary, &fe)) {
      delete pFile;
      AfxThrowFileException(fe.m_cause, fe.m_lOsError, strPath);
    }
    return pFile;
  }
  if (dwService != AFX_INET_SERVICE_HTTP && dwService != AFX_INET_SERVICE_HTTPS &&
      dwService != AFX_INET_SERVICE_FTP && dwService != AFX_INET_SERVICE_GOPHER)
    AfxThrowInternetException(dwContext, ERROR_INTERNET_UNRECOGNIZED_SCHEME);

  // HTTP takes the headers as they are; the other services take none.
  if (pstrHeaders != NULL && dwHeadersLength == 0) dwHeadersLength = (DWORD)-1;
  HINTERNET hFile = ::InternetOpenUrl(m_hSession, pstrURL, pstrHeaders, dwHeadersLength, dwFlags,
                                      dwContext);
  if (hFile == NULL) AfxThrowInternetException(dwContext);

  switch (dwService) {
  case AFX_INET_SERVICE_HTTP:
  case AFX_INET_SERVICE_HTTPS:
    return new CHttpFile(hFile, m_hSession, strObject, strServer, _T("GET"), dwContext);
  case AFX_INET_SERVICE_GOPHER:
    return new CGopherFile(hFile, m_hSession, strObject, 0, dwContext);
  default:
    return new CInternetFile(hFile, m_hSession, strObject, strServer, dwContext, TRUE);
  }
}

CFtpConnection* CInternetSession::GetFtpConnection(LPCTSTR pstrServer, LPCTSTR pstrUserName,
                                                   LPCTSTR pstrPassword, INTERNET_PORT nPort,
                                                   BOOL bPassive) {
  return new CFtpConnection(this, pstrServer, pstrUserName, pstrPassword, m_dwContext, nPort,
                            bPassive);
}

CHttpConnection* CInternetSession::GetHttpConnection(LPCTSTR pstrServer, INTERNET_PORT nPort,
                                                     LPCTSTR pstrUserName, LPCTSTR pstrPassword) {
  return new CHttpConnection(this, pstrServer, nPort, pstrUserName, pstrPassword, m_dwContext);
}

CHttpConnection* CInternetSession::GetHttpConnection(LPCTSTR pstrServer, DWORD dwFlags,
                                                     INTERNET_PORT nPort, LPCTSTR pstrUserName,
                                                     LPCTSTR pstrPassword) {
  return new CHttpConnection(this, pstrServer, dwFlags, nPort, pstrUserName, pstrPassword,
                             m_dwContext);
}

CGopherConnection* CInternetSession::GetGopherConnection(LPCTSTR pstrServer, LPCTSTR pstrUserName,
                                                         LPCTSTR pstrPassword,
                                                         INTERNET_PORT nPort) {
  return new CGopherConnection(this, pstrServer, pstrUserName, pstrPassword, m_dwContext, nPort);
}

DWORD CInternetSession::ServiceTypeFromHandle(HINTERNET hQuery) {
  DWORD dwType = 0;
  if (!QueryDwordOption(hQuery, INTERNET_OPTION_HANDLE_TYPE, dwType)) return AFX_INET_SERVICE_UNK;
  switch (dwType) {
  case INTERNET_HANDLE_TYPE_CONNECT_FTP:
  case INTERNET_HANDLE_TYPE_FTP_FIND:
  case INTERNET_HANDLE_TYPE_FTP_FIND_HTML:
  case INTERNET_HANDLE_TYPE_FTP_FILE:
  case INTERNET_HANDLE_TYPE_FTP_FILE_HTML:
    return AFX_INET_SERVICE_FTP;
  case INTERNET_HANDLE_TYPE_CONNECT_GOPHER:
  case INTERNET_HANDLE_TYPE_GOPHER_FIND:
  case INTERNET_HANDLE_TYPE_GOPHER_FIND_HTML:
  case INTERNET_HANDLE_TYPE_GOPHER_FILE:
  case INTERNET_HANDLE_TYPE_GOPHER_FILE_HTML:
    return AFX_INET_SERVICE_GOPHER;
  case INTERNET_HANDLE_TYPE_CONNECT_HTTP:
  case INTERNET_HANDLE_TYPE_HTTP_REQUEST:
    return AFX_INET_SERVICE_HTTP;
  default:
    return AFX_INET_SERVICE_UNK;
  }
}

BOOL CInternetSession::SetCookie(LPCTSTR pstrUrl, LPCTSTR pstrCookieName,
                                 LPCTSTR pstrCookieData) {
  return ::InternetSetCookie(pstrUrl, pstrCookieName, pstrCookieData);
}

BOOL CInternetSession::GetCookie(LPCTSTR pstrUrl, LPCTSTR pstrCookieName, LPTSTR pstrCookieData,
                                 DWORD dwBufLen) {
  return ::InternetGetCookie(pstrUrl, pstrCookieName, pstrCookieData, &dwBufLen);
}

DWORD CInternetSession::GetCookieLength(LPCTSTR pstrUrl, LPCTSTR pstrCookieName) {
  DWORD dwLen = 0;
  if (!::InternetGetCookie(pstrUrl, pstrCookieName, NULL, &dwLen)) return 0;
  return dwLen;
}

BOOL CInternetSession::GetCookie(LPCTSTR pstrUrl, LPCTSTR pstrCookieName, CString& strCookieData) {
  strCookieData.Empty();
  DWORD dwLen = GetCookieLength(pstrUrl, pstrCookieName);
  if (dwLen == 0) return FALSE;
  // dwLen counts bytes, with the terminator.
  DWORD cch = dwLen / sizeof(TCHAR) + 1;
  dwLen = cch * sizeof(TCHAR);
  BOOL bOK = ::InternetGetCookie(pstrUrl, pstrCookieName, strCookieData.GetBuffer(cch), &dwLen);
  strCookieData.ReleaseBuffer(bOK ? -1 : 0);
  return bOK;
}

// ---------------------------------------------------------------------
// CInternetConnection

CInternetConnection::CInternetConnection(CInternetSession* pSession, LPCTSTR pstrServer,
                                         INTERNET_PORT nPort, DWORD_PTR dwContext)
    : m_hConnection(NULL),
      m_dwContext(dwContext),
      m_pSession(pSession),
      m_strServerName(pstrServer),
      m_nPort(nPort) {}

CInternetConnection::~CInternetConnection() { Close(); }

void CInternetConnection::Connect(DWORD dwService, LPCTSTR pstrUserName, LPCTSTR pstrPassword,
                                  DWORD dwFlags) {
  m_hConnection = ::InternetConnect((HINTERNET)*m_pSession, m_strServerName, m_nPort,
                                    pstrUserName, pstrPassword, dwService, dwFlags, m_dwContext);
  if (m_hConnection == NULL) AfxThrowInternetException(m_dwContext);
}

void CInternetConnection::Close() {
  if (m_hConnection != NULL) {
    ::InternetCloseHandle(m_hConnection);
    m_hConnection = NULL;
  }
}

BOOL CInternetConnection::QueryOption(DWORD dwOption, LPVOID lpBuffer, LPDWORD lpdwBufLen) const {
  return ::InternetQueryOption(m_hConnection, dwOption, lpBuffer, lpdwBufLen);
}

BOOL CInternetConnection::QueryOption(DWORD dwOption, DWORD& dwValue) const {
  return QueryDwordOption(m_hConnection, dwOption, dwValue);
}

BOOL CInternetConnection::QueryOption(DWORD dwOption, CString& refString) const {
  return QueryStringOption(m_hConnection, dwOption, refString);
}

BOOL CInternetConnection::SetOption(DWORD dwOption, LPVOID lpBuffer, DWORD dwBufferLength,
                                    DWORD) {
  return ::InternetSetOption(m_hConnection, dwOption, lpBuffer, dwBufferLength);
}

BOOL CInternetConnection::SetOption(DWORD dwOption, DWORD dwValue, DWORD) {
  return ::InternetSetOption(m_hConnection, dwOption, &dwValue, sizeof(dwValue));
}

// ---------------------------------------------------------------------
// CFtpConnection

CFtpConnection::CFtpConnection(CInternetSession* pSession, LPCTSTR pstrServer,
                               LPCTSTR pstrUserName, LPCTSTR pstrPassword, DWORD_PTR dwContext,
                               INTERNET_PORT nPort, BOOL bPassive)
    : CInternetConnection(pSession, pstrServer, nPort, dwContext) {
  Connect(INTERNET_SERVICE_FTP, pstrUserName, pstrPassword, bPassive ? INTERNET_FLAG_PASSIVE : 0);
}

CFtpConnection::CFtpConnection(CInternetSession* pSession, HINTERNET hConnected,
                               LPCTSTR pstrServer, DWORD_PTR dwContext)
    : CInternetConnection(pSession, pstrServer, INTERNET_INVALID_PORT_NUMBER, dwContext) {
  m_hConnection = hConnected;
}

CFtpConnection::~CFtpConnection() {}

BOOL CFtpConnection::SetCurrentDirectory(LPCTSTR pstrDirName) {
  return ::FtpSetCurrentDirectory(m_hConnection, pstrDirName);
}

BOOL CFtpConnection::GetCurrentDirectory(LPTSTR pstrDirName, LPDWORD lpdwLen) const {
  return ::FtpGetCurrentDirectory(m_hConnection, pstrDirName, lpdwLen);
}

BOOL CFtpConnection::GetCurrentDirectory(CString& strDirName) const {
  DWORD dwLen = INTERNET_MAX_PATH_LENGTH;
  BOOL bOK = GetCurrentDirectory(strDirName.GetBuffer(dwLen), &dwLen);
  // The length may or may not count the terminator, so it is not used.
  strDirName.ReleaseBuffer(bOK ? -1 : 0);
  return bOK;
}

BOOL CFtpConnection::GetCurrentDirectoryAsURL(CString& strDirName) const {
  CString strDir;
  if (!GetCurrentDirectory(strDir)) return FALSE;
  strDirName = _T("ftp://") + m_strServerName;
  if (m_nPort != INTERNET_INVALID_PORT_NUMBER && m_nPort != INTERNET_DEFAULT_FTP_PORT) {
    CString strPort;
    strPort.Format(_T(":%u"), (unsigned)m_nPort);
    strDirName += strPort;
  }
  if (strDir.IsEmpty() || strDir[0] != '/') strDirName += '/';
  strDirName += strDir;
  return TRUE;
}

BOOL CFtpConnection::GetCurrentDirectoryAsURL(LPTSTR pstrName, LPDWORD lpdwLen) const {
  CString str;
  if (lpdwLen == NULL || !GetCurrentDirectoryAsURL(str)) return FALSE;
  if ((DWORD)str.GetLength() + 1 > *lpdwLen) {
    *lpdwLen = str.GetLength() + 1;
    ::SetLastError(ERROR_INSUFFICIENT_BUFFER);
    return FALSE;
  }
  lstrcpy(pstrName, str);
  *lpdwLen = str.GetLength();
  return TRUE;
}

BOOL CFtpConnection::RemoveDirectory(LPCTSTR pstrDirName) {
  return ::FtpRemoveDirectory(m_hConnection, pstrDirName);
}

BOOL CFtpConnection::CreateDirectory(LPCTSTR pstrDirName) {
  return ::FtpCreateDirectory(m_hConnection, pstrDirName);
}

BOOL CFtpConnection::Rename(LPCTSTR pstrExisting, LPCTSTR pstrNew) {
  return ::FtpRenameFile(m_hConnection, pstrExisting, pstrNew);
}

BOOL CFtpConnection::Remove(LPCTSTR pstrFileName) {
  return ::FtpDeleteFile(m_hConnection, pstrFileName);
}

BOOL CFtpConnection::PutFile(LPCTSTR pstrLocalFile, LPCTSTR pstrRemoteFile, DWORD dwFlags,
                             DWORD_PTR dwContext) {
  return ::FtpPutFile(m_hConnection, pstrLocalFile, pstrRemoteFile, dwFlags, dwContext);
}

BOOL CFtpConnection::GetFile(LPCTSTR pstrRemoteFile, LPCTSTR pstrLocalFile, BOOL bFailIfExists,
                             DWORD dwAttributes, DWORD dwFlags, DWORD_PTR dwContext) {
  return ::FtpGetFile(m_hConnection, pstrRemoteFile, pstrLocalFile, bFailIfExists, dwAttributes,
                      dwFlags, dwContext);
}

CInternetFile* CFtpConnection::OpenFile(LPCTSTR pstrFileName, DWORD dwAccess, DWORD dwFlags,
                                        DWORD_PTR dwContext) {
  HINTERNET hFile = ::FtpOpenFile(m_hConnection, pstrFileName, dwAccess, dwFlags, dwContext);
  if (hFile == NULL) AfxThrowInternetException(dwContext);
  CInternetFile* pFile = new CInternetFile(hFile, pstrFileName, this, dwAccess == GENERIC_READ);
  pFile->m_dwContext = dwContext;
  return pFile;
}

// ---------------------------------------------------------------------
// CHttpConnection

const LPCTSTR CHttpConnection::szHtmlVerbs[] = {
    _T("POST"), _T("GET"), _T("HEAD"), _T("PUT"), _T("LINK"), _T("DELETE"), _T("UNLINK"),
};

CHttpConnection::CHttpConnection(CInternetSession* pSession, LPCTSTR pstrServer,
                                 INTERNET_PORT nPort, LPCTSTR pstrUserName, LPCTSTR pstrPassword,
                                 DWORD_PTR dwContext)
    : CInternetConnection(pSession, pstrServer, nPort, dwContext), m_dwFlags(0) {
  Connect(INTERNET_SERVICE_HTTP, pstrUserName, pstrPassword, 0);
}

CHttpConnection::CHttpConnection(CInternetSession* pSession, LPCTSTR pstrServer, DWORD dwFlags,
                                 INTERNET_PORT nPort, LPCTSTR pstrUserName, LPCTSTR pstrPassword,
                                 DWORD_PTR dwContext)
    : CInternetConnection(pSession, pstrServer, nPort, dwContext), m_dwFlags(dwFlags) {
  Connect(INTERNET_SERVICE_HTTP, pstrUserName, pstrPassword, dwFlags);
}

CHttpConnection::CHttpConnection(CInternetSession* pSession, HINTERNET hConnected,
                                 LPCTSTR pstrServer, DWORD_PTR dwContext)
    : CInternetConnection(pSession, pstrServer, INTERNET_INVALID_PORT_NUMBER, dwContext),
      m_dwFlags(0) {
  m_hConnection = hConnected;
}

CHttpConnection::~CHttpConnection() {}

CHttpFile* CHttpConnection::OpenRequest(LPCTSTR pstrVerb, LPCTSTR pstrObjectName,
                                        LPCTSTR pstrReferer, DWORD_PTR dwContext,
                                        LPCTSTR* ppstrAcceptTypes, LPCTSTR pstrVersion,
                                        DWORD dwFlags) {
  // A connection made for https makes https requests.
  dwFlags |= m_dwFlags & INTERNET_FLAG_SECURE;
  HINTERNET hFile = ::HttpOpenRequest(m_hConnection, pstrVerb, pstrObjectName, pstrVersion,
                                      pstrReferer, ppstrAcceptTypes, dwFlags, dwContext);
  if (hFile == NULL) AfxThrowInternetException(dwContext);
  CHttpFile* pFile = new CHttpFile(hFile, pstrVerb, pstrObjectName, this);
  pFile->m_dwContext = dwContext;
  return pFile;
}

CHttpFile* CHttpConnection::OpenRequest(int nVerb, LPCTSTR pstrObjectName, LPCTSTR pstrReferer,
                                        DWORD_PTR dwContext, LPCTSTR* ppstrAcceptTypes,
                                        LPCTSTR pstrVersion, DWORD dwFlags) {
  if (nVerb < _HTTP_VERB_MIN || nVerb > _HTTP_VERB_MAX)
    AfxThrowInternetException(dwContext, ERROR_INVALID_PARAMETER);
  return OpenRequest(szHtmlVerbs[nVerb], pstrObjectName, pstrReferer, dwContext, ppstrAcceptTypes,
                     pstrVersion, dwFlags);
}

// ---------------------------------------------------------------------
// CGopherConnection and CGopherLocator

CGopherLocator::CGopherLocator(LPCTSTR pstrLocator, DWORD dwLocLen)
    : m_strLocator(pstrLocator), m_dwBufferLength(dwLocLen) {}

CGopherLocator::CGopherLocator(const CGopherLocator& ref)
    : CObject(), m_strLocator(ref.m_strLocator), m_dwBufferLength(ref.m_dwBufferLength) {}

CGopherLocator::~CGopherLocator() {}

CGopherLocator& CGopherLocator::operator=(const CGopherLocator& ref) {
  m_strLocator = ref.m_strLocator;
  m_dwBufferLength = ref.m_dwBufferLength;
  return *this;
}

BOOL CGopherLocator::GetLocatorType(DWORD& dwRef) const {
  return ::GopherGetLocatorType(m_strLocator, &dwRef);
}

CGopherConnection::CGopherConnection(CInternetSession* pSession, LPCTSTR pstrServer,
                                     LPCTSTR pstrUserName, LPCTSTR pstrPassword,
                                     DWORD_PTR dwContext, INTERNET_PORT nPort)
    : CInternetConnection(pSession, pstrServer, nPort, dwContext) {
  Connect(INTERNET_SERVICE_GOPHER, pstrUserName, pstrPassword, 0);
}

CGopherConnection::CGopherConnection(CInternetSession* pSession, HINTERNET hConnected,
                                     LPCTSTR pstrServer, DWORD_PTR dwContext)
    : CInternetConnection(pSession, pstrServer, INTERNET_INVALID_PORT_NUMBER, dwContext) {
  m_hConnection = hConnected;
}

CGopherConnection::~CGopherConnection() {}

static CGopherLocator MakeLocator(LPCTSTR pstrServer, INTERNET_PORT nPort, LPCTSTR pstrDisplay,
                                  LPCTSTR pstrSelector, DWORD dwType) {
  TCHAR sz[MAX_GOPHER_LOCATOR_LENGTH + 1];
  DWORD dwLen = MAX_GOPHER_LOCATOR_LENGTH;
  if (!::GopherCreateLocator(pstrServer, nPort, pstrDisplay, pstrSelector, dwType, sz, &dwLen))
    AfxThrowInternetException(0);
  return CGopherLocator(sz, dwLen);
}

CGopherLocator CGopherConnection::CreateLocator(LPCTSTR pstrDisplayString,
                                                LPCTSTR pstrSelectorString, DWORD dwGopherType) {
  return MakeLocator(m_strServerName, m_nPort, pstrDisplayString, pstrSelectorString,
                     dwGopherType);
}

CGopherLocator CGopherConnection::CreateLocator(LPCTSTR pstrLocator) {
  return CGopherLocator(pstrLocator, lstrlen(pstrLocator) + 1);
}

CGopherLocator CGopherConnection::CreateLocator(LPCTSTR pstrServerName, LPCTSTR pstrDisplayString,
                                                LPCTSTR pstrSelectorString, DWORD dwGopherType,
                                                INTERNET_PORT nPort) {
  return MakeLocator(pstrServerName, nPort, pstrDisplayString, pstrSelectorString, dwGopherType);
}

CGopherFile* CGopherConnection::OpenFile(CGopherLocator& refLocator, DWORD dwFlags,
                                         LPCTSTR pstrView, DWORD_PTR dwContext) {
  HINTERNET hFile =
      ::GopherOpenFile(m_hConnection, refLocator.m_strLocator, pstrView, dwFlags, dwContext);
  if (hFile == NULL) AfxThrowInternetException(dwContext);
  CGopherFile* pFile = new CGopherFile(hFile, refLocator, this);
  pFile->m_dwContext = dwContext;
  return pFile;
}

BOOL CGopherConnection::GetAttribute(CGopherLocator& refLocator, CString strRequestedAttributes,
                                     CString& strResult) {
  const DWORD cch = 4096;
  DWORD dwRead = 0;
  BOOL bOK = ::GopherGetAttribute(m_hConnection, refLocator.m_strLocator,
                                  strRequestedAttributes.IsEmpty() ? NULL
                                                                   : (LPCTSTR)strRequestedAttributes,
                                  (LPBYTE)strResult.GetBuffer(cch), cch * sizeof(TCHAR), &dwRead,
                                  NULL, m_dwContext);
  strResult.ReleaseBuffer(bOK ? (int)(dwRead / sizeof(TCHAR)) : 0);
  return bOK;
}

// ---------------------------------------------------------------------
// CInternetFile

CInternetFile::CInternetFile(HINTERNET hFile, LPCTSTR pstrFileName,
                             CInternetConnection* pConnection, BOOL bReadMode)
    : m_pbReadBuffer(NULL),
      m_nReadBufferSize(0),
      m_nReadBufferPos(0),
      m_nReadBufferBytes(0),
      m_pbWriteBuffer(NULL),
      m_nWriteBufferSize(0),
      m_nWriteBufferPos(0),
      m_bReadMode(bReadMode),
      m_dwContext(pConnection->GetContext()),
      m_hConnection((HINTERNET)*pConnection),
      m_strServerName(pConnection->GetServerName()),
      m_nPosition(0) {
  m_hFile = (HANDLE)hFile;
  m_strFileName = pstrFileName;
  m_bCloseOnDelete = TRUE;
}

CInternetFile::CInternetFile(HINTERNET hFile, HINTERNET hSession, LPCTSTR pstrFileName,
                             LPCTSTR pstrServer, DWORD_PTR dwContext, BOOL bReadMode)
    : m_pbReadBuffer(NULL),
      m_nReadBufferSize(0),
      m_nReadBufferPos(0),
      m_nReadBufferBytes(0),
      m_pbWriteBuffer(NULL),
      m_nWriteBufferSize(0),
      m_nWriteBufferPos(0),
      m_bReadMode(bReadMode),
      m_dwContext(dwContext),
      m_hConnection(hSession),
      m_strServerName(pstrServer),
      m_nPosition(0) {
  m_hFile = (HANDLE)hFile;
  m_strFileName = pstrFileName;
  m_bCloseOnDelete = TRUE;
}

CInternetFile::~CInternetFile() {
  // Not through CFile's destructor, which would CloseHandle the handle.
  if (m_hFile != INVALID_HANDLE_VALUE) {
    try {
      CInternetFile::Close();
    } catch (CException* e) {
      e->Delete();
      CInternetFile::Abort();
    }
  }
  delete[] m_pbReadBuffer;
  delete[] m_pbWriteBuffer;
}

BOOL CInternetFile::SetReadBufferSize(UINT nReadSize) {
  // Data still in the buffer would be lost.
  if (m_nReadBufferPos < m_nReadBufferBytes) return FALSE;
  delete[] m_pbReadBuffer;
  m_pbReadBuffer = nReadSize > 0 ? new BYTE[nReadSize] : NULL;
  m_nReadBufferSize = nReadSize;
  m_nReadBufferPos = m_nReadBufferBytes = 0;
  return TRUE;
}

BOOL CInternetFile::SetWriteBufferSize(UINT nWriteSize) {
  if (m_hFile != INVALID_HANDLE_VALUE) FlushWrites();
  delete[] m_pbWriteBuffer;
  m_pbWriteBuffer = nWriteSize > 0 ? new BYTE[nWriteSize] : NULL;
  m_nWriteBufferSize = nWriteSize;
  m_nWriteBufferPos = 0;
  return TRUE;
}

BOOL CInternetFile::QueryOption(DWORD dwOption, LPVOID lpBuffer, LPDWORD lpdwBufLen) const {
  return ::InternetQueryOption((HINTERNET)m_hFile, dwOption, lpBuffer, lpdwBufLen);
}

BOOL CInternetFile::QueryOption(DWORD dwOption, DWORD& dwValue) const {
  return QueryDwordOption((HINTERNET)m_hFile, dwOption, dwValue);
}

BOOL CInternetFile::QueryOption(DWORD dwOption, CString& refString) const {
  return QueryStringOption((HINTERNET)m_hFile, dwOption, refString);
}

BOOL CInternetFile::SetOption(DWORD dwOption, LPVOID lpBuffer, DWORD dwBufferLength, DWORD) {
  return ::InternetSetOption((HINTERNET)m_hFile, dwOption, lpBuffer, dwBufferLength);
}

BOOL CInternetFile::SetOption(DWORD dwOption, DWORD dwValue, DWORD) {
  return ::InternetSetOption((HINTERNET)m_hFile, dwOption, &dwValue, sizeof(dwValue));
}

UINT CInternetFile::ReadRaw(void* lpBuf, UINT nCount) {
  // InternetReadFile can return less than asked before the end.
  UINT nTotal = 0;
  while (nTotal < nCount) {
    DWORD dwRead = 0;
    if (!::InternetReadFile((HINTERNET)m_hFile, (BYTE*)lpBuf + nTotal, nCount - nTotal, &dwRead))
      AfxThrowInternetException(m_dwContext);
    if (dwRead == 0) break;
    nTotal += dwRead;
  }
  return nTotal;
}

UINT CInternetFile::Read(void* lpBuf, UINT nCount) {
  if (nCount == 0) return 0;
  UINT nRead = 0;
  if (m_pbReadBuffer == NULL) {
    nRead = ReadRaw(lpBuf, nCount);
  } else {
    BYTE* p = (BYTE*)lpBuf;
    while (nRead < nCount) {
      if (m_nReadBufferPos == m_nReadBufferBytes) {
        // Large reads skip the buffer.
        if (nCount - nRead >= m_nReadBufferSize) {
          nRead += ReadRaw(p + nRead, nCount - nRead);
          break;
        }
        m_nReadBufferPos = 0;
        m_nReadBufferBytes = ReadRaw(m_pbReadBuffer, m_nReadBufferSize);
        if (m_nReadBufferBytes == 0) break;
      }
      UINT n = m_nReadBufferBytes - m_nReadBufferPos;
      if (n > nCount - nRead) n = nCount - nRead;
      memcpy(p + nRead, m_pbReadBuffer + m_nReadBufferPos, n);
      m_nReadBufferPos += n;
      nRead += n;
    }
  }
  m_nPosition += nRead;
  return nRead;
}

void CInternetFile::FlushWrites() {
  if (m_nWriteBufferPos == 0) return;
  DWORD dwWritten = 0;
  UINT n = m_nWriteBufferPos;
  m_nWriteBufferPos = 0;
  if (!::InternetWriteFile((HINTERNET)m_hFile, m_pbWriteBuffer, n, &dwWritten) || dwWritten != n)
    AfxThrowInternetException(m_dwContext);
}

void CInternetFile::Write(const void* lpBuf, UINT nCount) {
  if (nCount == 0) return;
  if (m_pbWriteBuffer != NULL && nCount < m_nWriteBufferSize) {
    if (m_nWriteBufferPos + nCount > m_nWriteBufferSize) FlushWrites();
    memcpy(m_pbWriteBuffer + m_nWriteBufferPos, lpBuf, nCount);
    m_nWriteBufferPos += nCount;
  } else {
    FlushWrites();
    DWORD dwWritten = 0;
    if (!::InternetWriteFile((HINTERNET)m_hFile, lpBuf, nCount, &dwWritten) ||
        dwWritten != nCount)
      AfxThrowInternetException(m_dwContext);
  }
  m_nPosition += nCount;
}

ULONGLONG CInternetFile::Seek(LONGLONG lOffset, UINT nFrom) {
  FlushWrites();
  // What is in the read buffer is ahead of the file's own position.
  LONGLONG lUnread = (LONGLONG)(m_nReadBufferBytes - m_nReadBufferPos);
  m_nReadBufferPos = m_nReadBufferBytes = 0;
  DWORD dwMethod;
  switch (nFrom) {
  case CFile::begin: dwMethod = FILE_BEGIN; break;
  case CFile::current: dwMethod = FILE_CURRENT; lOffset -= lUnread; break;
  default: dwMethod = FILE_END; break;
  }
  DWORD dwPos = ::InternetSetFilePointer((HINTERNET)m_hFile, (LONG)lOffset, NULL, dwMethod, 0);
  if (dwPos == (DWORD)-1) AfxThrowInternetException(m_dwContext);
  m_nPosition = dwPos;
  return m_nPosition;
}

ULONGLONG CInternetFile::GetPosition() const { return m_nPosition; }

ULONGLONG CInternetFile::GetLength() const {
  // The size the server gave (HTTP's Content-Length, FTP's SIZE), or what
  // has arrived.
  DWORD dwLen = 0, dwSize = sizeof(dwLen);
  if (::HttpQueryInfo((HINTERNET)m_hFile, HTTP_QUERY_CONTENT_LENGTH | HTTP_QUERY_FLAG_NUMBER,
                      &dwLen, &dwSize, NULL))
    return dwLen;
  DWORD dwHigh = 0;
  DWORD dwLow = ::FtpGetFileSize((HINTERNET)m_hFile, &dwHigh);
  if (dwLow != (DWORD)-1 || ::GetLastError() == NO_ERROR)
    return ((ULONGLONG)dwHigh << 32) | dwLow;
  DWORD dwAvail = 0;
  ::InternetQueryDataAvailable((HINTERNET)m_hFile, &dwAvail, 0, 0);
  return m_nPosition + (m_nReadBufferBytes - m_nReadBufferPos) + dwAvail;
}

DWORD CInternetFile::QueryDataAvailable() {
  DWORD dwAvail = 0;
  if (!::InternetQueryDataAvailable((HINTERNET)m_hFile, &dwAvail, 0, 0))
    AfxThrowInternetException(m_dwContext);
  return dwAvail + (m_nReadBufferBytes - m_nReadBufferPos);
}

BOOL CInternetFile::ReadString(CString& rString) {
  // Lines end with \n or \r\n, which are not kept.
  if (m_pbReadBuffer == NULL) SetReadBufferSize(4096);
  rString.Empty();
  CStringA strLine;
  BOOL bAny = FALSE;
  char c;
  while (Read(&c, 1) == 1) {
    bAny = TRUE;
    if (c == '\n') break;
    strLine += c;
  }
  if (!bAny) return FALSE;
  if (!strLine.IsEmpty() && strLine[strLine.GetLength() - 1] == '\r')
    strLine = strLine.Left(strLine.GetLength() - 1);
  rString = CString(strLine);
  return TRUE;
}

LPTSTR CInternetFile::ReadString(LPTSTR pstr, UINT nMax) {
  // As fgets: the \n is kept, and the text ends at nMax - 1 characters.
  if (pstr == NULL || nMax == 0) return NULL;
  if (m_pbReadBuffer == NULL) SetReadBufferSize(4096);
  UINT n = 0;
  char c;
  while (n + 1 < nMax && Read(&c, 1) == 1) {
    pstr[n++] = (TCHAR)(BYTE)c;
    if (c == '\n') break;
  }
  pstr[n] = 0;
  return n > 0 ? pstr : NULL;
}

void CInternetFile::WriteString(LPCTSTR pstr) {
  if (pstr == NULL) return;
  CStringA str(pstr);
  Write((LPCSTR)str, str.GetLength());
}

void CInternetFile::Flush() { FlushWrites(); }

void CInternetFile::Close() {
  if (m_hFile == INVALID_HANDLE_VALUE) return;
  HINTERNET hFile = (HINTERNET)m_hFile;
  try {
    FlushWrites();
  } catch (CException*) {
    ::InternetCloseHandle(hFile);
    m_hFile = INVALID_HANDLE_VALUE;
    throw;
  }
  ::InternetCloseHandle(hFile);
  m_hFile = INVALID_HANDLE_VALUE;
  m_nReadBufferPos = m_nReadBufferBytes = 0;
}

void CInternetFile::Abort() {
  if (m_hFile != INVALID_HANDLE_VALUE) ::InternetCloseHandle((HINTERNET)m_hFile);
  m_hFile = INVALID_HANDLE_VALUE;
  m_nWriteBufferPos = 0;
  m_nReadBufferPos = m_nReadBufferBytes = 0;
}

CString CInternetFile::GetFileURL() const {
  CString str;
  if (QueryStringOption((HINTERNET)m_hFile, INTERNET_OPTION_URL, str) && !str.IsEmpty()) return str;
  str = _T("ftp://") + m_strServerName;
  if (m_strFileName.IsEmpty() || m_strFileName[0] != '/') str += '/';
  return str + m_strFileName;
}

void CInternetFile::SetLength(ULONGLONG) { AfxThrowNotSupportedException(); }
void CInternetFile::LockRange(ULONGLONG, ULONGLONG) { AfxThrowNotSupportedException(); }
void CInternetFile::UnlockRange(ULONGLONG, ULONGLONG) { AfxThrowNotSupportedException(); }

CFile* CInternetFile::Duplicate() const {
  AfxThrowNotSupportedException();
  return NULL;
}

// ---------------------------------------------------------------------
// CHttpFile

CHttpFile::CHttpFile(HINTERNET hFile, HINTERNET hSession, LPCTSTR pstrObject, LPCTSTR pstrServer,
                     LPCTSTR pstrVerb, DWORD_PTR dwContext)
    : CInternetFile(hFile, hSession, pstrObject, pstrServer, dwContext, TRUE),
      m_strObject(pstrObject),
      m_strVerb(pstrVerb) {}

CHttpFile::CHttpFile(HINTERNET hFile, LPCTSTR pstrVerb, LPCTSTR pstrObject,
                     CHttpConnection* pConnection)
    : CInternetFile(hFile, pstrObject, pConnection, TRUE),
      m_strObject(pstrObject),
      m_strVerb(pstrVerb) {}

CHttpFile::~CHttpFile() {}

BOOL CHttpFile::AddRequestHeaders(LPCTSTR pstrHeaders, DWORD dwFlags, int dwHeadersLen) {
  return ::HttpAddRequestHeaders((HINTERNET)m_hFile, pstrHeaders, (DWORD)dwHeadersLen, dwFlags);
}

BOOL CHttpFile::AddRequestHeaders(CString& str, DWORD dwFlags) {
  return AddRequestHeaders(str, dwFlags, str.GetLength());
}

BOOL CHttpFile::SendRequest(LPCTSTR pstrHeaders, DWORD dwHeadersLen, LPVOID lpOptional,
                            DWORD dwOptionalLen) {
  if (pstrHeaders != NULL && dwHeadersLen == 0) dwHeadersLen = (DWORD)-1;
  if (!::HttpSendRequest((HINTERNET)m_hFile, pstrHeaders, dwHeadersLen, lpOptional,
                         dwOptionalLen))
    AfxThrowInternetException(m_dwContext);
  m_nPosition = 0;
  return TRUE;
}

BOOL CHttpFile::SendRequest(CString& strHeaders, LPVOID lpOptional, DWORD dwOptionalLen) {
  return SendRequest(strHeaders, strHeaders.GetLength(), lpOptional, dwOptionalLen);
}

BOOL CHttpFile::SendRequestEx(DWORD dwTotalLen, DWORD dwFlags, DWORD_PTR dwContext) {
  INTERNET_BUFFERS buffers;
  memset(&buffers, 0, sizeof(buffers));
  buffers.dwStructSize = sizeof(buffers);
  buffers.dwBufferTotal = dwTotalLen;
  return SendRequestEx(&buffers, NULL, dwFlags, dwContext);
}

BOOL CHttpFile::SendRequestEx(LPINTERNET_BUFFERS lpBuffIn, LPINTERNET_BUFFERS lpBuffOut,
                              DWORD dwFlags, DWORD_PTR dwContext) {
  if (!::HttpSendRequestEx((HINTERNET)m_hFile, lpBuffIn, lpBuffOut, dwFlags, dwContext))
    AfxThrowInternetException(m_dwContext);
  m_bReadMode = FALSE;
  m_nPosition = 0;
  return TRUE;
}

BOOL CHttpFile::EndRequest(DWORD dwFlags, LPINTERNET_BUFFERS lpBuffIn, DWORD_PTR dwContext) {
  FlushWrites();
  if (!::HttpEndRequest((HINTERNET)m_hFile, lpBuffIn, dwFlags, dwContext))
    AfxThrowInternetException(m_dwContext);
  // The response is read from the start.
  m_bReadMode = TRUE;
  m_nPosition = 0;
  return TRUE;
}

BOOL CHttpFile::QueryInfo(DWORD dwInfoLevel, LPVOID lpvBuffer, LPDWORD lpdwBufferLength,
                          LPDWORD lpdwIndex) const {
  return ::HttpQueryInfo((HINTERNET)m_hFile, dwInfoLevel, lpvBuffer, lpdwBufferLength, lpdwIndex);
}

BOOL CHttpFile::QueryInfo(DWORD dwInfoLevel, CString& str, LPDWORD dwIndex) const {
  str.Empty();
  // The length comes back in bytes.
  DWORD dwIndexSave = dwIndex != NULL ? *dwIndex : 0;
  DWORD dwLen = 0;
  if (!::HttpQueryInfo((HINTERNET)m_hFile, dwInfoLevel, NULL, &dwLen, dwIndex) &&
      ::GetLastError() != ERROR_INSUFFICIENT_BUFFER)
    return FALSE;
  if (dwIndex != NULL) *dwIndex = dwIndexSave;
  DWORD cch = dwLen / sizeof(TCHAR) + 1;
  dwLen = cch * sizeof(TCHAR);
  BOOL bOK = ::HttpQueryInfo((HINTERNET)m_hFile, dwInfoLevel, str.GetBuffer(cch), &dwLen, dwIndex);
  str.ReleaseBuffer(bOK ? (int)(dwLen / sizeof(TCHAR)) : 0);
  return bOK;
}

BOOL CHttpFile::QueryInfo(DWORD dwInfoLevel, SYSTEMTIME* pSysTime, LPDWORD dwIndex) const {
  DWORD dwLen = sizeof(SYSTEMTIME);
  return ::HttpQueryInfo((HINTERNET)m_hFile, dwInfoLevel | HTTP_QUERY_FLAG_SYSTEMTIME, pSysTime,
                         &dwLen, dwIndex);
}

BOOL CHttpFile::QueryInfo(DWORD dwInfoLevel, DWORD& dwResult, LPDWORD dwIndex) const {
  DWORD dwLen = sizeof(DWORD);
  return ::HttpQueryInfo((HINTERNET)m_hFile, dwInfoLevel | HTTP_QUERY_FLAG_NUMBER, &dwResult,
                         &dwLen, dwIndex);
}

BOOL CHttpFile::QueryInfoStatusCode(DWORD& dwStatusCode) const {
  return QueryInfo(HTTP_QUERY_STATUS_CODE, dwStatusCode);
}

DWORD CHttpFile::ErrorDlg(CWnd* pParent, DWORD dwError, DWORD dwFlags, LPVOID* lppvData) {
  HWND hWnd = pParent != NULL ? pParent->GetSafeHwnd() : NULL;
  if (hWnd == NULL) {
    CWnd* pMain = AfxGetMainWnd();
    hWnd = pMain != NULL ? pMain->GetSafeHwnd() : ::GetDesktopWindow();
  }
  LPVOID pvData = NULL;
  return ::InternetErrorDlg(hWnd, (HINTERNET)m_hFile, dwError, dwFlags,
                            lppvData != NULL ? lppvData : &pvData);
}

CString CHttpFile::GetFileURL() const {
  CString str;
  if (QueryStringOption((HINTERNET)m_hFile, INTERNET_OPTION_URL, str) && !str.IsEmpty()) return str;
  str = _T("http://") + m_strServerName;
  if (m_strObject.IsEmpty() || m_strObject[0] != '/') str += '/';
  return str + m_strObject;
}

void CHttpFile::Close() { CInternetFile::Close(); }

// ---------------------------------------------------------------------
// CGopherFile

CGopherFile::CGopherFile(HINTERNET hFile, CGopherLocator& refLocator,
                         CGopherConnection* pConnection)
    : CInternetFile(hFile, (LPCTSTR)refLocator, pConnection, TRUE) {}

CGopherFile::CGopherFile(HINTERNET hFile, HINTERNET hSession, LPCTSTR pstrLocator, DWORD,
                         DWORD_PTR dwContext)
    : CInternetFile(hFile, hSession, pstrLocator, NULL, dwContext, TRUE) {}

CGopherFile::~CGopherFile() {}

void CGopherFile::Close() { CInternetFile::Close(); }

// Gopher files are read only.
void CGopherFile::Write(const void*, UINT) { AfxThrowNotSupportedException(); }
void CGopherFile::WriteString(LPCTSTR) { AfxThrowNotSupportedException(); }

// ---------------------------------------------------------------------
// CFtpFileFind

CFtpFileFind::CFtpFileFind(CFtpConnection* pConnection, DWORD_PTR dwContext)
    : m_pConnection(pConnection), m_dwContext(dwContext) {
  m_chDirSeparator = '/';
}

CFtpFileFind::~CFtpFileFind() {
  // CFileFind's destructor would end the handle with FindClose.
  Close();
}

void CFtpFileFind::CloseContext() { ::InternetCloseHandle((HINTERNET)m_hContext); }

BOOL CFtpFileFind::FindFile(LPCTSTR pstrName, DWORD dwFlags) {
  Close();
  CString strName = pstrName != NULL ? pstrName : _T("*");
  // The folder the names are in: the pattern's, or the current one, asked
  // for first since the connection takes no commands during a listing.
  CString strRoot;
  int i = strName.ReverseFind('/');
  if (i >= 0) {
    strRoot = strName.Left(i);
    if (strRoot.IsEmpty()) strRoot = _T("/");
  } else if (!m_pConnection->GetCurrentDirectory(strRoot)) {
    strRoot = _T("/");
  }
  m_pNextInfo = new WIN32_FIND_DATA;
  memset(m_pNextInfo, 0, sizeof(WIN32_FIND_DATA));
  HINTERNET hFind = ::FtpFindFirstFile((HINTERNET)*m_pConnection, strName, m_pNextInfo, dwFlags,
                                       m_dwContext);
  if (hFind == NULL) {
    DWORD dwError = ::GetLastError();
    Close();
    ::SetLastError(dwError);
    return FALSE;
  }
  m_hContext = (HANDLE)hFind;
  m_strRoot = strRoot;
  return TRUE;
}

BOOL CFtpFileFind::FindNextFile() {
  if (m_hContext == NULL) return FALSE;
  if (m_pFoundInfo == NULL) m_pFoundInfo = new WIN32_FIND_DATA;
  WIN32_FIND_DATA* pTemp = m_pFoundInfo;
  m_pFoundInfo = m_pNextInfo;
  m_pNextInfo = pTemp;
  return ::InternetFindNextFile((HINTERNET)m_hContext, m_pNextInfo);
}

CString CFtpFileFind::GetFileURL() const {
  if (m_pFoundInfo == NULL) return CString();
  CString str = _T("ftp://") + m_pConnection->GetServerName();
  INTERNET_PORT nPort = m_pConnection->GetPort();
  if (nPort != INTERNET_INVALID_PORT_NUMBER && nPort != INTERNET_DEFAULT_FTP_PORT) {
    CString strPort;
    strPort.Format(_T(":%u"), (unsigned)nPort);
    str += strPort;
  }
  CString strPath = GetFilePath();
  if (strPath.IsEmpty() || strPath[0] != '/') str += '/';
  return str + strPath;
}

// ---------------------------------------------------------------------
// CGopherFileFind

CGopherFileFind::CGopherFileFind(CGopherConnection* pConnection, DWORD_PTR dwContext)
    : m_pConnection(pConnection), m_dwContext(dwContext), m_pGopherFound(NULL),
      m_pGopherNext(NULL) {
  m_chDirSeparator = '/';
}

CGopherFileFind::~CGopherFileFind() {
  Close();
  delete m_pGopherFound;
  delete m_pGopherNext;
}

void CGopherFileFind::CloseContext() {
  ::InternetCloseHandle((HINTERNET)m_hContext);
  delete m_pGopherFound;
  m_pGopherFound = NULL;
  delete m_pGopherNext;
  m_pGopherNext = NULL;
}

BOOL CGopherFileFind::FindFile(CGopherLocator& refLocator, LPCTSTR pstrString, DWORD dwFlags) {
  Close();
  m_pGopherNext = new GOPHER_FIND_DATA;
  memset(m_pGopherNext, 0, sizeof(GOPHER_FIND_DATA));
  HINTERNET hFind = ::GopherFindFirstFile((HINTERNET)*m_pConnection, (LPCTSTR)refLocator,
                                          pstrString, m_pGopherNext, dwFlags, m_dwContext);
  if (hFind == NULL) {
    DWORD dwError = ::GetLastError();
    delete m_pGopherNext;
    m_pGopherNext = NULL;
    Close();
    ::SetLastError(dwError);
    return FALSE;
  }
  m_hContext = (HANDLE)hFind;
  return TRUE;
}

BOOL CGopherFileFind::FindFile(LPCTSTR pstrString, DWORD dwFlags) {
  Close();
  m_pGopherNext = new GOPHER_FIND_DATA;
  memset(m_pGopherNext, 0, sizeof(GOPHER_FIND_DATA));
  HINTERNET hFind = ::GopherFindFirstFile((HINTERNET)*m_pConnection, NULL, pstrString,
                                          m_pGopherNext, dwFlags, m_dwContext);
  if (hFind == NULL) {
    DWORD dwError = ::GetLastError();
    delete m_pGopherNext;
    m_pGopherNext = NULL;
    Close();
    ::SetLastError(dwError);
    return FALSE;
  }
  m_hContext = (HANDLE)hFind;
  return TRUE;
}

BOOL CGopherFileFind::FindNextFile() {
  if (m_hContext == NULL) return FALSE;
  if (m_pGopherFound == NULL) m_pGopherFound = new GOPHER_FIND_DATA;
  GOPHER_FIND_DATA* pTemp = m_pGopherFound;
  m_pGopherFound = m_pGopherNext;
  m_pGopherNext = pTemp;
  return ::InternetFindNextFile((HINTERNET)m_hContext, m_pGopherNext);
}

CGopherLocator CGopherFileFind::GetLocator() const {
  if (m_pGopherFound == NULL) return CGopherLocator(_T(""), 1);
  return CGopherLocator(m_pGopherFound->Locator, lstrlen(m_pGopherFound->Locator) + 1);
}

CString CGopherFileFind::GetScreenName() const {
  return m_pGopherFound != NULL ? CString(m_pGopherFound->DisplayString) : CString();
}

BOOL CGopherFileFind::GetLastWriteTime(FILETIME* pTimeStamp) const {
  if (m_pGopherFound == NULL || pTimeStamp == NULL) return FALSE;
  *pTimeStamp = m_pGopherFound->LastModificationTime;
  return TRUE;
}

// Gopher gives only the time of the last change.
BOOL CGopherFileFind::GetLastAccessTime(FILETIME* pTimeStamp) const {
  return GetLastWriteTime(pTimeStamp);
}
BOOL CGopherFileFind::GetCreationTime(FILETIME* pTimeStamp) const {
  return GetLastWriteTime(pTimeStamp);
}

BOOL CGopherFileFind::GetLastWriteTime(CTime& refTime) const {
  FILETIME ft;
  if (!GetLastWriteTime(&ft)) return FALSE;
  refTime = CTime(ft);
  return TRUE;
}

BOOL CGopherFileFind::GetLastAccessTime(CTime& refTime) const { return GetLastWriteTime(refTime); }
BOOL CGopherFileFind::GetCreationTime(CTime& refTime) const { return GetLastWriteTime(refTime); }

ULONGLONG CGopherFileFind::GetLength() const {
  if (m_pGopherFound == NULL) return 0;
  return ((ULONGLONG)m_pGopherFound->SizeHigh << 32) | m_pGopherFound->SizeLow;
}

CString CGopherFileFind::GetFileName() const { return GetScreenName(); }
CString CGopherFileFind::GetFilePath() const { return GetScreenName(); }
CString CGopherFileFind::GetFileTitle() const { return GetScreenName(); }

CString CGopherFileFind::GetFileURL() const {
  if (m_pGopherFound == NULL) return CString();
  return _T("gopher://") + m_pConnection->GetServerName() + _T("/") +
         CString(m_pGopherFound->Locator);
}

BOOL CGopherFileFind::MatchesMask(DWORD dwMask) const {
  if (m_pGopherFound == NULL) return FALSE;
  // A gopher directory is a directory; nothing else has attributes.
  if ((dwMask & FILE_ATTRIBUTE_DIRECTORY) && (m_pGopherFound->GopherType & GOPHER_TYPE_DIRECTORY))
    return TRUE;
  return FALSE;
}

BOOL CGopherFileFind::IsDots() const { return FALSE; }
