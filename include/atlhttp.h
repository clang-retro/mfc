// atlhttp.h - CAtlHttpClient: HTTP/1.1 requests over a ZEvtSyncSocket (or
// any socket class with its methods). Connections are kept open between
// requests to the same server; bodies may come with a length, chunked, or
// up to the close. Redirects are followed and Basic authentication done
// when the flags ask for them, and requests can go through a proxy.
// Winsock must be started first.
//
// Navigate returns true once a response is read, whatever its status;
// GetStatus has that. Only http: URLs; NTLM authentication is not here.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#ifndef __ATLHTTP_H__
#define __ATLHTTP_H__

#include <atlsocket.h>
#include <atlutil.h>
#include <atlstr.h>
#include <atlcoll.h>
#include <atlenc.h>

namespace ATL {

#define ATL_HTTP_FLAG_AUTO_REDIRECT 0x1
#define ATL_HTTP_FLAG_PROCESS_RESULT 0x2
#define ATL_HTTP_FLAG_SEND_CALLBACK 0x4
#define ATL_HTTP_FLAG_SEND_BLOCKS 0x8
#define ATL_HTTP_FLAG_INVALID_FLAGS 0xFFFFFFFF

#define ATL_HTTP_METHOD_GET _T("GET")
#define ATL_HTTP_METHOD_POST _T("POST")
#define ATL_HTTP_METHOD_HEAD _T("HEAD")
#define ATL_HTTP_METHOD_PUT _T("PUT")
#define ATL_HTTP_METHOD_DELETE _T("DELETE")

#ifndef ATL_HTTP_DEFAULT_BLOCK_SIZE
#define ATL_HTTP_DEFAULT_BLOCK_SIZE 4096
#endif
#ifndef ATL_HTTP_USERAGENT
#define ATL_HTTP_USERAGENT "Microsoft-ATL-Native/9.00"
#endif
#ifndef ATL_HTTP_MAX_REDIRECTS
#define ATL_HTTP_MAX_REDIRECTS 10
#endif
#define ATL_INVALID_STATUS -1
#define ATL_URL_DEFAULT_HTTP_PORT 80

#define ATL_AUTH_HDR_SIZE 1024

typedef bool(WINAPI* PFNATLCHUNKEDCB)(BYTE** ppData, DWORD* pdwSize, DWORD_PTR dwParam);
typedef bool(WINAPI* PFNATLSTATUSCALLBACK)(DWORD dwBytesSent, DWORD_PTR dwParam);

struct ATL_NAVIGATE_DATA {
  LPCTSTR szExtraHeaders;
  LPCTSTR szMethod;
  LPCTSTR szDataType;
  DWORD dwDataLen;
  DWORD dwFlags;
  DWORD dwTimeout;
  DWORD dwSendBlockSize;
  DWORD dwReadBlockSize;
  DWORD_PTR m_lParamSend;
  DWORD_PTR m_lParamRead;
  DWORD_PTR m_lParamChunkCB;
  short nPort;
  BYTE* pData;
  PFNATLCHUNKEDCB pfnChunkCallback;
  PFNATLSTATUSCALLBACK pfnSendStatusCallback;
  PFNATLSTATUSCALLBACK pfnReadStatusCallback;
};

class CAtlNavigateData : public ATL_NAVIGATE_DATA {
public:
  CAtlNavigateData() {
    szExtraHeaders = NULL;
    szMethod = ATL_HTTP_METHOD_GET;
    szDataType = NULL;
    dwDataLen = 0;
    dwFlags = ATL_HTTP_FLAG_AUTO_REDIRECT | ATL_HTTP_FLAG_PROCESS_RESULT;
    dwTimeout = ATL_SOCK_TIMEOUT;
    dwSendBlockSize = ATL_HTTP_DEFAULT_BLOCK_SIZE;
    dwReadBlockSize = ATL_HTTP_DEFAULT_BLOCK_SIZE;
    m_lParamSend = m_lParamRead = m_lParamChunkCB = 0;
    nPort = ATL_URL_DEFAULT_HTTP_PORT;
    pData = NULL;
    pfnChunkCallback = NULL;
    pfnSendStatusCallback = NULL;
    pfnReadStatusCallback = NULL;
  }
  CAtlNavigateData(const CAtlNavigateData& rhs) : ATL_NAVIGATE_DATA(rhs) {}
  CAtlNavigateData(const ATL_NAVIGATE_DATA& rhs) : ATL_NAVIGATE_DATA(rhs) {}
  CAtlNavigateData& operator=(const ATL_NAVIGATE_DATA& rhs) {
    ATL_NAVIGATE_DATA::operator=(rhs);
    return *this;
  }

