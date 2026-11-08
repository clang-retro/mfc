// isapi.cpp - CHtmlStream, CHttpServerContext, CHttpServer and its parse
// maps, CHttpFilterContext and CHttpFilter.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include "afximpl.h"
#include <afxisapi.h>
#include <stdio.h>
#include <stdlib.h>

// ---------------------------------------------------------------------
// CHtmlStream

CHtmlStream::CHtmlStream(UINT nGrowBytes)
    : m_nGrowBytes(nGrowBytes),
      m_nPosition(0),
      m_nBufferSize(0),
      m_nStreamSize(0),
      m_lpBuffer(NULL),
      m_bAutoDelete(TRUE) {}

CHtmlStream::CHtmlStream(BYTE* lpBuffer, UINT nBufferSize, UINT nGrowBytes)
    : m_nGrowBytes(0),
      m_nPosition(0),
      m_nBufferSize(0),
      m_nStreamSize(0),
      m_lpBuffer(NULL),
      m_bAutoDelete(FALSE) {
  Attach(lpBuffer, nBufferSize, nGrowBytes);
}

CHtmlStream::~CHtmlStream() {
  if (m_lpBuffer != NULL) Close();
}

// The caller's buffer; it grows (and becomes the stream's) only with
// nGrowBytes.
void CHtmlStream::Attach(BYTE* lpBuffer, UINT nBufferSize, UINT nGrowBytes) {
  if (m_lpBuffer != NULL && m_bAutoDelete) Free(m_lpBuffer);
  m_lpBuffer = lpBuffer;
  m_nBufferSize = nBufferSize;
  m_nGrowBytes = nGrowBytes;
  m_nPosition = 0;
  m_nStreamSize = 0;
  m_bAutoDelete = FALSE;
}

BYTE* CHtmlStream::Detach() {
  BYTE* lpBuffer = m_lpBuffer;
  m_lpBuffer = NULL;
  m_nBufferSize = 0;
  m_nStreamSize = 0;
  m_nPosition = 0;
  return lpBuffer;
}

void CHtmlStream::Abort() { Close(); }

void CHtmlStream::Close() {
  if (m_lpBuffer != NULL && m_bAutoDelete) Free(m_lpBuffer);
  m_lpBuffer = NULL;
  m_nBufferSize = 0;
  m_nStreamSize = 0;
  m_nPosition = 0;
}

void CHtmlStream::InitStream() {}

void CHtmlStream::Reset() {
  m_nPosition = 0;
  m_nStreamSize = 0;
}

BYTE* CHtmlStream::Alloc(DWORD nBytes) { return (BYTE*)malloc(nBytes); }

BYTE* CHtmlStream::Realloc(BYTE* lpMem, DWORD nBytes) { return (BYTE*)realloc(lpMem, nBytes); }

BYTE* CHtmlStream::Memcpy(BYTE* lpMemTarget, const BYTE* lpMemSource, UINT nBytes) {
  return (BYTE*)memcpy(lpMemTarget, lpMemSource, nBytes);
}

void CHtmlStream::Free(BYTE* lpMem) { free(lpMem); }

void CHtmlStream::GrowStream(DWORD dwNewLen) {
  if (dwNewLen <= m_nBufferSize) return;
  if (m_nGrowBytes == 0) AfxThrowMemoryException();
  DWORD dwNewSize = m_nBufferSize;
  while (dwNewSize < dwNewLen) dwNewSize += m_nGrowBytes;
  BYTE* lpNew;
  if (m_lpBuffer == NULL) {
    lpNew = Alloc(dwNewSize);
  } else if (m_bAutoDelete) {
    lpNew = Realloc(m_lpBuffer, dwNewSize);
  } else {
    // An attached buffer stays the caller's: the stream copies it.
    lpNew = Alloc(dwNewSize);
    if (lpNew != NULL) Memcpy(lpNew, m_lpBuffer, m_nStreamSize);
  }
  if (lpNew == NULL) AfxThrowMemoryException();
  m_lpBuffer = lpNew;
  m_nBufferSize = dwNewSize;
  m_bAutoDelete = TRUE;
}

void CHtmlStream::Write(const void* lpBuf, UINT nCount) {
  if (nCount == 0) return;
  if (m_nPosition + nCount > m_nBufferSize) GrowStream(m_nPosition + nCount);
  Memcpy(m_lpBuffer + m_nPosition, (const BYTE*)lpBuf, nCount);
  m_nPosition += nCount;
  if (m_nPosition > m_nStreamSize) m_nStreamSize = m_nPosition;
}

