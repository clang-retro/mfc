// atlutil.h - ATL's utilities: CUrl and the URL helpers (escape,
// unescape, canonicalize, combine), AtlGetErrorDescription, the thread
// traits, CWorkerThread (one thread waiting on handles and timers for
// IWorkerThreadClient objects) and CThreadPool (worker threads fed
// requests through an I/O completion port).
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#ifndef __ATLUTIL_H__
#define __ATLUTIL_H__

#include <atlbase.h>
#ifndef __AFX_H__
#include <atlstr.h>
#endif
#include <process.h>
#include <stdio.h>
#include <string.h>
#include <vector>

namespace ATL {

#ifdef __AFX_H__
typedef ::CString _AtlUtilString;
#else
typedef ::ATL::CString _AtlUtilString;
#endif

// ---------------------------------------------------------------------
// Errors and paths

inline _AtlUtilString AtlGetErrorDescription(HRESULT hr, DWORD dwLangId = 0) {
  _AtlUtilString str;
  LPTSTR pszMsg = NULL;
  DWORD dwCode = HRESULT_FACILITY(hr) == FACILITY_WIN32 ? HRESULT_CODE(hr) : (DWORD)hr;
  if (::FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                          FORMAT_MESSAGE_IGNORE_INSERTS,
                      NULL, dwCode, dwLangId, (LPTSTR)&pszMsg, 0, NULL) != 0 &&
      pszMsg != NULL) {
    str = pszMsg;
    ::LocalFree(pszMsg);
    str.TrimRight();
  }
  return str;
}

// A path from a drive's root ("C:\x") or a UNC path ("\\server\share").
inline bool AtlIsFullPath(LPCTSTR szPath) {
  if (szPath == NULL || szPath[0] == 0) return false;
  if (szPath[0] == '\\' && szPath[1] == '\\') return true;
  return szPath[1] == ':' && (szPath[2] == '\\' || szPath[2] == '/');
}

// ---------------------------------------------------------------------
// The thread traits: how CWorkerThread and CThreadPool start threads.
// CRTThreadTraits starts them through the C runtime, so it is set up for
// them; Win32ThreadTraits calls CreateThread directly.

class CRTThreadTraits {
public:
  static HANDLE CreateThread(LPSECURITY_ATTRIBUTES lpsa, DWORD dwStackSize,
                             LPTHREAD_START_ROUTINE pfnThreadProc, void* pvParam,
                             DWORD dwCreationFlags, DWORD* pdwThreadId) {
    unsigned int nId = 0;
    HANDLE h = (HANDLE)_beginthreadex(lpsa, dwStackSize,
                                      (unsigned(__stdcall*)(void*))(void*)pfnThreadProc, pvParam,
                                      dwCreationFlags, &nId);
    if (pdwThreadId != NULL) *pdwThreadId = nId;
    return h;
  }
};

class Win32ThreadTraits {
public:
  static HANDLE CreateThread(LPSECURITY_ATTRIBUTES lpsa, DWORD dwStackSize,
                             LPTHREAD_START_ROUTINE pfnThreadProc, void* pvParam,
                             DWORD dwCreationFlags, DWORD* pdwThreadId) {
    return ::CreateThread(lpsa, dwStackSize, pfnThreadProc, pvParam, dwCreationFlags,
                          pdwThreadId);
  }
};

typedef CRTThreadTraits DefaultThreadTraits;

// ---------------------------------------------------------------------
// URLs

enum ATL_URL_SCHEME {
  ATL_URL_SCHEME_UNKNOWN = -1,
  ATL_URL_SCHEME_FTP = 0,
  ATL_URL_SCHEME_GOPHER = 1,
  ATL_URL_SCHEME_HTTP = 2,
  ATL_URL_SCHEME_HTTPS = 3,
  ATL_URL_SCHEME_FILE = 4,
  ATL_URL_SCHEME_NEWS = 5,
  ATL_URL_SCHEME_MAILTO = 6,
  ATL_URL_SCHEME_SOCKS = 7
};

typedef WORD ATL_URL_PORT;

#define ATL_URL_MAX_HOST_NAME_LENGTH 256
#define ATL_URL_MAX_USER_NAME_LENGTH 128
#define ATL_URL_MAX_PASSWORD_LENGTH 128
#define ATL_URL_MAX_PORT_NUMBER_LENGTH 5
#define ATL_URL_MAX_PORT_NUMBER_VALUE 65535
#define ATL_URL_MAX_SCHEME_LENGTH 32
#define ATL_URL_MAX_PATH_LENGTH 2048
#define ATL_URL_MAX_URL_LENGTH 2084

#define ATL_URL_INVALID_PORT_NUMBER 0
#define ATL_URL_DEFAULT_FTP_PORT 21
#define ATL_URL_DEFAULT_GOPHER_PORT 70
#define ATL_URL_DEFAULT_HTTP_PORT 80
#define ATL_URL_DEFAULT_HTTPS_PORT 443
#define ATL_URL_DEFAULT_SOCKS_PORT 1080

#define ATL_URL_ESCAPE 1                        // escape (CreateUrl) or unescape (CrackUrl)
#define ATL_URL_NO_ENCODE 0x20000000            // leave unsafe characters alone
#define ATL_URL_DECODE 0x10000000               // decode %XX
#define ATL_URL_NO_META 0x08000000              // keep . and .. in the path
#define ATL_URL_ENCODE_SPACES_ONLY 0x04000000   // only spaces become %20
#define ATL_URL_BROWSER_MODE 0x02000000         // nothing after # or ? is encoded
#define ATL_URL_ENCODE_PERCENT 0x00001000       // % is encoded too
#define ATL_URL_CANONICALIZE 0x00000002
#define ATL_URL_COMBINE 0x00000004