  DWORD SetFlags(DWORD dwNewFlags) {
    DWORD dw = dwFlags;
    dwFlags = dwNewFlags;
    return dw;
  }
  DWORD GetFlags() const { return dwFlags; }
  LPCTSTR SetExtraHeaders(LPCTSTR szNewHeaders) {
    LPCTSTR p = szExtraHeaders;
    szExtraHeaders = szNewHeaders;
    return p;
  }
  LPCTSTR GetExtraHeaders() const { return szExtraHeaders; }
  LPCTSTR SetMethod(LPCTSTR szNewMethod) {
    LPCTSTR p = szMethod;
    szMethod = szNewMethod;
    return p;
  }
  LPCTSTR GetMethod() const { return szMethod; }
  BOOL SetPostData(BYTE* pd, DWORD len, LPCTSTR type) {
    pData = pd;
    dwDataLen = len;
    szDataType = type;
    return TRUE;
  }
  DWORD SetSocketTimeout(DWORD dwNewTimeout) {
    DWORD dw = dwTimeout;
    dwTimeout = dwNewTimeout;
    return dw;
  }
  DWORD GetSocketTimeout() const { return dwTimeout; }
  DWORD SetSendBlockSize(DWORD dwBlockSize) {
    DWORD dw = dwSendBlockSize;
    dwSendBlockSize = dwBlockSize;
    return dw;
  }
  DWORD GetSendBlockSize() const { return dwSendBlockSize; }
  DWORD SetReadBlockSize(DWORD dwBlockSize) {
    DWORD dw = dwReadBlockSize;
    dwReadBlockSize = dwBlockSize;
    return dw;
  }
  DWORD GetReadBlockSize() const { return dwReadBlockSize; }
  PFNATLCHUNKEDCB SetChunkCallback(PFNATLCHUNKEDCB pfn, DWORD_PTR dwParam) {
    PFNATLCHUNKEDCB p = pfnChunkCallback;
    pfnChunkCallback = pfn;
    m_lParamChunkCB = dwParam;
    return p;
  }
  PFNATLCHUNKEDCB GetChunkCallback() const { return pfnChunkCallback; }
  PFNATLSTATUSCALLBACK SetSendStatusCallback(PFNATLSTATUSCALLBACK pfn, DWORD_PTR dwData) {
    PFNATLSTATUSCALLBACK p = pfnSendStatusCallback;
    pfnSendStatusCallback = pfn;
    m_lParamSend = dwData;
    return p;
  }
  PFNATLSTATUSCALLBACK GetSendStatusCallback() const { return pfnSendStatusCallback; }
  PFNATLSTATUSCALLBACK SetReadStatusCallback(PFNATLSTATUSCALLBACK pfn, DWORD_PTR dwData) {
    PFNATLSTATUSCALLBACK p = pfnReadStatusCallback;
    pfnReadStatusCallback = pfn;
    m_lParamRead = dwData;
    return p;
  }
  PFNATLSTATUSCALLBACK GetReadStatusCallback() const { return pfnReadStatusCallback; }
  short SetPort(short newPort) {
    short n = nPort;
    nPort = newPort;
    return n;
  }
  short GetPort() const { return nPort; }
};

// Where an authentication object gets the user's name and password.
struct IAuthInfo {
  virtual HRESULT GetPassword(LPTSTR szPwd, DWORD* dwBuffSize) = 0;
  virtual HRESULT GetUsername(LPTSTR szUid, DWORD* dwBuffSize) = 0;
  virtual HRESULT GetDomain(LPTSTR szDomain, DWORD* dwBuffSize) = 0;
};
typedef HRESULT (IAuthInfo::*PFNAUTHFUNC)(LPTSTR szPwd, DWORD* pdwSize);

// Asks pAuthInfo for a string, growing the buffer as it says to.
inline bool _AtlGetAuthInfo(IAuthInfo* pAuthInfo, PFNAUTHFUNC pfn, CString& str) {
  if (pAuthInfo == NULL) return false;
  DWORD dw = 0;
  HRESULT hr = (pAuthInfo->*pfn)(NULL, &dw);
  if (FAILED(hr) && dw == 0) return false;
  if (dw == 0) {
    str.Empty();
    return true;
  }
  hr = (pAuthInfo->*pfn)(str.GetBuffer((int)dw + 1), &dw);
  str.ReleaseBuffer();
  return SUCCEEDED(hr);
}

template <class TSocketClass>
class CAtlHttpClientT;
typedef CAtlHttpClientT<ZEvtSyncSocket> CAtlHttpClient;

// What authentication objects are given: the client, when it is a
// CAtlHttpClient.
inline CAtlHttpClient* _AtlHttpAsClient(CAtlHttpClient* p) { return p; }
template <class T>
inline CAtlHttpClient* _AtlHttpAsClient(T*) { return NULL; }

class CAtlBaseAuthObject {
public:
  CAtlBaseAuthObject() : m_bFailed(false) {}
  virtual ~CAtlBaseAuthObject() {}
  virtual bool Authenticate(LPCTSTR szAuthTypes, bool bProxy) = 0;
  virtual void Init(CAtlHttpClient* pSocket, IAuthInfo* pAuthInfo) = 0;
  bool m_bFailed;
};

// ---------------------------------------------------------------------

template <class TSocketClass>
class CAtlHttpClientT : public TSocketClass {
public:
  typedef TSocketClass _SocketClass;

  CAtlHttpClientT()
      : m_nBodyOffset(0), m_nStatus(ATL_INVALID_STATUS), m_nConnPort(0), m_nProxyPort(0),
        m_bSilentLogonOk(false), m_pNavData(NULL), m_bProxyAuth(false), m_nAuthDepth(0) {}
  ~CAtlHttpClientT() {
    Close();
    RemoveAllAuthObjs();
  }