CHtmlStream& CHtmlStream::operator<<(LPCSTR psz) {
  if (psz != NULL) Write(psz, (UINT)strlen(psz));
  return *this;
}

// Wide text goes out in the ANSI code page, as the server sends bytes.
CHtmlStream& CHtmlStream::operator<<(LPCWSTR psz) {
  if (psz != NULL) {
    CStringA str(psz);
    Write((LPCSTR)str, (UINT)str.GetLength());
  }
  return *this;
}

CHtmlStream& CHtmlStream::operator<<(short w) { return *this << (long)w; }

CHtmlStream& CHtmlStream::operator<<(int n) { return *this << (long)n; }

CHtmlStream& CHtmlStream::operator<<(long dw) {
  char sz[16];
  sprintf(sz, "%ld", dw);
  return *this << (LPCSTR)sz;
}

CHtmlStream& CHtmlStream::operator<<(DWORD dw) {
  char sz[16];
  sprintf(sz, "%lu", (unsigned long)dw);
  return *this << (LPCSTR)sz;
}

CHtmlStream& CHtmlStream::operator<<(float f) { return *this << (double)f; }

CHtmlStream& CHtmlStream::operator<<(double d) {
  char sz[64];
  sprintf(sz, "%g", d);
  return *this << (LPCSTR)sz;
}

CHtmlStream& CHtmlStream::operator<<(const CHtmlStream& stream) {
  Write(stream.m_lpBuffer, stream.m_nStreamSize);
  return *this;
}

CHtmlStream& CHtmlStream::operator<<(BYTE b) {
  Write(&b, 1);
  return *this;
}

// ---------------------------------------------------------------------
// CHttpServerContext

CHttpServerContext::CHttpServerContext(EXTENSION_CONTROL_BLOCK* pECB)
    : m_pECB(pECB), m_pStream(NULL), m_dwStatusCode(200), m_pbRawData(NULL),
      m_dwRawSize(0) {}

CHttpServerContext::~CHttpServerContext() {
  delete m_pStream;
  delete[] m_pbRawData;
}

void CHttpServerContext::Reset() {
  if (m_pStream != NULL) m_pStream->Reset();
  m_strHeaders.Empty();
}

BOOL CHttpServerContext::GetServerVariable(LPTSTR lpszVariableName, LPVOID lpvBuffer,
                                           LPDWORD lpdwSize) {
  CStringA strName(lpszVariableName);
#ifdef _UNICODE
  // The server's text is bytes; the caller gets it as wide characters.
  DWORD cb = 0;
  m_pECB->GetServerVariable(m_pECB->ConnID, strName.GetBuffer(), NULL, &cb);
  if (cb == 0) cb = 1;
  CStringA strValue;
  BOOL bOK = m_pECB->GetServerVariable(m_pECB->ConnID, strName.GetBuffer(),
                                       strValue.GetBuffer((int)cb), &cb);
  strValue.ReleaseBuffer();
  if (!bOK) return FALSE;
  CStringW strW(strValue);
  DWORD cbNeed = (DWORD)(strW.GetLength() + 1) * sizeof(WCHAR);
  if (lpvBuffer == NULL || *lpdwSize < cbNeed) {
    *lpdwSize = cbNeed;
    ::SetLastError(ERROR_INSUFFICIENT_BUFFER);
    return FALSE;
  }
  memcpy(lpvBuffer, (LPCWSTR)strW, cbNeed);
  *lpdwSize = cbNeed;
  return TRUE;
#else
  return m_pECB->GetServerVariable(m_pECB->ConnID, strName.GetBuffer(), lpvBuffer, lpdwSize);
#endif
}

BOOL CHttpServerContext::WriteClient(LPVOID lpvBuffer, LPDWORD lpdwBytes, DWORD dwReserved) {
  return m_pECB->WriteClient(m_pECB->ConnID, lpvBuffer, lpdwBytes, dwReserved);
}

BOOL CHttpServerContext::ReadClient(LPVOID lpvBuffer, LPDWORD lpdwSize) {
  return m_pECB->ReadClient(m_pECB->ConnID, lpvBuffer, lpdwSize);
}

BOOL CHttpServerContext::ServerSupportFunction(DWORD dwHSERRequest, LPVOID lpvBuffer,
                                               LPDWORD lpdwSize, LPDWORD lpdwDataType) {
  return m_pECB->ServerSupportFunction(m_pECB->ConnID, dwHSERRequest, lpvBuffer, lpdwSize,
                                       lpdwDataType);
}