inline ATL_URL_PORT AtlGetDefaultUrlPort(ATL_URL_SCHEME m_nScheme) {
  switch (m_nScheme) {
    case ATL_URL_SCHEME_FTP: return ATL_URL_DEFAULT_FTP_PORT;
    case ATL_URL_SCHEME_GOPHER: return ATL_URL_DEFAULT_GOPHER_PORT;
    case ATL_URL_SCHEME_HTTP: return ATL_URL_DEFAULT_HTTP_PORT;
    case ATL_URL_SCHEME_HTTPS: return ATL_URL_DEFAULT_HTTPS_PORT;
    case ATL_URL_SCHEME_SOCKS: return ATL_URL_DEFAULT_SOCKS_PORT;
    default: return ATL_URL_INVALID_PORT_NUMBER;
  }
}

// The characters RFC 1738 says to escape.
inline BOOL AtlIsUnsafeUrlChar(char chIn) {
  unsigned char ch = (unsigned char)chIn;
  if (ch <= 32 || ch >= 127) return TRUE;
  switch (ch) {
    case '"': case '<': case '>': case '%': case '\\': case '^': case '[': case ']':
    case '`': case '{': case '|': case '}': case '#':
      return TRUE;
  }
  return FALSE;
}

template <typename C>
inline int _AtlUrlHexValue(C ch) {
  if (ch >= '0' && ch <= '9') return ch - '0';
  if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
  if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
  return -1;
}

// Escapes into szStringOut (dwMaxLength characters, the NUL included);
// *pdwStrLen gets the length written, or needed when it does not fit.
template <typename C>
inline BOOL _AtlEscapeUrl(const C* szStringIn, C* szStringOut, DWORD* pdwStrLen,
                          DWORD dwMaxLength, DWORD dwFlags) {
  static const char s_hex[] = "0123456789ABCDEF";
  DWORD nOut = 0;
  bool bRest = false;  // past '?' or '#' in browser mode
  for (const C* p = szStringIn; *p != 0; p++) {
    C ch = *p;
    if ((dwFlags & ATL_URL_BROWSER_MODE) && (ch == '?' || ch == '#')) bRest = true;
    bool bEscape;
    if (dwFlags & ATL_URL_NO_ENCODE)
      bEscape = false;
    else if (dwFlags & ATL_URL_ENCODE_SPACES_ONLY)
      bEscape = ch == ' ';
    else if (bRest)
      bEscape = false;
    else if (ch == '%')
      bEscape = (dwFlags & ATL_URL_ENCODE_PERCENT) != 0;
    else if (ch == '#')
      bEscape = false;
    else
      bEscape = (unsigned)ch > 0xff || AtlIsUnsafeUrlChar((char)ch);
    if (bEscape) {
      unsigned v = (unsigned)ch & 0xff;
      if (nOut + 3 < dwMaxLength) {
        szStringOut[nOut] = '%';
        szStringOut[nOut + 1] = (C)s_hex[v >> 4];
        szStringOut[nOut + 2] = (C)s_hex[v & 0xf];
      }
      nOut += 3;
    } else {
      if (nOut + 1 < dwMaxLength) szStringOut[nOut] = ch;
      nOut++;
    }
  }
  if (pdwStrLen != NULL) *pdwStrLen = nOut;
  if (nOut + 1 > dwMaxLength) {
    if (dwMaxLength > 0) szStringOut[0] = 0;
    if (pdwStrLen != NULL) *pdwStrLen = nOut + 1;
    return FALSE;
  }
  szStringOut[nOut] = 0;
  return TRUE;
}

template <typename C>
inline BOOL _AtlUnescapeUrl(const C* szStringIn, C* szStringOut, LPDWORD pdwStrLen,
                            DWORD dwMaxLength) {
  DWORD nOut = 0;
  for (const C* p = szStringIn; *p != 0; p++) {
    C ch = *p;
    if (ch == '%' && _AtlUrlHexValue(p[1]) >= 0 && _AtlUrlHexValue(p[2]) >= 0) {
      ch = (C)((_AtlUrlHexValue(p[1]) << 4) | _AtlUrlHexValue(p[2]));
      p += 2;
    }
    if (nOut + 1 < dwMaxLength) szStringOut[nOut] = ch;
    nOut++;
  }
  if (pdwStrLen != NULL) *pdwStrLen = nOut;
  if (nOut + 1 > dwMaxLength) {
    if (pdwStrLen != NULL) *pdwStrLen = nOut + 1;
    return FALSE;
  }
  szStringOut[nOut] = 0;
  return TRUE;
}

inline BOOL AtlEscapeUrl(LPCSTR szStringIn, LPSTR szStringOut, DWORD* pdwStrLen,
                         DWORD dwMaxLength, DWORD dwFlags = 0) {
  return _AtlEscapeUrl(szStringIn, szStringOut, pdwStrLen, dwMaxLength, dwFlags);
}
inline BOOL AtlEscapeUrl(LPCWSTR szStringIn, LPWSTR szStringOut, DWORD* pdwStrLen,
                         DWORD dwMaxLength, DWORD dwFlags = 0) {
  return _AtlEscapeUrl(szStringIn, szStringOut, pdwStrLen, dwMaxLength, dwFlags);
}
inline BOOL AtlUnescapeUrl(LPCSTR szStringIn, LPSTR szStringOut, LPDWORD pdwStrLen,
                           DWORD dwMaxLength) {
  return _AtlUnescapeUrl(szStringIn, szStringOut, pdwStrLen, dwMaxLength);
}
inline BOOL AtlUnescapeUrl(LPCWSTR szStringIn, LPWSTR szStringOut, LPDWORD pdwStrLen,
                           DWORD dwMaxLength) {
  return _AtlUnescapeUrl(szStringIn, szStringOut, pdwStrLen, dwMaxLength);
}