  bool Navigate(LPCTSTR szUrl, ATL_NAVIGATE_DATA* pNavData = NULL) {
    CUrl url;
    if (szUrl == NULL || !url.CrackUrl(szUrl)) return false;
    return Navigate(&url, pNavData);
  }
  bool Navigate(LPCTSTR szServer, LPCTSTR szPath, ATL_NAVIGATE_DATA* pNavData = NULL) {
    if (szServer == NULL) return false;
    CUrl url;
    url.SetScheme(ATL_URL_SCHEME_HTTP);
    url.SetHostName(szServer);
    url.SetPortNumber(pNavData != NULL ? (ATL_URL_PORT)pNavData->nPort : ATL_URL_DEFAULT_HTTP_PORT);
    CString strPath(szPath != NULL ? szPath : _T("/"));
    int nQ = strPath.Find(_T('?'));
    if (nQ >= 0) {
      url.SetExtraInfo(strPath.Mid(nQ));
      strPath = strPath.Left(nQ);
    }
    url.SetUrlPath(strPath);
    return Navigate(&url, pNavData);
  }
  bool Navigate(const CUrl* pUrl, ATL_NAVIGATE_DATA* pNavData = NULL) {
    return _Navigate(pUrl, pNavData, false);
  }
  // The body sent chunked, each piece from the chunk callback until it
  // gives zero bytes.
  bool NavigateChunked(LPCTSTR szUrl, ATL_NAVIGATE_DATA* pNavData) {
    CUrl url;
    if (szUrl == NULL || !url.CrackUrl(szUrl)) return false;
    return NavigateChunked(&url, pNavData);
  }
  bool NavigateChunked(LPCTSTR szServer, LPCTSTR szPath, ATL_NAVIGATE_DATA* pNavData) {
    CString strUrl;
    strUrl.Format(_T("http://%s:%d%s"), szServer, pNavData != NULL ? (int)pNavData->nPort : 80,
                  szPath != NULL ? szPath : _T("/"));
    return NavigateChunked(strUrl, pNavData);
  }
  bool NavigateChunked(const CUrl* pUrl, ATL_NAVIGATE_DATA* pNavData) {
    if (pNavData == NULL || pNavData->pfnChunkCallback == NULL) return false;
    return _Navigate(pUrl, pNavData, true);
  }

  int GetStatus() const { return m_nStatus; }
  LPCTSTR GetMethod() const { return m_strMethod; }
  const BYTE* GetBody() const {
    return m_response.GetCount() > m_nBodyOffset ? m_response.GetData() + m_nBodyOffset : NULL;
  }
  DWORD GetBodyLength() const { return (DWORD)(m_response.GetCount() - m_nBodyOffset); }
  const BYTE* GetResponse() const { return m_response.GetCount() ? m_response.GetData() : NULL; }
  DWORD GetResponseLength() const { return (DWORD)m_response.GetCount(); }
  LPCSTR GetRawResponseHeader() const { return m_strRawHeader; }
  DWORD GetRawResponseHeaderLength() const { return (DWORD)m_strRawHeader.GetLength(); }
  bool GetRawResponseHeader(LPBYTE szBuffer, DWORD* pdwLen) const {
    if (pdwLen == NULL) return false;
    DWORD dwNeed = (DWORD)m_strRawHeader.GetLength() + 1;
    if (szBuffer == NULL || *pdwLen < dwNeed) {
      *pdwLen = dwNeed;
      return false;
    }
    memcpy(szBuffer, (LPCSTR)m_strRawHeader, dwNeed);
    *pdwLen = dwNeed - 1;
    return true;
  }
  // A response header (names compare without case; repeats are joined
  // with commas).
  bool GetHeaderValue(LPCTSTR szName, CString& strValue) const {
    CStringA strVal;
    if (szName == NULL || !_FindHeader(CStringA(szName), strVal)) return false;
    strValue = CString(strVal);
    return true;
  }
  bool GetHeaderValue(LPCSTR szName, LPTSTR szBuffer, DWORD* pdwLen) const {
    CStringA strVal;
    if (szName == NULL || pdwLen == NULL || !_FindHeader(CStringA(szName), strVal)) return false;
    CString str(strVal);
    DWORD dwNeed = (DWORD)str.GetLength() + 1;
    if (szBuffer == NULL || *pdwLen < dwNeed) {
      *pdwLen = dwNeed;
      return false;
    }
    memcpy(szBuffer, (LPCTSTR)str, dwNeed * sizeof(TCHAR));
    *pdwLen = dwNeed - 1;
    return true;
  }
  long GetContentLength() const {
    CStringA str;
    return _FindHeader("Content-Length", str) ? atol(str) : -1;
  }
  const CUrl* GetCurrentUrl() const { return &m_urlCurrent; }

  bool SetProxy(LPCTSTR szProxy = NULL, short nProxyPort = 0) {
    if (szProxy == NULL) {
      // The proxy the environment names, as WinHTTP's tools set it.
      TCHAR sz[512];
      DWORD dw = ::GetEnvironmentVariable(_T("http_proxy"), sz, 512);
      if (dw == 0 || dw >= 512) return false;
      CUrl url;
      if (!url.CrackUrl(sz)) return false;
      m_strProxy = url.GetHostName();
      m_nProxyPort = url.GetPortNumber();
    } else {
      m_strProxy = szProxy;
      m_nProxyPort = nProxyPort != 0 ? nProxyPort : 80;
    }
    Close();
    return true;
  }
  void RemoveProxy() {
    m_strProxy.Empty();
    m_nProxyPort = 0;
    Close();
  }
  LPCTSTR GetProxy() const { return m_strProxy.IsEmpty() ? NULL : (LPCTSTR)m_strProxy; }