BOOL CHttpServerContext::TransmitFile(HANDLE hFile, DWORD dwFlags, LPVOID pstrHeader,
                                      DWORD dwHeaderLen, LPVOID pstrTrailer, DWORD dwTrailerLen) {
  HSE_TF_INFO info;
  memset(&info, 0, sizeof(info));
  info.hFile = hFile;
  info.dwFlags = dwFlags | HSE_IO_SYNC;
  info.pHead = pstrHeader;
  info.HeadLength = dwHeaderLen;
  info.pTail = pstrTrailer;
  info.TailLength = dwTrailerLen;
  return ServerSupportFunction(HSE_REQ_TRANSMIT_FILE, &info, NULL, NULL);
}

HANDLE CHttpServerContext::GetImpersonationToken() const {
  HANDLE hToken = NULL;
  DWORD cb = sizeof(hToken);
  if (!m_pECB->ServerSupportFunction(m_pECB->ConnID, HSE_REQ_GET_IMPERSONATION_TOKEN, &hToken,
                                     &cb, NULL))
    return NULL;
  return hToken;
}

DWORD CHttpServerContext::SetStatusCode(DWORD nStatusCode) {
  DWORD dwOld = m_dwStatusCode;
  m_dwStatusCode = nStatusCode;
  return dwOld;
}

#define CTXT_OUT(T)                                              \
  CHttpServerContext& CHttpServerContext::operator<<(T value) {  \
    if (m_pStream != NULL) *m_pStream << value;                   \
    return *this;                                                 \
  }
CTXT_OUT(LPCSTR)
CTXT_OUT(LPCWSTR)
CTXT_OUT(short)
CTXT_OUT(int)
CTXT_OUT(long)
CTXT_OUT(DWORD)
CTXT_OUT(float)
CTXT_OUT(double)
CTXT_OUT(const CHtmlStream&)
CTXT_OUT(BYTE)
#undef CTXT_OUT

// ---------------------------------------------------------------------
// Parse maps

long AFXAPI _AfxParseLong(LPCTSTR psz) { return psz != NULL ? _tcstol(psz, NULL, 0) : 0; }

LONGLONG AFXAPI _AfxParseLongLong(LPCTSTR psz) {
  return psz != NULL ? _tcstoi64(psz, NULL, 0) : 0;
}

double AFXAPI _AfxParseDouble(LPCTSTR psz) { return psz != NULL ? _tcstod(psz, NULL) : 0; }

const AFX_PARSEMAP CHttpServer::parseMap = {&CHttpServer::_GetBaseParseMap,
                                           &CHttpServer::_parseEntries[0]};
const AFX_PARSEMAP_ENTRY CHttpServer::_parseEntries[] = {
    {NULL, NULL, NULL, NULL, NULL, _AFX_PARSE_END}};

const AFX_PARSEMAP* CHttpServer::GetParseMap() const { return &CHttpServer::parseMap; }

// The command's entry, and the parameter list that follows it, in this
// class's map or a base's. A NULL name looks for the default command.
const AFX_PARSEMAP_ENTRY* CHttpServer::LookUp(LPCTSTR szMethod, const AFX_PARSEMAP*& pMap,
                                              const AFX_PARSEMAP_ENTRY*& pParams,
                                              AFX_PISAPICMD) {
  pParams = NULL;
  CString strDefault;
  if (szMethod == NULL) {
    for (pMap = GetParseMap(); pMap != NULL && strDefault.IsEmpty();
         pMap = pMap->pfnGetBaseMap != NULL ? pMap->pfnGetBaseMap() : NULL)
      for (const AFX_PARSEMAP_ENTRY* p = pMap->lpEntries; p->nKind != _AFX_PARSE_END; p++)
        if (p->nKind == _AFX_PARSE_DEFAULT) {
          strDefault = p->pszFnName;
          break;
        }
    if (strDefault.IsEmpty()) return NULL;
    szMethod = strDefault;
  }
  for (pMap = GetParseMap(); pMap != NULL;
       pMap = pMap->pfnGetBaseMap != NULL ? pMap->pfnGetBaseMap() : NULL) {
    for (const AFX_PARSEMAP_ENTRY* p = pMap->lpEntries; p->nKind != _AFX_PARSE_END; p++) {
      if (p->nKind != _AFX_PARSE_COMMAND || _tcsicmp(p->pszFnName, szMethod) != 0) continue;
      if (p[1].nKind == _AFX_PARSE_PARAMS) pParams = &p[1];
      return p;
    }
  }
  return NULL;
}