// Removes "." and ".." segments from a path, in place.
inline void _AtlRemoveDotSegments(_AtlUtilString& strPath) {
  _AtlUtilString strOut;
  int i = 0, n = strPath.GetLength();
  while (i < n) {
    int j = strPath.Find(_T('/'), i);
    if (j < 0) j = n;
    _AtlUtilString strSeg = strPath.Mid(i, j - i);
    bool bSlash = j < n;
    if (strSeg == _T(".")) {
      if (!bSlash && strOut.Right(1) != _T("/")) strOut += _T('/');
    } else if (strSeg == _T("..")) {
      // Drop the last segment written.
      int k = strOut.GetLength() > 0 ? strOut.Left(strOut.GetLength() - 1).ReverseFind(_T('/'))
                                     : -1;
      strOut = k >= 0 ? strOut.Left(k + 1) : _AtlUtilString(strPath[0] == _T('/') ? _T("/") : _T(""));
    } else {
      strOut += strSeg;
      if (bSlash) strOut += _T('/');
    }
    i = j + 1;
  }
  strPath = strOut;
}

class CUrl {
public:
  CUrl() { Clear(); }
  CUrl(const CUrl& urlThat) { *this = urlThat; }

  CUrl& operator=(const CUrl& urlThat) {
    if (this != &urlThat) {
      m_strScheme = urlThat.m_strScheme;
      m_nScheme = urlThat.m_nScheme;
      m_strHostName = urlThat.m_strHostName;
      m_nPortNumber = urlThat.m_nPortNumber;
      m_strUserName = urlThat.m_strUserName;
      m_strPassword = urlThat.m_strPassword;
      m_strUrlPath = urlThat.m_strUrlPath;
      m_strExtraInfo = urlThat.m_strExtraInfo;
    }
    return *this;
  }

  void Clear() {
    m_strScheme.Empty();
    m_nScheme = ATL_URL_SCHEME_UNKNOWN;
    m_strHostName.Empty();
    m_nPortNumber = ATL_URL_INVALID_PORT_NUMBER;
    m_strUserName.Empty();
    m_strPassword.Empty();
    m_strUrlPath.Empty();
    m_strExtraInfo.Empty();
  }

  // scheme://user:password@host:port/path?extra#extra
  BOOL CrackUrl(LPCTSTR lpszUrl, DWORD dwFlags = 0) {
    Clear();
    if (lpszUrl == NULL) return FALSE;
    _AtlUtilString strUrl = lpszUrl;
    strUrl.Trim();
    int nColon = strUrl.Find(_T(':'));
    if (nColon <= 0) return FALSE;
    for (int i = 0; i < nColon; i++) {
      TCHAR ch = strUrl[i];
      if (!_istalnum(ch) && ch != '+' && ch != '-' && ch != '.') return FALSE;
    }
    if (!SetSchemeName(strUrl.Left(nColon))) return FALSE;
    _AtlUtilString strRest = strUrl.Mid(nColon + 1);

    if (strRest.Left(2) == _T("//")) {
      strRest = strRest.Mid(2);
      int nEnd = strRest.FindOneOf(_T("/?#"));
      _AtlUtilString strAuth = nEnd >= 0 ? strRest.Left(nEnd) : strRest;
      strRest = nEnd >= 0 ? strRest.Mid(nEnd) : _AtlUtilString();
      int nAt = strAuth.ReverseFind(_T('@'));
      if (nAt >= 0) {
        _AtlUtilString strUser = strAuth.Left(nAt);
        strAuth = strAuth.Mid(nAt + 1);
        int nPw = strUser.Find(_T(':'));
        if (nPw >= 0) {
          m_strPassword = strUser.Mid(nPw + 1);
          strUser = strUser.Left(nPw);
        }
        m_strUserName = strUser;
      }
      // A port after the last colon, unless that is inside [an IPv6 address].
      int nPortColon = strAuth.ReverseFind(_T(':'));
      if (nPortColon >= 0 && nPortColon > strAuth.ReverseFind(_T(']'))) {
        _AtlUtilString strPort = strAuth.Mid(nPortColon + 1);
        strAuth = strAuth.Left(nPortColon);
        if (!strPort.IsEmpty()) {
          long n = _tcstol(strPort, NULL, 10);
          if (n <= 0 || n > ATL_URL_MAX_PORT_NUMBER_VALUE) return FALSE;
          m_nPortNumber = (ATL_URL_PORT)n;
        }
      }
      m_strHostName = strAuth;
    }
    int nExtra = strRest.FindOneOf(_T("?#"));
    if (nExtra >= 0) {
      m_strExtraInfo = strRest.Mid(nExtra);
      strRest = strRest.Left(nExtra);
    }
    m_strUrlPath = strRest;
    if (m_nPortNumber == ATL_URL_INVALID_PORT_NUMBER)
      m_nPortNumber = AtlGetDefaultUrlPort(m_nScheme);

    if (dwFlags & (ATL_URL_ESCAPE | ATL_URL_DECODE)) {
      Unescape(m_strUserName);
      Unescape(m_strPassword);
      Unescape(m_strUrlPath);
      Unescape(m_strExtraInfo);
    }
    return TRUE;
  }