  void Close() {
    TSocketClass::Close();
    m_strConnHost.Empty();
    m_nConnPort = 0;
  }
  void ResetConnection() { Close(); }

  // Authentication schemes (Basic, or the caller's own), by name.
  bool AddAuthObj(LPCTSTR szScheme, CAtlBaseAuthObject* pObject, IAuthInfo* pInfo = NULL) {
    if (szScheme == NULL || pObject == NULL) return false;
    pObject->Init(_AtlHttpAsClient(this), pInfo);
    CString strKey(szScheme);
    strKey.MakeLower();
    m_authObjs.SetAt(strKey, pObject);
    return true;
  }
  const CAtlBaseAuthObject* FindAuthObject(LPCTSTR szScheme) {
    CAtlBaseAuthObject* p = NULL;
    CString strKey(szScheme);
    strKey.MakeLower();
    return m_authObjs.Lookup(strKey, p) ? p : NULL;
  }
  bool RemoveAuthObj(LPCTSTR szScheme) {
    CString strKey(szScheme);
    strKey.MakeLower();
    return m_authObjs.RemoveKey(strKey) != FALSE;
  }
  void RemoveAllAuthObjs() { m_authObjs.RemoveAll(); }
  void SetSilentLogonOk(bool bSet) { m_bSilentLogonOk = bSet; }
  bool GetSilentLogonOk() const { return m_bSilentLogonOk; }

  // The last response asked for authentication (a 401, or a 407 from the
  // proxy): tries each scheme it offers that has an object.
  bool NegotiateAuth(bool bProxy) {
    CStringA strAll;
    if (!_FindHeader(bProxy ? "Proxy-Authenticate" : "WWW-Authenticate", strAll)) return false;
    // Schemes are the first word of each challenge.
    CAtlArray<CStringA> schemes;
    int i = 0;
    while (i < strAll.GetLength()) {
      int nComma = _NextChallenge(strAll, i);
      CStringA strOne = strAll.Mid(i, nComma - i);
      strOne.Trim();
      int nSp = strOne.Find(' ');
      if (!strOne.IsEmpty()) schemes.Add(nSp >= 0 ? strOne.Left(nSp) : strOne);
      i = nComma + 1;
    }
    for (size_t n = 0; n < schemes.GetCount(); n++) {
      CString strKey(schemes[n]);
      strKey.MakeLower();
      CAtlBaseAuthObject* p = NULL;
      if (!m_authObjs.Lookup(strKey, p) || p == NULL || p->m_bFailed) continue;
      if (p->Authenticate(CString(strAll), bProxy)) return true;
      p->m_bFailed = true;
    }
    return false;
  }

  // For authentication objects: the challenge text of a scheme, and the
  // current request sent again with an authorization header.
  bool _GetChallenge(LPCSTR szScheme, bool bProxy, CStringA& strChallenge) const {
    CStringA strAll;
    if (!_FindHeader(bProxy ? "Proxy-Authenticate" : "WWW-Authenticate", strAll)) return false;
    int i = 0;
    while (i < strAll.GetLength()) {
      int nComma = _NextChallenge(strAll, i);
      CStringA strOne = strAll.Mid(i, nComma - i);
      strOne.Trim();
      int nLen = (int)strlen(szScheme);
      if (strOne.GetLength() >= nLen && strOne.Left(nLen).CompareNoCase(szScheme) == 0 &&
          (strOne.GetLength() == nLen || strOne[nLen] == ' ')) {
        strChallenge = strOne.Mid(nLen).Trim();
        return true;
      }
      i = nComma + 1;
    }
    return false;
  }
  bool _ResendWithAuth(LPCSTR szHeaderValue, bool bProxy) {
    if (m_nAuthDepth > 2) return false;
    if (bProxy)
      m_strProxyAuthHeader = szHeaderValue;
    else
      m_strAuthHeader = szHeaderValue;
    m_nAuthDepth++;
    bool b = _Request(m_bChunkedRequest);
    if (b) b = _ProcessResult(m_bChunkedRequest);
    m_nAuthDepth--;
    if (b && m_nStatus != (bProxy ? 407 : 401)) return true;
    return false;
  }

protected:
  CAtlArray<BYTE> m_response;
  size_t m_nBodyOffset;
  CStringA m_strRawHeader;
  int m_nStatus;
  CString m_strMethod;
  CUrl m_urlCurrent;
  CString m_strConnHost;
  ATL_URL_PORT m_nConnPort;
  CString m_strProxy;
  ATL_URL_PORT m_nProxyPort;
  bool m_bSilentLogonOk;
  CAtlMap<CString, CAtlBaseAuthObject*, CStringElementTraitsI<CString> > m_authObjs;
  ATL_NAVIGATE_DATA* m_pNavData;
  CAtlNavigateData m_navCurrent;
  CStringA m_strAuthHeader, m_strProxyAuthHeader;
  bool m_bProxyAuth;
  int m_nAuthDepth;
  bool m_bChunkedRequest = false;
  bool m_bKeepAlive = false;

  // The end of a challenge that starts at i: a comma not inside quotes and
  // followed by a scheme name (a word, then a space or the end).
  static int _NextChallenge(const CStringA& s, int i) {
    bool bQuote = false;
    for (int j = i; j < s.GetLength(); j++) {
      char ch = s[j];
      if (ch == '"') bQuote = !bQuote;
      if (ch != ',' || bQuote) continue;
      int k = j + 1;
      while (k < s.GetLength() && s[k] == ' ') k++;
      int nWord = k;
      while (k < s.GetLength() && (isalnum((unsigned char)s[k]) || s[k] == '-' || s[k] == '_')) k++;
      if (k > nWord && (k == s.GetLength() || s[k] == ' ')) return j;
    }
    return s.GetLength();
  }