// %XX and + in an argument.
static void UrlDecode(CString& str) {
  CStringA strA(str);
  CStringA strOut;
  for (int i = 0; i < strA.GetLength(); i++) {
    char ch = strA[i];
    if (ch == '+') {
      ch = ' ';
    } else if (ch == '%' && i + 2 < strA.GetLength() && isxdigit((BYTE)strA[i + 1]) &&
               isxdigit((BYTE)strA[i + 2])) {
      char hex[3] = {strA[i + 1], strA[i + 2], 0};
      ch = (char)strtol(hex, NULL, 16);
      i += 2;
    }
    strOut += ch;
  }
  str = CString(strOut);
}

// The number of a command's arguments: ITS_RAW is the data and its size.
static UINT CountArgs(LPCSTR pszArgs) {
  UINT n = 0;
  for (LPCSTR p = pszArgs; p != NULL && *p != 0; p++) {
    if (*p == ITS_EMPTY[0]) continue;
    n += (*p == ITS_RAW[0]) ? 2 : 1;
  }
  return n;
}

// "name name2=default name3='a default with spaces'": the names, and the
// defaults (NULL where there is none, so the argument is required).
static int ParseParamInfo(LPCTSTR pszInfo, CStringArray& names, CStringArray& defaults,
                          CByteArray& hasDefault) {
  LPCTSTR p = pszInfo;
  while (*p != 0) {
    while (*p == ' ' || *p == '\t') p++;
    if (*p == 0) break;
    CString strName;
    while (*p != 0 && *p != ' ' && *p != '\t' && *p != '=') strName += *p++;
    CString strDefault;
    BOOL bDefault = FALSE;
    if (*p == '=') {
      bDefault = TRUE;
      p++;
      if (*p == '\'' || *p == '"') {
        TCHAR q = *p++;
        while (*p != 0 && *p != q) strDefault += *p++;
        if (*p != q) return CHttpServer::callMissingQuote;
        p++;
      } else {
        while (*p != 0 && *p != ' ' && *p != '\t') strDefault += *p++;
      }
    }
    names.Add(strName);
    defaults.Add(strDefault);
    hasDefault.Add((BYTE)bDefault);
  }
  return CHttpServer::callOK;
}