  BOOL CreateUrl(LPTSTR lpszUrl, DWORD* pdwMaxLength, DWORD dwFlags = 0) const {
    if (pdwMaxLength == NULL) return FALSE;
    _AtlUtilString str = m_strScheme;
    str += _T(':');
    if (!m_strHostName.IsEmpty() || m_nScheme == ATL_URL_SCHEME_FILE) {
      str += _T("//");
      if (!m_strUserName.IsEmpty()) {
        str += m_strUserName;
        if (!m_strPassword.IsEmpty()) {
          str += _T(':');
          str += m_strPassword;
        }
        str += _T('@');
      }
      str += m_strHostName;
      if (m_nPortNumber != ATL_URL_INVALID_PORT_NUMBER &&
          m_nPortNumber != AtlGetDefaultUrlPort(m_nScheme)) {
        TCHAR szPort[16];
        wsprintf(szPort, _T(":%u"), (unsigned)m_nPortNumber);
        str += szPort;
      }
    }
    _AtlUtilString strPath = m_strUrlPath;
    if (!m_strHostName.IsEmpty() && !strPath.IsEmpty() && strPath[0] != _T('/'))
      strPath = _T("/") + strPath;
    _AtlUtilString strTail = strPath + m_strExtraInfo;
    if (dwFlags & ATL_URL_ESCAPE) {
      DWORD dwLen = 0;
      _AtlUtilString strEsc;
      DWORD dwMax = (DWORD)strTail.GetLength() * 3 + 1;
      AtlEscapeUrl((LPCTSTR)strTail, strEsc.GetBuffer((int)dwMax), &dwLen, dwMax,
                   dwFlags | ATL_URL_BROWSER_MODE);
      strEsc.ReleaseBuffer((int)dwLen);
      strTail = strEsc;
    }
    str += strTail;
    DWORD dwNeeded = (DWORD)str.GetLength() + 1;
    if (lpszUrl == NULL || *pdwMaxLength < dwNeeded) {
      *pdwMaxLength = dwNeeded;
      return FALSE;
    }
    lstrcpyn(lpszUrl, str, (int)dwNeeded);
    *pdwMaxLength = dwNeeded - 1;
    return TRUE;
  }

  BOOL Canonicalize(DWORD dwFlags = 0) {
    m_strScheme.MakeLower();
    m_strHostName.MakeLower();
    if (!(dwFlags & ATL_URL_NO_META)) _AtlRemoveDotSegments(m_strUrlPath);
    if (dwFlags & ATL_URL_DECODE) Unescape(m_strUrlPath);
    return TRUE;
  }

  ATL_URL_SCHEME GetScheme() const { return m_nScheme; }
  LPCTSTR GetSchemeName() const { return m_strScheme; }
  DWORD GetSchemeNameLength() const { return (DWORD)m_strScheme.GetLength(); }
  LPCTSTR GetHostName() const { return m_strHostName; }
  DWORD GetHostNameLength() const { return (DWORD)m_strHostName.GetLength(); }
  ATL_URL_PORT GetPortNumber() const { return m_nPortNumber; }
  LPCTSTR GetUserName() const { return m_strUserName; }
  DWORD GetUserNameLength() const { return (DWORD)m_strUserName.GetLength(); }
  LPCTSTR GetPassword() const { return m_strPassword; }
  DWORD GetPasswordLength() const { return (DWORD)m_strPassword.GetLength(); }
  LPCTSTR GetUrlPath() const { return m_strUrlPath; }
  DWORD GetUrlPathLength() const { return (DWORD)m_strUrlPath.GetLength(); }
  LPCTSTR GetExtraInfo() const { return m_strExtraInfo; }
  DWORD GetExtraInfoLength() const { return (DWORD)m_strExtraInfo.GetLength(); }
  DWORD GetUrlLength() const {
    DWORD dw = 0;
    CreateUrl(NULL, &dw);
    return dw > 0 ? dw - 1 : 0;
  }

  BOOL SetScheme(ATL_URL_SCHEME nScheme) {
    LPCTSTR psz = SchemeName(nScheme);
    if (psz == NULL) return FALSE;
    m_nScheme = nScheme;
    m_strScheme = psz;
    m_nPortNumber = AtlGetDefaultUrlPort(nScheme);
    return TRUE;
  }
  BOOL SetSchemeName(LPCTSTR lpszSchm) {
    if (lpszSchm == NULL || *lpszSchm == 0 || lstrlen(lpszSchm) > ATL_URL_MAX_SCHEME_LENGTH)
      return FALSE;
    m_strScheme = lpszSchm;
    m_nScheme = ATL_URL_SCHEME_UNKNOWN;
    for (int n = ATL_URL_SCHEME_FTP; n <= ATL_URL_SCHEME_SOCKS; n++) {
      if (m_strScheme.CompareNoCase(SchemeName((ATL_URL_SCHEME)n)) == 0) {
        m_nScheme = (ATL_URL_SCHEME)n;
        break;
      }
    }
    return TRUE;
  }
  BOOL SetHostName(LPCTSTR lpszHost) { return SetPart(m_strHostName, lpszHost, ATL_URL_MAX_HOST_NAME_LENGTH); }
  BOOL SetPortNumber(ATL_URL_PORT nPrt) {
    m_nPortNumber = nPrt;
    return TRUE;
  }
  BOOL SetUserName(LPCTSTR lpszUser) { return SetPart(m_strUserName, lpszUser, ATL_URL_MAX_USER_NAME_LENGTH); }
  BOOL SetPassword(LPCTSTR lpszPass) { return SetPart(m_strPassword, lpszPass, ATL_URL_MAX_PASSWORD_LENGTH); }
  BOOL SetUrlPath(LPCTSTR lpszPath) { return SetPart(m_strUrlPath, lpszPath, ATL_URL_MAX_PATH_LENGTH); }
  BOOL SetExtraInfo(LPCTSTR lpszInfo) { return SetPart(m_strExtraInfo, lpszInfo, ATL_URL_MAX_PATH_LENGTH); }

private:
  _AtlUtilString m_strScheme;
  ATL_URL_SCHEME m_nScheme;
  _AtlUtilString m_strHostName;
  ATL_URL_PORT m_nPortNumber;
  _AtlUtilString m_strUserName;
  _AtlUtilString m_strPassword;
  _AtlUtilString m_strUrlPath;
  _AtlUtilString m_strExtraInfo;