  bool _FindHeader(const CStringA& strName, CStringA& strValue) const {
    bool bFound = false;
    strValue.Empty();
    int i = m_strRawHeader.Find('\n');  // after the status line
    while (i >= 0 && i + 1 < m_strRawHeader.GetLength()) {
      int nEnd = m_strRawHeader.Find('\n', i + 1);
      if (nEnd < 0) nEnd = m_strRawHeader.GetLength();
      CStringA strLine = m_strRawHeader.Mid(i + 1, nEnd - i - 1);
      strLine.TrimRight("\r");
      int nColon = strLine.Find(':');
      if (nColon > 0 && strLine.Left(nColon).Trim().CompareNoCase(strName) == 0) {
        CStringA strVal = strLine.Mid(nColon + 1).Trim();
        if (bFound) strValue += ", ";
        strValue += strVal;
        bFound = true;
      }
      i = nEnd < m_strRawHeader.GetLength() ? nEnd : -1;
    }
    return bFound;
  }

  bool _Navigate(const CUrl* pUrl, ATL_NAVIGATE_DATA* pNavData, bool bChunked) {
    if (pUrl == NULL) return false;
    if (pUrl->GetScheme() != ATL_URL_SCHEME_HTTP && !TSocketClass::SupportsScheme(pUrl->GetScheme()))
      return false;
    if (pNavData != NULL)
      m_navCurrent = *pNavData;
    else
      m_navCurrent = CAtlNavigateData();
    m_pNavData = &m_navCurrent;
    if (m_navCurrent.szMethod == NULL) m_navCurrent.szMethod = ATL_HTTP_METHOD_GET;
    m_strMethod = m_navCurrent.szMethod;
    m_urlCurrent = *pUrl;
    m_strAuthHeader.Empty();
    m_strProxyAuthHeader.Empty();
    m_bChunkedRequest = bChunked;
    typename CAtlMap<CString, CAtlBaseAuthObject*, CStringElementTraitsI<CString> >::CPair* pPair = NULL;
    for (POSITION pos = m_authObjs.GetStartPosition(); pos != NULL;) {
      pPair = m_authObjs.GetNext(pos);
      pPair->m_value->m_bFailed = false;
    }
    if (!_Request(bChunked)) return false;
    return _ProcessResult(bChunked);
  }

  bool _ProcessResult(bool bChunked) {
    if (!(m_navCurrent.dwFlags & ATL_HTTP_FLAG_PROCESS_RESULT)) return true;
    for (int nRedirects = 0; nRedirects < ATL_HTTP_MAX_REDIRECTS; nRedirects++) {
      int n = m_nStatus;
      if ((n == 401 || n == 407) && m_nAuthDepth == 0) {
        NegotiateAuth(n == 407);
        return true;
      }
      bool bRedirect = n == 301 || n == 302 || n == 303 || n == 307 || n == 308;
      if (!bRedirect || !(m_navCurrent.dwFlags & ATL_HTTP_FLAG_AUTO_REDIRECT)) return true;
      CStringA strLocation;
      if (!_FindHeader("Location", strLocation) || strLocation.IsEmpty()) return true;
      CUrl urlNew;
      if (!_ResolveUrl(CString(strLocation), urlNew)) return true;
      // A 303 (and a 301 or 302 to a POST, as browsers do) turns into a GET.
      if (n == 303 || ((n == 301 || n == 302) && m_strMethod.CompareNoCase(_T("POST")) == 0)) {
        m_navCurrent.szMethod = ATL_HTTP_METHOD_GET;
        m_strMethod = ATL_HTTP_METHOD_GET;
        m_navCurrent.pData = NULL;
        m_navCurrent.dwDataLen = 0;
        m_navCurrent.szDataType = NULL;
        bChunked = false;
      }
      m_urlCurrent = urlNew;
      m_strAuthHeader.Empty();
      if (!_Request(bChunked)) return false;
    }
    return true;
  }

  bool _ResolveUrl(const CString& strLocation, CUrl& url) {
    if (strLocation.Find(_T("://")) > 0) return url.CrackUrl(strLocation) != FALSE;
    url = m_urlCurrent;
    url.SetExtraInfo(_T(""));
    if (strLocation.Left(2) == _T("//")) {
      CString strScheme(m_urlCurrent.GetSchemeName());
      return url.CrackUrl(strScheme + _T(":") + strLocation) != FALSE;
    }
    CString strPath = strLocation, strExtra;
    int nQ = strPath.FindOneOf(_T("?#"));
    if (nQ >= 0) {
      strExtra = strPath.Mid(nQ);
      strPath = strPath.Left(nQ);
    }
    if (strPath.IsEmpty()) {
      strPath = m_urlCurrent.GetUrlPath();
    } else if (strPath[0] != _T('/')) {
      CString strBase(m_urlCurrent.GetUrlPath());
      int nSlash = strBase.ReverseFind(_T('/'));
      strPath = (nSlash >= 0 ? strBase.Left(nSlash + 1) : CString(_T("/"))) + strPath;
    }
    url.SetUrlPath(strPath);
    url.SetExtraInfo(strExtra);
    return true;
  }