int CHttpServer::CallMemberFunc(CHttpServerContext* pCtxt, const AFX_PARSEMAP_ENTRY* pEntry,
                                const AFX_PARSEMAP_ENTRY* pParams, LPTSTR szParams) {
  BOOL bRaw = pEntry->pszArgs != NULL && strchr(pEntry->pszArgs, ITS_RAW[0]) != NULL;
  UINT nArgs = CountArgs(pEntry->pszArgs);
  CStringArray values;
  values.SetSize(nArgs);
  CString strRawSize;
  AFX_PARSEARG* pArgs = new AFX_PARSEARG[nArgs ? nArgs : 1];
  memset(pArgs, 0, sizeof(AFX_PARSEARG) * (nArgs ? nArgs : 1));
  int nResult = callOK;

  if (bRaw) {
    // The request's data, and its size.
    if (nArgs >= 2) {
      pArgs[0].pvRaw = pCtxt->m_pbRawData;
      strRawSize.Format(_T("%lu"), (unsigned long)pCtxt->m_dwRawSize);
      pArgs[1].psz = strRawSize.GetBuffer();
    }
  } else {
    // The arguments, split at the delimiter.
    CStringArray tokens;
    if (szParams != NULL) {
      CString str(szParams);
      int nStart = 0;
      while (nStart <= str.GetLength() && !str.IsEmpty()) {
        int nEnd = str.Find(m_cTokenDelimiter, nStart);
        if (nEnd < 0) nEnd = str.GetLength();
        CString strToken = str.Mid(nStart, nEnd - nStart);
        if (!strToken.IsEmpty()) tokens.Add(strToken);
        nStart = nEnd + 1;
      }
    }
    if (pParams != NULL && pParams->pszParamInfo != NULL) {
      CStringArray names, defaults;
      CByteArray hasDefault;
      nResult = ParseParamInfo(pParams->pszParamInfo, names, defaults, hasDefault);
      if (nResult == callOK && (UINT)names.GetSize() != nArgs) nResult = callBadParamCount;
      CByteArray filled;
      filled.SetSize(nArgs);
      for (INT_PTR i = 0; nResult == callOK && i < tokens.GetSize(); i++) {
        CString strName, strValue = tokens[i];
        int nEq = strValue.Find('=');
        INT_PTR nSlot = -1;
        if (nEq >= 0) {
          strName = strValue.Left(nEq);
          UrlDecode(strName);
          strValue = strValue.Mid(nEq + 1);
          for (INT_PTR j = 0; j < names.GetSize(); j++)
            if (names[j].CompareNoCase(strName) == 0) nSlot = j;
          if (nSlot < 0) nResult = callBadParam;
        } else {
          for (INT_PTR j = 0; j < (INT_PTR)nArgs && nSlot < 0; j++)
            if (!filled[j]) nSlot = j;
          if (nSlot < 0) nResult = callBadParamCount;
        }
        if (nResult != callOK) break;
        UrlDecode(strValue);
        values[nSlot] = strValue;
        filled[nSlot] = 1;
      }
      for (UINT j = 0; nResult == callOK && j < nArgs; j++) {
        if (filled[j]) continue;
        if (!hasDefault[j]) {
          nResult = callParamRequired;
          break;
        }
        values[j] = defaults[j];
      }
    } else {
      if ((UINT)tokens.GetSize() != nArgs) nResult = nArgs > (UINT)tokens.GetSize()
                                                          ? callMissingParams
                                                          : callBadParamCount;
      for (UINT j = 0; nResult == callOK && j < nArgs; j++) {
        CString strValue = tokens[j];
        int nEq = strValue.Find('=');
        if (nEq >= 0) strValue = strValue.Mid(nEq + 1);
        UrlDecode(strValue);
        values[j] = strValue;
      }
    }
    for (UINT j = 0; nResult == callOK && j < nArgs; j++) pArgs[j].psz = values[j].GetBuffer();
  }

  if (nResult == callOK && pEntry->pfnInvoke != NULL &&
      pEntry->pfnInvoke(this, pEntry->pfn, pCtxt, pArgs, nArgs) != 0)
    nResult = callBadParamCount;
  delete[] pArgs;
  return nResult;
}

// ---------------------------------------------------------------------
// CHttpServer

CHttpServer* _afxIsapiServer = NULL;

CHttpServer::CHttpServer(TCHAR cDelimiter) : m_cTokenDelimiter(cDelimiter) {
  ::InitializeCriticalSection(&m_cs);
  _afxIsapiServer = this;
}

CHttpServer::~CHttpServer() {
  if (_afxIsapiServer == this) _afxIsapiServer = NULL;
  ::DeleteCriticalSection(&m_cs);
}

BOOL CHttpServer::GetExtensionVersion(HSE_VERSION_INFO* pVer) {
  pVer->dwExtensionVersion = MAKELONG(0, 4);
  CStringA strTitle(GetTitle());
  lstrcpynA(pVer->lpszExtensionDesc, strTitle, HSE_MAX_EXT_DLL_NAME_LEN);
  return TRUE;
}

BOOL CHttpServer::TerminateExtension(DWORD) { return TRUE; }

BOOL CHttpServer::InitInstance(CHttpServerContext*) { return TRUE; }

CHtmlStream* CHttpServer::ConstructStream() { return new CHtmlStream; }

// The extension's file name, without its path.
LPCTSTR CHttpServer::GetTitle() const {
  static TCHAR szTitle[MAX_PATH];
  if (szTitle[0] == 0) {
    TCHAR szPath[MAX_PATH];
    ::GetModuleFileName(AfxGetResourceHandle(), szPath, MAX_PATH);
    LPCTSTR p = _tcsrchr(szPath, '\\');
    lstrcpyn(szTitle, p != NULL ? p + 1 : szPath, MAX_PATH);
  }
  return szTitle;
}

void CHttpServer::AddHeader(CHttpServerContext* pCtxt, LPCTSTR pszString) const {
  pCtxt->m_strHeaders += CStringA(pszString);
}

void CHttpServer::StartContent(CHttpServerContext* pCtxt) const { *pCtxt << "<HTML>\r\n"; }

void CHttpServer::EndContent(CHttpServerContext* pCtxt) const {
  *pCtxt << "</BODY>\r\n</HTML>\r\n";
}

void CHttpServer::WriteTitle(CHttpServerContext* pCtxt) const {
  *pCtxt << "<HEAD><TITLE>" << GetTitle() << "</TITLE></HEAD>\r\n<BODY>\r\n";
}