  static LPCTSTR SchemeName(ATL_URL_SCHEME n) {
    switch (n) {
      case ATL_URL_SCHEME_FTP: return _T("ftp");
      case ATL_URL_SCHEME_GOPHER: return _T("gopher");
      case ATL_URL_SCHEME_HTTP: return _T("http");
      case ATL_URL_SCHEME_HTTPS: return _T("https");
      case ATL_URL_SCHEME_FILE: return _T("file");
      case ATL_URL_SCHEME_NEWS: return _T("news");
      case ATL_URL_SCHEME_MAILTO: return _T("mailto");
      case ATL_URL_SCHEME_SOCKS: return _T("socks");
      default: return NULL;
    }
  }
  static BOOL SetPart(_AtlUtilString& str, LPCTSTR psz, int nMax) {
    if (psz == NULL || lstrlen(psz) > nMax) return FALSE;
    str = psz;
    return TRUE;
  }
  static void Unescape(_AtlUtilString& str) {
    if (str.IsEmpty()) return;
    DWORD dwLen = 0;
    _AtlUtilString strOut;
    DWORD dwMax = (DWORD)str.GetLength() + 1;
    AtlUnescapeUrl((LPCTSTR)str, strOut.GetBuffer((int)dwMax), &dwLen, dwMax);
    strOut.ReleaseBuffer((int)dwLen);
    str = strOut;
  }
};

inline BOOL AtlCanonicalizeUrl(LPCTSTR szUrl, LPTSTR szCanonicalized, DWORD* pdwMaxLength,
                               DWORD dwFlags = 0) {
  CUrl url;
  if (!url.CrackUrl(szUrl, (dwFlags & ATL_URL_DECODE) ? ATL_URL_DECODE : 0)) return FALSE;
  url.Canonicalize(dwFlags);
  return url.CreateUrl(szCanonicalized, pdwMaxLength,
                       (dwFlags & ATL_URL_NO_ENCODE) ? 0 : (dwFlags & ~ATL_URL_DECODE));
}

// A relative URL against a base: as RFC 1808 resolves it.
inline BOOL AtlCombineUrl(LPCTSTR szBaseUrl, LPCTSTR szRelativeUrl, LPTSTR szBuffer,
                          DWORD* pdwMaxLength, DWORD dwFlags = 0) {
  CUrl rel;
  if (szRelativeUrl != NULL && rel.CrackUrl(szRelativeUrl))
    return AtlCanonicalizeUrl(szRelativeUrl, szBuffer, pdwMaxLength, dwFlags);
  CUrl base;
  if (!base.CrackUrl(szBaseUrl) || szRelativeUrl == NULL) return FALSE;
  _AtlUtilString strRel = szRelativeUrl;
  _AtlUtilString strExtra;
  int nExtra = strRel.FindOneOf(_T("?#"));
  if (nExtra >= 0) {
    strExtra = strRel.Mid(nExtra);
    strRel = strRel.Left(nExtra);
  }
  if (strRel.IsEmpty()) {
    if (!strExtra.IsEmpty()) base.SetExtraInfo(strExtra);
  } else if (strRel[0] == _T('/')) {
    base.SetUrlPath(strRel);
    base.SetExtraInfo(strExtra);
  } else {
    _AtlUtilString strPath = base.GetUrlPath();
    int nSlash = strPath.ReverseFind(_T('/'));
    strPath = nSlash >= 0 ? strPath.Left(nSlash + 1) : _AtlUtilString(_T("/"));
    base.SetUrlPath(strPath + strRel);
    base.SetExtraInfo(strExtra);
  }
  base.Canonicalize(dwFlags);
  return base.CreateUrl(szBuffer, pdwMaxLength, dwFlags & ATL_URL_ESCAPE);
}

// ---------------------------------------------------------------------
// CWorkerThread: one thread waiting on up to MAXIMUM_WAIT_OBJECTS - 1
// handles; when one is signaled, its client's Execute runs on the thread.

#ifndef ATL_WORKER_THREAD_WAIT
#define ATL_WORKER_THREAD_WAIT 10000
#endif

struct IWorkerThreadClient {
  virtual HRESULT Execute(DWORD_PTR dwParam, HANDLE hObject) = 0;
  virtual HRESULT CloseHandle(HANDLE hHandle) = 0;
};

template <class ThreadTraits = DefaultThreadTraits>
class CWorkerThread {
public:
  CWorkerThread() : m_hThread(NULL), m_dwThreadId(0), m_pShared(NULL), m_nHandles(0) {
    ::InitializeCriticalSection(&m_cs);
    m_hRefresh = NULL;
    m_hAck = NULL;
    m_bShutdown = false;
  }
  ~CWorkerThread() {
    Shutdown();
    ::DeleteCriticalSection(&m_cs);
  }