  bool _Connect() {
    CString strHost = m_strProxy.IsEmpty() ? CString(m_urlCurrent.GetHostName()) : m_strProxy;
    ATL_URL_PORT nPort = m_strProxy.IsEmpty() ? m_urlCurrent.GetPortNumber() : m_nProxyPort;
    if (nPort == 0) nPort = ATL_URL_DEFAULT_HTTP_PORT;
    if (m_bKeepAlive && TSocketClass::GetSocket() != INVALID_SOCKET &&
        strHost.CompareNoCase(m_strConnHost) == 0 && nPort == m_nConnPort)
      return true;
    Close();
    // An IPv6 literal comes in brackets.
    if (strHost.GetLength() > 2 && strHost[0] == _T('[') && strHost[strHost.GetLength() - 1] == _T(']'))
      strHost = strHost.Mid(1, strHost.GetLength() - 2);
    TSocketClass::SetSocketTimeout(m_navCurrent.dwTimeout);
    if (!TSocketClass::Connect(strHost, nPort)) {
      Close();
      return false;
    }
    m_strConnHost = strHost;
    m_nConnPort = nPort;
    return true;
  }

  CStringA _RequestHead(bool bChunked) {
    CStringA strPath(m_urlCurrent.GetUrlPath());
    if (strPath.IsEmpty()) strPath = "/";
    CStringA strExtra(m_urlCurrent.GetExtraInfo());
    int nHash = strExtra.Find('#');
    if (nHash >= 0) strExtra = strExtra.Left(nHash);
    CStringA strHost(m_urlCurrent.GetHostName());
    ATL_URL_PORT nPort = m_urlCurrent.GetPortNumber();
    if (nPort != 0 && nPort != ATL_URL_DEFAULT_HTTP_PORT) {
      CStringA strPort;
      strPort.Format(":%u", (unsigned)nPort);
      strHost += strPort;
    }
    CStringA str(m_strMethod);
    str += " ";
    if (!m_strProxy.IsEmpty()) str += "http://" + strHost;
    str += strPath + strExtra + " HTTP/1.1\r\nHost: " + strHost + "\r\n";
    str += "User-Agent: " ATL_HTTP_USERAGENT "\r\nAccept: */*\r\n";
    if (m_navCurrent.szExtraHeaders != NULL && *m_navCurrent.szExtraHeaders != 0) {
      CStringA strExtraHdr(m_navCurrent.szExtraHeaders);
      str += strExtraHdr;
      if (strExtraHdr.Right(2) != "\r\n") str += "\r\n";
    }
    if (!m_strAuthHeader.IsEmpty()) str += "Authorization: " + m_strAuthHeader + "\r\n";
    if (!m_strProxyAuthHeader.IsEmpty())
      str += "Proxy-Authorization: " + m_strProxyAuthHeader + "\r\n";
    if (bChunked) {
      str += "Transfer-Encoding: chunked\r\n";
    } else if (m_navCurrent.pData != NULL || m_navCurrent.dwDataLen > 0 ||
               m_strMethod.CompareNoCase(_T("POST")) == 0 || m_strMethod.CompareNoCase(_T("PUT")) == 0) {
      CStringA strLen;
      strLen.Format("Content-Length: %lu\r\n", (unsigned long)m_navCurrent.dwDataLen);
      str += strLen;
    }
    if ((bChunked || m_navCurrent.dwDataLen > 0) && m_navCurrent.szDataType != NULL &&
        *m_navCurrent.szDataType != 0)
      str += "Content-Type: " + CStringA(m_navCurrent.szDataType) + "\r\n";
    str += "\r\n";
    return str;
  }

  bool _Write(const BYTE* p, DWORD dwLen) {
    DWORD dw = dwLen;
    return TSocketClass::Write(p, &dw) && dw == dwLen;
  }

  bool _SendBody(bool bChunked) {
    if (bChunked) {
      for (;;) {
        BYTE* pChunk = NULL;
        DWORD dwSize = 0;
        if (!m_navCurrent.pfnChunkCallback(&pChunk, &dwSize, m_navCurrent.m_lParamChunkCB)) return false;
        CStringA strSize;
        strSize.Format("%lx\r\n", (unsigned long)dwSize);
        if (!_Write((const BYTE*)(LPCSTR)strSize, (DWORD)strSize.GetLength())) return false;
        if (dwSize == 0) return _Write((const BYTE*)"\r\n", 2);
        if (!_Write(pChunk, dwSize) || !_Write((const BYTE*)"\r\n", 2)) return false;
      }
    }
    if (m_navCurrent.dwDataLen == 0 || m_navCurrent.pData == NULL) return true;
    bool bBlocks = (m_navCurrent.dwFlags & ATL_HTTP_FLAG_SEND_BLOCKS) != 0;
    DWORD dwBlock = bBlocks && m_navCurrent.dwSendBlockSize > 0 ? m_navCurrent.dwSendBlockSize
                                                                : m_navCurrent.dwDataLen;
    DWORD dwSent = 0;
    while (dwSent < m_navCurrent.dwDataLen) {
      DWORD dw = m_navCurrent.dwDataLen - dwSent;
      if (dw > dwBlock) dw = dwBlock;
      if (!_Write(m_navCurrent.pData + dwSent, dw)) return false;
      dwSent += dw;
      if ((m_navCurrent.dwFlags & ATL_HTTP_FLAG_SEND_CALLBACK) && m_navCurrent.pfnSendStatusCallback != NULL &&
          !m_navCurrent.pfnSendStatusCallback(dwSent, m_navCurrent.m_lParamSend))
        return false;
    }
    return true;
  }