void CHttpServer::OnWriteBody(CHttpServerContext* pCtxt, LPBYTE pbContent, DWORD dwSize) {
  DWORD dw = dwSize;
  if (dw != 0) pCtxt->WriteClient(pbContent, &dw);
}

void CHttpServer::EndOfRequest(CHttpServerContext*) {}

static LPCSTR StatusText(DWORD dwStatus) {
  switch (dwStatus) {
    case 200: return "OK";
    case 201: return "Created";
    case 204: return "No Content";
    case 301: return "Moved Permanently";
    case 302: return "Moved Temporarily";
    case 304: return "Not Modified";
    case 400: return "Bad Request";
    case 401: return "Unauthorized";
    case 403: return "Forbidden";
    case 404: return "Not Found";
    case 500: return "Server Error";
    case 501: return "Not Implemented";
    case 503: return "Service Unavailable";
  }
  return "";
}

BOOL CHttpServer::OnParseError(CHttpServerContext* pCtxt, int nCause) {
  static const char* const s_reasons[] = {
      "",
      "A required parameter is missing.",
      "The request has the wrong number of parameters.",
      "The command is not one this extension knows.",
      "The command's parameters do not fit.",
      "There is no stream for the reply.",
      "A quotation mark is not closed.",
      "The request has too few parameters.",
      "A parameter is not one the command takes."};
  if (pCtxt->m_pStream == NULL) return FALSE;
  pCtxt->Reset();
  pCtxt->SetStatusCode(nCause == callBadCommand ? 404 : 400);
  StartContent(pCtxt);
  WriteTitle(pCtxt);
  *pCtxt << "<P>"
         << (nCause > 0 && nCause < (int)(sizeof(s_reasons) / sizeof(s_reasons[0]))
                 ? s_reasons[nCause]
                 : "The request could not be handled.")
         << "</P>\r\n";
  EndContent(pCtxt);
  return TRUE;
}

// The command is the query's first item when it is not an argument (it
// has no '='), up to a '?' or the delimiter; the rest are its arguments.
int CHttpServer::CallFunction(CHttpServerContext* pCtxt, LPTSTR pszQuery, LPTSTR pszCommand) {
  const AFX_PARSEMAP* pMap = NULL;
  const AFX_PARSEMAP_ENTRY* pParams = NULL;
  const AFX_PARSEMAP_ENTRY* pEntry = LookUp(pszCommand, pMap, pParams);
  if (pEntry == NULL) return callBadCommand;
  return CallMemberFunc(pCtxt, pEntry, pParams, pszQuery);
}

void CHttpServer::SendReply(CHttpServerContext* pCtxt) {
  CHtmlStream* pStream = pCtxt->m_pStream;
  DWORD dwSize = pStream != NULL ? pStream->GetStreamSize() : 0;
  CStringA strHeaders = pCtxt->m_strHeaders;
  CStringA strLower = strHeaders;
  strLower.MakeLower();
  if (strLower.Find("content-type:") != 0 && strLower.Find("\ncontent-type:") < 0)
    strHeaders += "Content-Type: text/html\r\n";
  CStringA strLength;
  strLength.Format("Content-Length: %lu\r\n\r\n", (unsigned long)dwSize);
  strHeaders += strLength;
  CStringA strStatus;
  strStatus.Format("%lu %s", (unsigned long)pCtxt->m_dwStatusCode,
                   StatusText(pCtxt->m_dwStatusCode));
  pCtxt->m_pECB->dwHttpStatusCode = pCtxt->m_dwStatusCode;
  DWORD cbStatus = (DWORD)strStatus.GetLength();
  pCtxt->ServerSupportFunction(HSE_REQ_SEND_RESPONSE_HEADER, strStatus.GetBuffer(), &cbStatus,
                               (LPDWORD)strHeaders.GetBuffer());
  if (dwSize != 0) OnWriteBody(pCtxt, pStream->m_lpBuffer, dwSize);
}