  HRESULT Initialize() {
    if (m_hThread != NULL || m_pShared != NULL) return E_UNEXPECTED;
    m_hRefresh = ::CreateEvent(NULL, FALSE, FALSE, NULL);
    m_hAck = ::CreateEvent(NULL, FALSE, FALSE, NULL);
    if (m_hRefresh == NULL || m_hAck == NULL) return AtlHresultFromLastError();
    m_bShutdown = false;
    m_hThread = ThreadTraits::CreateThread(NULL, 0, ThreadProc, this, 0, &m_dwThreadId);
    if (m_hThread == NULL) return AtlHresultFromLastError();
    return S_OK;
  }
  // Shares another CWorkerThread's thread: the handles go to it.
  HRESULT Initialize(CWorkerThread<ThreadTraits>* pThread) {
    if (pThread == NULL || pThread->m_hThread == NULL) return E_INVALIDARG;
    m_pShared = pThread;
    return S_OK;
  }

  HRESULT Shutdown(DWORD dwWait = ATL_WORKER_THREAD_WAIT) {
    if (m_pShared != NULL) {
      m_pShared = NULL;
      return S_OK;
    }
    if (m_hThread == NULL) return S_OK;
    ::EnterCriticalSection(&m_cs);
    m_bShutdown = true;
    ::LeaveCriticalSection(&m_cs);
    ::SetEvent(m_hRefresh);
    HRESULT hr = S_OK;
    if (::WaitForSingleObject(m_hThread, dwWait) != WAIT_OBJECT_0) {
      ::TerminateThread(m_hThread, 0);
      hr = S_FALSE;
    }
    // The clients close the handles that are left.
    for (int i = 0; i < m_nHandles; i++) m_clients[i]->CloseHandle(m_handles[i]);
    m_nHandles = 0;
    ::CloseHandle(m_hThread);
    ::CloseHandle(m_hRefresh);
    ::CloseHandle(m_hAck);
    m_hThread = m_hRefresh = m_hAck = NULL;
    return hr;
  }

  HRESULT AddHandle(HANDLE hObject, IWorkerThreadClient* pClient, DWORD_PTR dwParam) {
    if (m_pShared != NULL) return m_pShared->AddHandle(hObject, pClient, dwParam);
    if (m_hThread == NULL) return E_UNEXPECTED;
    ::EnterCriticalSection(&m_cs);
    if (m_nHandles >= MAXIMUM_WAIT_OBJECTS - 1) {
      ::LeaveCriticalSection(&m_cs);
      return E_FAIL;
    }
    m_handles[m_nHandles] = hObject;
    m_clients[m_nHandles] = pClient;
    m_params[m_nHandles] = dwParam;
    m_nHandles++;
    ::LeaveCriticalSection(&m_cs);
    Refresh();
    return S_OK;
  }

  // A periodic waitable timer, first due after dwInterval milliseconds.
  HRESULT AddTimer(DWORD dwInterval, IWorkerThreadClient* pClient, DWORD_PTR dwParam,
                   HANDLE* phTimer) {
    if (phTimer == NULL) return E_POINTER;
    *phTimer = NULL;
    HANDLE hTimer = ::CreateWaitableTimer(NULL, FALSE, NULL);
    if (hTimer == NULL) return AtlHresultFromLastError();
    LARGE_INTEGER liDue;
    liDue.QuadPart = -(LONGLONG)dwInterval * 10000;
    if (!::SetWaitableTimer(hTimer, &liDue, (LONG)dwInterval, NULL, NULL, FALSE)) {
      HRESULT hr = AtlHresultFromLastError();
      ::CloseHandle(hTimer);
      return hr;
    }
    HRESULT hr = AddHandle(hTimer, pClient, dwParam);
    if (FAILED(hr)) {
      ::CloseHandle(hTimer);
      return hr;
    }
    *phTimer = hTimer;
    return S_OK;
  }

  // Stops waiting on the handle, then has its client close it.
  HRESULT RemoveHandle(HANDLE hObject) {
    if (m_pShared != NULL) return m_pShared->RemoveHandle(hObject);
    ::EnterCriticalSection(&m_cs);
    IWorkerThreadClient* pClient = NULL;
    for (int i = 0; i < m_nHandles; i++) {
      if (m_handles[i] == hObject) {
        pClient = m_clients[i];
        for (int j = i + 1; j < m_nHandles; j++) {
          m_handles[j - 1] = m_handles[j];
          m_clients[j - 1] = m_clients[j];
          m_params[j - 1] = m_params[j];
        }
        m_nHandles--;
        break;
      }
    }
    ::LeaveCriticalSection(&m_cs);
    if (pClient == NULL) return E_INVALIDARG;
    Refresh();
    return pClient->CloseHandle(hObject);
  }

  HANDLE GetThreadHandle() { return m_pShared != NULL ? m_pShared->m_hThread : m_hThread; }
  DWORD GetThreadId() { return m_pShared != NULL ? m_pShared->m_dwThreadId : m_dwThreadId; }

private:
  HANDLE m_hThread;
  DWORD m_dwThreadId;
  CWorkerThread<ThreadTraits>* m_pShared;
  CRITICAL_SECTION m_cs;
  HANDLE m_hRefresh, m_hAck;
  bool m_bShutdown;
  int m_nHandles;
  HANDLE m_handles[MAXIMUM_WAIT_OBJECTS];
  IWorkerThreadClient* m_clients[MAXIMUM_WAIT_OBJECTS];
  DWORD_PTR m_params[MAXIMUM_WAIT_OBJECTS];