  // One request and its response; a kept connection the server has since
  // closed is opened again once.
  bool _Request(bool bChunked) {
    for (int nTry = 0; nTry < 2; nTry++) {
      bool bReused = m_bKeepAlive && TSocketClass::GetSocket() != INVALID_SOCKET;
      if (!_Connect()) return false;
      CStringA strHead = _RequestHead(bChunked);
      bool bSent = _Write((const BYTE*)(LPCSTR)strHead, (DWORD)strHead.GetLength()) && _SendBody(bChunked);
      int nResult = bSent ? _ReadResponse() : -1;
      if (nResult == 1) return true;
      Close();
      if (!(bReused && nResult != 0)) return false;
      // nResult -1: nothing came back on a reused connection.
      m_bKeepAlive = false;
    }
    return false;
  }

  // 1 for a response, -1 for nothing read at all, 0 for a broken one.
  int _ReadResponse() {
    m_response.RemoveAll();
    m_strRawHeader.Empty();
    m_nBodyOffset = 0;
    m_nStatus = ATL_INVALID_STATUS;
    CAtlArray<BYTE> buf;
    DWORD dwBlock = m_navCurrent.dwReadBlockSize > 0 ? m_navCurrent.dwReadBlockSize : ATL_HTTP_DEFAULT_BLOCK_SIZE;
    DWORD dwTotalRead = 0;
    size_t nHeaderEnd = 0;
    // The header, skipping any 1xx responses before it.
    for (;;) {
      nHeaderEnd = _FindHeaderEnd(buf);
      if (nHeaderEnd != 0) {
        CStringA strHeader((LPCSTR)buf.GetData(), (int)nHeaderEnd);
        int nStatus = _ParseStatus(strHeader);
        if (nStatus >= 100 && nStatus < 200) {
          _Consume(buf, nHeaderEnd);
          continue;
        }
        m_strRawHeader = strHeader;
        m_nStatus = nStatus;
        break;
      }
      int n = _ReadMore(buf, dwBlock, dwTotalRead);
      if (n <= 0) return buf.IsEmpty() && dwTotalRead == 0 ? -1 : 0;
    }
    if (m_nStatus == ATL_INVALID_STATUS) return 0;
    // The response: the header, then the body as it is decoded.
    m_response.SetCount(nHeaderEnd);
    memcpy(m_response.GetData(), buf.GetData(), nHeaderEnd);
    m_nBodyOffset = nHeaderEnd;
    _Consume(buf, nHeaderEnd);

    CStringA strConn, strTE, strLen;
    bool bHttp11 = m_strRawHeader.Left(8).CompareNoCase("HTTP/1.1") == 0;
    m_bKeepAlive = bHttp11;
    if (_FindHeader("Connection", strConn)) {
      if (strConn.Find("close") >= 0 || strConn.Find("Close") >= 0) m_bKeepAlive = false;
      if (strConn.CompareNoCase("keep-alive") == 0) m_bKeepAlive = true;
    }
    bool bNoBody = m_strMethod.CompareNoCase(_T("HEAD")) == 0 || m_nStatus == 204 || m_nStatus == 304;
    if (bNoBody) return 1;
    if (_FindHeader("Transfer-Encoding", strTE) && strTE.MakeLower().Find("chunked") >= 0)
      return _ReadChunked(buf, dwBlock, dwTotalRead) ? 1 : 0;
    if (_FindHeader("Content-Length", strLen)) {
      size_t nLen = (size_t)strtoul(strLen, NULL, 10);
      while (buf.GetCount() < nLen)
        if (_ReadMore(buf, dwBlock, dwTotalRead) <= 0) return 0;
      _AppendBody(buf.GetData(), nLen);
      return 1;
    }
    // Up to the close.
    m_bKeepAlive = false;
    for (;;) {
      int n = _ReadMore(buf, dwBlock, dwTotalRead);
      if (n < 0) return 0;
      if (n == 0) break;
    }
    _AppendBody(buf.GetData(), buf.GetCount());
    return 1;
  }

  bool _ReadChunked(CAtlArray<BYTE>& buf, DWORD dwBlock, DWORD& dwTotalRead) {
    for (;;) {
      size_t nEol;
      while ((nEol = _FindCRLF(buf, 0)) == (size_t)-1)
        if (_ReadMore(buf, dwBlock, dwTotalRead) <= 0) return false;
      CStringA strSize((LPCSTR)buf.GetData(), (int)nEol);
      size_t nSize = (size_t)strtoul(strSize, NULL, 16);
      _Consume(buf, nEol + 2);
      if (nSize == 0) {
        // Trailers, to the blank line.
        for (;;) {
          while ((nEol = _FindCRLF(buf, 0)) == (size_t)-1)
            if (_ReadMore(buf, dwBlock, dwTotalRead) <= 0) return true;
          _Consume(buf, nEol + 2);
          if (nEol == 0) return true;
        }
      }
      while (buf.GetCount() < nSize + 2)
        if (_ReadMore(buf, dwBlock, dwTotalRead) <= 0) return false;
      _AppendBody(buf.GetData(), nSize);
      _Consume(buf, nSize + 2);
    }
  }