DWORD CHttpServer::HttpExtensionProc(EXTENSION_CONTROL_BLOCK* pECB) {
  CHttpServerContext ctxt(pECB);
  DWORD dwResult = HSE_STATUS_SUCCESS;
  try {
    ctxt.m_pStream = ConstructStream();
    if (!InitInstance(&ctxt)) return HSE_STATUS_ERROR;
    if (ctxt.m_pStream != NULL) ctxt.m_pStream->InitStream();

    // The request's data: what the server has read, then the rest.
    if (pECB->cbTotalBytes != 0) {
      ctxt.m_pbRawData = new BYTE[pECB->cbTotalBytes + 1];
      DWORD cb = min(pECB->cbAvailable, pECB->cbTotalBytes);
      if (cb != 0) memcpy(ctxt.m_pbRawData, pECB->lpbData, cb);
      while (cb < pECB->cbTotalBytes) {
        DWORD cbRead = pECB->cbTotalBytes - cb;
        if (!ctxt.ReadClient(ctxt.m_pbRawData + cb, &cbRead) || cbRead == 0) break;
        cb += cbRead;
      }
      ctxt.m_pbRawData[cb] = 0;
      ctxt.m_dwRawSize = cb;
    }

    // The command, from the query string.
    CString strQuery(pECB->lpszQueryString != NULL ? pECB->lpszQueryString : "");
    CString strCommand;
    int nEnd = strQuery.FindOneOf(CString(_T("?")) + m_cTokenDelimiter);
    CString strFirst = nEnd < 0 ? strQuery : strQuery.Left(nEnd);
    if (!strFirst.IsEmpty() && strFirst.Find('=') < 0) {
      strCommand = strFirst;
      UrlDecode(strCommand);
      strQuery = nEnd < 0 ? CString() : strQuery.Mid(nEnd + 1);
    }

    // Form data posted is the arguments, after the query string's.
    const AFX_PARSEMAP* pMap = NULL;
    const AFX_PARSEMAP_ENTRY* pParams = NULL;
    const AFX_PARSEMAP_ENTRY* pEntry =
        LookUp(strCommand.IsEmpty() ? NULL : (LPCTSTR)strCommand, pMap, pParams);
    BOOL bRaw = pEntry != NULL && pEntry->pszArgs != NULL &&
                strchr(pEntry->pszArgs, ITS_RAW[0]) != NULL;
    if (!bRaw && ctxt.m_pbRawData != NULL &&
        lstrcmpiA(pECB->lpszContentType != NULL ? pECB->lpszContentType : "",
                  "application/x-www-form-urlencoded") == 0) {
      CString strData((LPCSTR)ctxt.m_pbRawData);
      strData.TrimRight(_T("\r\n"));
      if (!strQuery.IsEmpty() && !strData.IsEmpty()) strQuery += m_cTokenDelimiter;
      strQuery += strData;
    }

    int nResult;
    if (ctxt.m_pStream == NULL) {
      nResult = callNoStream;
    } else if (pEntry == NULL) {
      nResult = callBadCommand;
    } else {
      // CallFunction finds the entry again, so an override sees the
      // command by name.
      CString strCmd = pEntry->pszFnName;
      nResult = CallFunction(&ctxt, strQuery.GetBuffer(), strCmd.GetBuffer());
    }
    if (nResult != callOK) {
      if (!OnParseError(&ctxt, nResult)) dwResult = HSE_STATUS_ERROR;
    }
    if (dwResult == HSE_STATUS_SUCCESS) SendReply(&ctxt);
    EndOfRequest(&ctxt);
  } catch (CException* e) {
    e->Delete();
    dwResult = HSE_STATUS_ERROR;
  }
  return dwResult;
}

// ---------------------------------------------------------------------
// CHttpFilterContext and CHttpFilter

CHttpFilterContext::CHttpFilterContext(PHTTP_FILTER_CONTEXT pfc) : m_pFC(pfc) {}

BOOL CHttpFilterContext::GetServerVariable(LPTSTR lpszVariableName, LPVOID lpvBuffer,
                                           LPDWORD lpdwSize) {
  CStringA strName(lpszVariableName);
  return m_pFC->GetServerVariable(m_pFC, strName.GetBuffer(), lpvBuffer, lpdwSize);
}

BOOL CHttpFilterContext::AddResponseHeaders(LPTSTR lpszHeaders, DWORD dwReserved) {
  CStringA str(lpszHeaders);
  return m_pFC->AddResponseHeaders(m_pFC, str.GetBuffer(), dwReserved);
}

BOOL CHttpFilterContext::WriteClient(LPVOID lpvBuffer, LPDWORD lpdwBytes, DWORD dwReserved) {
  return m_pFC->WriteClient(m_pFC, lpvBuffer, lpdwBytes, dwReserved);
}

LPVOID CHttpFilterContext::AllocMem(DWORD cbSize, DWORD dwReserved) {
  return m_pFC->AllocMem(m_pFC, cbSize, dwReserved);
}