  // Has the thread pick up the new handle list; from another thread,
  // waits until it has, so a removed handle is no longer waited on.
  void Refresh() {
    if (::GetCurrentThreadId() == m_dwThreadId) {
      ::SetEvent(m_hRefresh);
      return;
    }
    ::ResetEvent(m_hAck);
    ::SetEvent(m_hRefresh);
    ::WaitForSingleObject(m_hAck, ATL_WORKER_THREAD_WAIT);
  }

  static DWORD WINAPI ThreadProc(LPVOID pv) {
    CWorkerThread<ThreadTraits>* pThis = (CWorkerThread<ThreadTraits>*)pv;
    HANDLE handles[MAXIMUM_WAIT_OBJECTS];
    IWorkerThreadClient* clients[MAXIMUM_WAIT_OBJECTS];
    DWORD_PTR params[MAXIMUM_WAIT_OBJECTS];
    for (;;) {
      ::EnterCriticalSection(&pThis->m_cs);
      if (pThis->m_bShutdown) {
        ::LeaveCriticalSection(&pThis->m_cs);
        return 0;
      }
      int n = pThis->m_nHandles;
      handles[0] = pThis->m_hRefresh;
      for (int i = 0; i < n; i++) {
        handles[i + 1] = pThis->m_handles[i];
        clients[i + 1] = pThis->m_clients[i];
        params[i + 1] = pThis->m_params[i];
      }
      ::LeaveCriticalSection(&pThis->m_cs);
      DWORD dw = ::WaitForMultipleObjects((DWORD)n + 1, handles, FALSE, INFINITE);
      if (dw == WAIT_OBJECT_0) {
        ::SetEvent(pThis->m_hAck);  // picked up below, at the top
        continue;
      }
      if (dw > WAIT_OBJECT_0 && dw <= WAIT_OBJECT_0 + (DWORD)n) {
        int i = (int)(dw - WAIT_OBJECT_0);
        clients[i]->Execute(params[i], handles[i]);
      } else if (dw == WAIT_FAILED) {
        // A handle closed under us: wait for the list to change.
        ::WaitForSingleObject(pThis->m_hRefresh, INFINITE);
        ::SetEvent(pThis->m_hAck);
      }
    }
  }

  CWorkerThread(const CWorkerThread&);
  CWorkerThread& operator=(const CWorkerThread&);
};

// ---------------------------------------------------------------------
// CThreadPool: Worker is a class with
//   typedef ... RequestType;   (fits in a ULONG_PTR)
//   BOOL Initialize(void* pvWorkerParam);
//   void Execute(RequestType request, void* pvWorkerParam, OVERLAPPED* pOverlapped);
//   void Terminate(void* pvWorkerParam);
// One Worker object lives on each thread. A negative thread count means
// that many per processor; 0 means two per processor.

#ifndef ATLS_DEFAULT_THREADSPERPROC
#define ATLS_DEFAULT_THREADSPERPROC 2
#endif
#ifndef ATLS_DEFAULT_THREADPOOLSHUTDOWNTIMEOUT
#define ATLS_DEFAULT_THREADPOOLSHUTDOWNTIMEOUT 36000
#endif

struct IThreadPoolConfig : public IUnknown {
  virtual HRESULT STDMETHODCALLTYPE SetSize(int nNumThreads) = 0;
  virtual HRESULT STDMETHODCALLTYPE GetSize(int* pnNumThreads) = 0;
  virtual HRESULT STDMETHODCALLTYPE SetTimeout(DWORD dwMaxWait) = 0;
  virtual HRESULT STDMETHODCALLTYPE GetTimeout(DWORD* pdwMaxWait) = 0;
};

template <class Worker, class ThreadTraits = DefaultThreadTraits>
class CThreadPool : public IThreadPoolConfig {
public:
  CThreadPool()
      : m_hRequestQueue(NULL), m_pvWorkerParam(NULL), m_dwStackSize(0),
        m_dwMaxWait(ATLS_DEFAULT_THREADPOOLSHUTDOWNTIMEOUT), m_nStarted(0) {
    ::InitializeCriticalSection(&m_cs);
    m_hThreadEvent = NULL;
  }
  ~CThreadPool() {
    Shutdown();
    ::DeleteCriticalSection(&m_cs);
  }

  HRESULT Initialize(void* pvWorkerParam = NULL, int nNumThreads = 0, DWORD dwStackSize = 0,
                     HANDLE hCompletion = INVALID_HANDLE_VALUE) {
    if (m_hRequestQueue != NULL) return E_UNEXPECTED;
    m_pvWorkerParam = pvWorkerParam;
    m_dwStackSize = dwStackSize;
    m_hRequestQueue = ::CreateIoCompletionPort(hCompletion, NULL, 0, 0);
    if (m_hRequestQueue == NULL) return AtlHresultFromLastError();
    m_hThreadEvent = ::CreateEvent(NULL, FALSE, FALSE, NULL);
    HRESULT hr = SetSize(nNumThreads);
    if (FAILED(hr)) Shutdown();
    return hr;
  }

  void Shutdown(DWORD dwMaxWait = 0) {
    if (m_hRequestQueue == NULL) return;
    if (dwMaxWait == 0) dwMaxWait = m_dwMaxWait;
    ShrinkTo(0, dwMaxWait);
    ::CloseHandle(m_hRequestQueue);
    ::CloseHandle(m_hThreadEvent);
    m_hRequestQueue = m_hThreadEvent = NULL;
  }