  // Bytes read onto buf: their count, 0 at the close, -1 on an error.
  int _ReadMore(CAtlArray<BYTE>& buf, DWORD dwBlock, DWORD& dwTotalRead) {
    size_t nOld = buf.GetCount();
    if (!buf.SetCount(nOld + dwBlock)) return -1;
    DWORD dw = dwBlock;
    if (!TSocketClass::Read(buf.GetData() + nOld, &dw)) {
      buf.SetCount(nOld);
      return -1;
    }
    buf.SetCount(nOld + dw);
    dwTotalRead += dw;
    if (dw > 0 && m_navCurrent.pfnReadStatusCallback != NULL &&
        !m_navCurrent.pfnReadStatusCallback(dwTotalRead, m_navCurrent.m_lParamRead))
      return -1;
    return (int)dw;
  }
  void _AppendBody(const BYTE* p, size_t n) {
    size_t nOld = m_response.GetCount();
    m_response.SetCount(nOld + n);
    if (n > 0) memcpy(m_response.GetData() + nOld, p, n);
  }
  static void _Consume(CAtlArray<BYTE>& buf, size_t n) {
    if (n >= buf.GetCount()) {
      buf.RemoveAll();
      return;
    }
    memmove(buf.GetData(), buf.GetData() + n, buf.GetCount() - n);
    buf.SetCount(buf.GetCount() - n);
  }
  static size_t _FindCRLF(const CAtlArray<BYTE>& buf, size_t nFrom) {
    for (size_t i = nFrom; i + 1 < buf.GetCount(); i++)
      if (buf[i] == '\r' && buf[i + 1] == '\n') return i;
    return (size_t)-1;
  }
  // Where the header ends, after its blank line; 0 if it has not yet.
  static size_t _FindHeaderEnd(const CAtlArray<BYTE>& buf) {
    for (size_t i = 0; i + 3 < buf.GetCount(); i++)
      if (buf[i] == '\r' && buf[i + 1] == '\n' && buf[i + 2] == '\r' && buf[i + 3] == '\n') return i + 4;
    return 0;
  }
  static int _ParseStatus(const CStringA& strHeader) {
    if (strHeader.Left(5).CompareNoCase("HTTP/") != 0) return ATL_INVALID_STATUS;
    int nSp = strHeader.Find(' ');
    if (nSp < 0) return ATL_INVALID_STATUS;
    int n = atoi((LPCSTR)strHeader + nSp + 1);
    return n >= 100 && n < 1000 ? n : ATL_INVALID_STATUS;
  }

private:
  CAtlHttpClientT(const CAtlHttpClientT&);
  CAtlHttpClientT& operator=(const CAtlHttpClientT&);
};

// ---------------------------------------------------------------------
// Basic authentication (RFC 7617): the name and password from IAuthInfo,
// sent once the server asks.

class CBasicAuthObject : public CAtlBaseAuthObject {
public:
  CBasicAuthObject() : m_pClient(NULL), m_pAuthInfo(NULL) {}
  explicit CBasicAuthObject(IAuthInfo* pAuthInfo) : m_pClient(NULL), m_pAuthInfo(pAuthInfo) {}

  void SetAuthInfo(IAuthInfo* pAuthInfo) { m_pAuthInfo = pAuthInfo; }
  void Init(CAtlHttpClient* pSocket, IAuthInfo* pAuthInfo = NULL) override {
    m_pClient = pSocket;
    if (pAuthInfo != NULL) m_pAuthInfo = pAuthInfo;
  }
  bool Authenticate(LPCTSTR /*szAuthTypes*/, bool bProxy) override {
    if (m_pClient == NULL) return false;
    CStringA strChallenge;
    if (m_pClient->_GetChallenge("Basic", bProxy, strChallenge)) {
      int nRealm = strChallenge.Find("realm=");
      if (nRealm >= 0) {
        CStringA strRealm = strChallenge.Mid(nRealm + 6);
        strRealm.Trim();
        if (strRealm.GetLength() > 0 && strRealm[0] == '"') {
          int nEnd = strRealm.Find('"', 1);
          strRealm = strRealm.Mid(1, nEnd > 0 ? nEnd - 1 : strRealm.GetLength() - 1);
        }
        m_strRealm = CString(strRealm);
      }
    }
    return DoBasicAuthenticate(bProxy);
  }
  LPCTSTR GetRealm() const { return m_strRealm; }

  bool DoBasicAuthenticate(bool bProxy) {
    CString strUser, strPassword;
    if (!_AtlGetAuthInfo(m_pAuthInfo, &IAuthInfo::GetUsername, strUser)) return false;
    if (!_AtlGetAuthInfo(m_pAuthInfo, &IAuthInfo::GetPassword, strPassword)) return false;
    CStringA strCred(strUser + _T(":") + strPassword);
    int nLen = Base64EncodeGetRequiredLength(strCred.GetLength(), ATL_BASE64_FLAG_NOCRLF);
    CStringA strB64;
    Base64Encode((const BYTE*)(LPCSTR)strCred, strCred.GetLength(), strB64.GetBuffer(nLen + 1), &nLen,
                 ATL_BASE64_FLAG_NOCRLF);
    strB64.ReleaseBuffer(nLen);
    return m_pClient->_ResendWithAuth("Basic " + strB64, bProxy);
  }

protected:
  CAtlHttpClient* m_pClient;
  IAuthInfo* m_pAuthInfo;
  CString m_strRealm;
};

}  // namespace ATL

#endif  // __ATLHTTP_H__