BOOL CHttpFilterContext::ServerSupportFunction(enum SF_REQ_TYPE sfReq, LPVOID lpvBuffer,
                                               LPDWORD lpdwSize, LPDWORD lpdwDataType) {
  return m_pFC->ServerSupportFunction(m_pFC, sfReq, lpvBuffer, (ULONG_PTR)lpdwSize,
                                      (ULONG_PTR)lpdwDataType);
}

CHttpFilter* _afxIsapiFilter = NULL;

CHttpFilter::CHttpFilter() { _afxIsapiFilter = this; }

CHttpFilter::~CHttpFilter() {
  if (_afxIsapiFilter == this) _afxIsapiFilter = NULL;
}

BOOL CHttpFilter::GetFilterVersion(PHTTP_FILTER_VERSION pVer) {
  pVer->dwFilterVersion = HTTP_FILTER_REVISION;
  pVer->dwFlags = 0;
  pVer->lpszFilterDesc[0] = 0;
  return TRUE;
}

BOOL CHttpFilter::TerminateFilter(DWORD) { return TRUE; }

DWORD CHttpFilter::HttpFilterProc(PHTTP_FILTER_CONTEXT pfc, DWORD dwNotificationType,
                                  LPVOID pvNotification) {
  CHttpFilterContext ctxt(pfc);
  DWORD dwResult = SF_STATUS_REQ_NEXT_NOTIFICATION;
  try {
    switch (dwNotificationType) {
      case SF_NOTIFY_READ_RAW_DATA:
        dwResult = OnReadRawData(&ctxt, (PHTTP_FILTER_RAW_DATA)pvNotification);
        break;
      case SF_NOTIFY_PREPROC_HEADERS:
        dwResult = OnPreprocHeaders(&ctxt, (PHTTP_FILTER_PREPROC_HEADERS)pvNotification);
        break;
      case SF_NOTIFY_AUTHENTICATION:
        dwResult = OnAuthentication(&ctxt, (PHTTP_FILTER_AUTHENT)pvNotification);
        break;
      case SF_NOTIFY_URL_MAP:
        dwResult = OnUrlMap(&ctxt, (PHTTP_FILTER_URL_MAP)pvNotification);
        break;
      case SF_NOTIFY_SEND_RAW_DATA:
        dwResult = OnSendRawData(&ctxt, (PHTTP_FILTER_RAW_DATA)pvNotification);
        break;
      case SF_NOTIFY_LOG: dwResult = OnLog(&ctxt, (PHTTP_FILTER_LOG)pvNotification); break;
      case SF_NOTIFY_END_OF_NET_SESSION: dwResult = OnEndOfNetSession(&ctxt); break;
      case SF_NOTIFY_END_OF_REQUEST: dwResult = OnEndOfRequest(&ctxt); break;
    }
  } catch (CException* e) {
    e->Delete();
    dwResult = SF_STATUS_REQ_ERROR;
  }
  return dwResult;
}

DWORD CHttpFilter::OnReadRawData(CHttpFilterContext*, PHTTP_FILTER_RAW_DATA) {
  return SF_STATUS_REQ_NEXT_NOTIFICATION;
}
DWORD CHttpFilter::OnPreprocHeaders(CHttpFilterContext*, PHTTP_FILTER_PREPROC_HEADERS) {
  return SF_STATUS_REQ_NEXT_NOTIFICATION;
}
DWORD CHttpFilter::OnAuthentication(CHttpFilterContext*, PHTTP_FILTER_AUTHENT) {
  return SF_STATUS_REQ_NEXT_NOTIFICATION;
}
DWORD CHttpFilter::OnUrlMap(CHttpFilterContext*, PHTTP_FILTER_URL_MAP) {
  return SF_STATUS_REQ_NEXT_NOTIFICATION;
}
DWORD CHttpFilter::OnSendRawData(CHttpFilterContext*, PHTTP_FILTER_RAW_DATA) {
  return SF_STATUS_REQ_NEXT_NOTIFICATION;
}
DWORD CHttpFilter::OnLog(CHttpFilterContext*, PHTTP_FILTER_LOG) {
  return SF_STATUS_REQ_NEXT_NOTIFICATION;
}
DWORD CHttpFilter::OnEndOfNetSession(CHttpFilterContext*) {
  return SF_STATUS_REQ_NEXT_NOTIFICATION;
}
DWORD CHttpFilter::OnEndOfRequest(CHttpFilterContext*) { return SF_STATUS_REQ_NEXT_NOTIFICATION; }