  BOOL QueueRequest(typename Worker::RequestType request) {
    return ::PostQueuedCompletionStatus(m_hRequestQueue, 0, (ULONG_PTR)request, NULL);
  }

  HANDLE GetQueueHandle() { return m_hRequestQueue; }
  int GetNumThreads() { return (int)m_threads.size(); }

  // IThreadPoolConfig
  HRESULT STDMETHODCALLTYPE SetSize(int nNumThreads) {
    if (m_hRequestQueue == NULL) return E_UNEXPECTED;
    if (nNumThreads == 0) nNumThreads = -ATLS_DEFAULT_THREADSPERPROC;
    if (nNumThreads < 0) {
      SYSTEM_INFO si;
      ::GetSystemInfo(&si);
      nNumThreads = (int)si.dwNumberOfProcessors * -nNumThreads;
    }
    ::EnterCriticalSection(&m_cs);
    HRESULT hr = S_OK;
    while ((int)m_threads.size() < nNumThreads && SUCCEEDED(hr)) hr = AddThread();
    ::LeaveCriticalSection(&m_cs);
    if ((int)m_threads.size() > nNumThreads) ShrinkTo(nNumThreads, m_dwMaxWait);
    return hr;
  }
  HRESULT STDMETHODCALLTYPE GetSize(int* pnNumThreads) {
    if (pnNumThreads == NULL) return E_POINTER;
    *pnNumThreads = GetNumThreads();
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE SetTimeout(DWORD dwMaxWait) {
    m_dwMaxWait = dwMaxWait;
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE GetTimeout(DWORD* pdwMaxWait) {
    if (pdwMaxWait == NULL) return E_POINTER;
    *pdwMaxWait = m_dwMaxWait;
    return S_OK;
  }

  // IUnknown: the pool lives as long as its owner says.
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) {
    if (ppv == NULL) return E_POINTER;
    if (::InlineIsEqualGUID(riid, IID_IUnknown)) {
      *ppv = static_cast<IUnknown*>(this);
      return S_OK;
    }
    *ppv = NULL;
    return E_NOINTERFACE;
  }
  ULONG STDMETHODCALLTYPE AddRef() { return 1; }
  ULONG STDMETHODCALLTYPE Release() { return 1; }

private:
  HANDLE m_hRequestQueue;
  HANDLE m_hThreadEvent;
  void* m_pvWorkerParam;
  DWORD m_dwStackSize;
  DWORD m_dwMaxWait;
  CRITICAL_SECTION m_cs;
  std::vector<HANDLE> m_threads;
  BOOL m_bWorkerOK;
  int m_nStarted;

  // The shutdown request a thread ends on.
  static OVERLAPPED* ShutdownKey() { return (OVERLAPPED*)(INT_PTR)-1; }

  HRESULT AddThread() {
    DWORD dwId = 0;
    m_bWorkerOK = FALSE;
    HANDLE h = ThreadTraits::CreateThread(NULL, m_dwStackSize, ThreadProc, this, 0, &dwId);
    if (h == NULL) return AtlHresultFromLastError();
    // Wait for the thread's worker to say whether it started.
    ::WaitForSingleObject(m_hThreadEvent, INFINITE);
    if (!m_bWorkerOK) {
      ::WaitForSingleObject(h, INFINITE);
      ::CloseHandle(h);
      return E_FAIL;
    }
    m_threads.push_back(h);
    return S_OK;
  }

  void ShrinkTo(int nThreads, DWORD dwMaxWait) {
    ::EnterCriticalSection(&m_cs);
    while ((int)m_threads.size() > nThreads) {
      ::PostQueuedCompletionStatus(m_hRequestQueue, 0, 0, ShutdownKey());
      // Whichever thread took it ends; find and reap it.
      DWORD dw = ::WaitForMultipleObjects((DWORD)m_threads.size(), &m_threads[0], FALSE,
                                          dwMaxWait);
      size_t i;
      if (dw >= WAIT_OBJECT_0 && dw < WAIT_OBJECT_0 + m_threads.size()) {
        i = dw - WAIT_OBJECT_0;
      } else {
        i = m_threads.size() - 1;
        ::TerminateThread(m_threads[i], 0);
      }
      ::CloseHandle(m_threads[i]);
      m_threads.erase(m_threads.begin() + i);
    }
    ::LeaveCriticalSection(&m_cs);
  }

  static DWORD WINAPI ThreadProc(LPVOID pv) {
    CThreadPool* pThis = (CThreadPool*)pv;
    Worker worker;
    pThis->m_bWorkerOK = worker.Initialize(pThis->m_pvWorkerParam);
    BOOL bOK = pThis->m_bWorkerOK;
    ::SetEvent(pThis->m_hThreadEvent);
    if (!bOK) return 1;
    for (;;) {
      DWORD dwBytes = 0;
      ULONG_PTR key = 0;
      OVERLAPPED* pOverlapped = NULL;
      BOOL b = ::GetQueuedCompletionStatus(pThis->m_hRequestQueue, &dwBytes, &key, &pOverlapped,
                                           INFINITE);
      if (pOverlapped == ShutdownKey()) break;
      if (!b && pOverlapped == NULL) break;  // the port is gone
      worker.Execute((typename Worker::RequestType)key, pThis->m_pvWorkerParam, pOverlapped);
    }
    worker.Terminate(pThis->m_pvWorkerParam);
    return 0;
  }

  CThreadPool(const CThreadPool&);
  CThreadPool& operator=(const CThreadPool&);
};

}  // namespace ATL

#endif  // __ATLUTIL_H__
