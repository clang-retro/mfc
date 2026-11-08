// isapitest.cpp - console tests for afxisapi.h: CHtmlStream, and a
// CHttpServer extension and a CHttpFilter called through the functions an
// ISAPI DLL exports, by a pretend web server.
#include <afx.h>
#include <afxisapi.h>
#include <stdio.h>
#include <math.h>

static int g_failures = 0;
#define CHECK(cond)                                          \
  do {                                                       \
    if (!(cond)) {                                           \
      printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
      g_failures++;                                          \
    }                                                        \
  } while (0)

// ---------------------------------------------------------------------
// The extension.

class CBaseServer : public CHttpServer {
public:
  void Ping(CHttpServerContext* pCtxt) { *pCtxt << "pong"; }
  DECLARE_PARSE_MAP()
};
BEGIN_PARSE_MAP(CBaseServer, CHttpServer)
  ON_PARSE_COMMAND(Ping, CBaseServer, ITS_EMPTY)
END_PARSE_MAP(CBaseServer)

class CTestServer : public CBaseServer {
public:
  CString m_strName;
  int m_nAge;
  CString m_strLastCmd;
  int m_nEnds;

  CTestServer() : m_nAge(-1), m_nEnds(0) {}

  virtual LPCTSTR GetTitle() const { return _T("Test Extension"); }
  virtual void EndOfRequest(CHttpServerContext*) { m_nEnds++; }

  void Default(CHttpServerContext* pCtxt) {
    m_strLastCmd = _T("Default");
    StartContent(pCtxt);
    WriteTitle(pCtxt);
    *pCtxt << _T("Hello");
    EndContent(pCtxt);
  }
  void Add(CHttpServerContext* pCtxt, LPTSTR pszName, int nAge) {
    m_strLastCmd = _T("Add");
    m_strName = pszName;
    m_nAge = nAge;
    *pCtxt << pszName << " is " << nAge;
  }
  void Sum(CHttpServerContext* pCtxt, double a, double b) {
    m_strLastCmd = _T("Sum");
    *pCtxt << (a + b);
  }
  void Types(CHttpServerContext* pCtxt, short s, long l, float f, LONGLONG ll) {
    m_strLastCmd = _T("Types");
    *pCtxt << s << "," << l << "," << f << "," << (long)(ll / 1000000000);
  }
  void Upload(CHttpServerContext* pCtxt, LPVOID pv, DWORD cb) {
    m_strLastCmd = _T("Upload");
    AddHeader(pCtxt, _T("Content-Type: text/plain\r\n"));
    AddHeader(pCtxt, _T("X-Bytes: yes\r\n"));
    *pCtxt << (DWORD)cb << ":";
    pCtxt->m_pStream->Write(pv, (UINT)cb);
  }
  void Missing(CHttpServerContext* pCtxt) {
    pCtxt->SetStatusCode(404);
    *pCtxt << "gone";
  }
  void Var(CHttpServerContext* pCtxt) {
    TCHAR sz[64];
    DWORD cb = sizeof(sz);
    if (pCtxt->GetServerVariable((LPTSTR)_T("SERVER_NAME"), sz, &cb)) *pCtxt << sz;
    else *pCtxt << "none";
  }
  DECLARE_PARSE_MAP()
};
BEGIN_PARSE_MAP(CTestServer, CBaseServer)
  ON_PARSE_COMMAND(Default, CTestServer, ITS_EMPTY)
  DEFAULT_PARSE_COMMAND(Default, CTestServer)
  ON_PARSE_COMMAND(Add, CTestServer, ITS_PSTR ITS_I4)
  ON_PARSE_COMMAND_PARAMS("name age=21")
  ON_PARSE_COMMAND(Sum, CTestServer, ITS_R8 ITS_R8)
  ON_PARSE_COMMAND(Types, CTestServer, ITS_I2 ITS_I4 ITS_R4 ITS_I8)
  ON_PARSE_COMMAND(Upload, CTestServer, ITS_RAW)
  ON_PARSE_COMMAND(Missing, CTestServer, ITS_EMPTY)
  ON_PARSE_COMMAND(Var, CTestServer, ITS_EMPTY)
END_PARSE_MAP(CTestServer)

CTestServer theServer;

// ---------------------------------------------------------------------
// The pretend server: one request, its reply.

struct Reply {
  CStringA strStatus;
  CStringA strHeaders;
  CStringA strBody;
  DWORD dwStatusCode;
};
static Reply g_reply;
static CStringA g_strPosted;
static DWORD g_nRead;

static BOOL WINAPI SrvGetServerVariable(HCONN, LPSTR lpszName, LPVOID lpvBuffer,
                                        LPDWORD lpdwSize) {
  if (lstrcmpA(lpszName, "SERVER_NAME") != 0) {
    ::SetLastError(ERROR_INVALID_INDEX);
    return FALSE;
  }
  const char* psz = "testhost";
  DWORD cb = (DWORD)strlen(psz) + 1;
  if (lpvBuffer == NULL || *lpdwSize < cb) {
    *lpdwSize = cb;
    ::SetLastError(ERROR_INSUFFICIENT_BUFFER);
    return FALSE;
  }
  memcpy(lpvBuffer, psz, cb);
  *lpdwSize = cb;
  return TRUE;
}

static BOOL WINAPI SrvWriteClient(HCONN, LPVOID Buffer, LPDWORD lpdwBytes, DWORD) {
  g_reply.strBody += CStringA((LPCSTR)Buffer, (int)*lpdwBytes);
  return TRUE;
}

// The rest of the posted data, a few bytes at a time.
static BOOL WINAPI SrvReadClient(HCONN, LPVOID lpvBuffer, LPDWORD lpdwSize) {
  DWORD cb = min(*lpdwSize, (DWORD)3);
  cb = min(cb, (DWORD)g_strPosted.GetLength() - g_nRead);
  memcpy(lpvBuffer, (LPCSTR)g_strPosted + g_nRead, cb);
  g_nRead += cb;
  *lpdwSize = cb;
  return TRUE;
}

static BOOL WINAPI SrvSupport(HCONN, DWORD dwRequest, LPVOID lpvBuffer, LPDWORD,
                              LPDWORD lpdwDataType) {
  if (dwRequest != HSE_REQ_SEND_RESPONSE_HEADER) return FALSE;
  g_reply.strStatus = (LPCSTR)lpvBuffer;
  g_reply.strHeaders = (LPCSTR)lpdwDataType;
  return TRUE;
}

static DWORD Request(LPCSTR pszQuery, LPCSTR pszMethod = "GET", LPCSTR pszContentType = "",
                     LPCSTR pszPosted = "", DWORD cbAvailable = 0) {
  g_reply = Reply();
  g_strPosted = pszPosted;
  g_nRead = cbAvailable;
  EXTENSION_CONTROL_BLOCK ecb;
  memset(&ecb, 0, sizeof(ecb));
  ecb.cbSize = sizeof(ecb);
  ecb.dwVersion = HSE_VERSION;
  ecb.lpszMethod = (LPSTR)pszMethod;
  ecb.lpszQueryString = (LPSTR)pszQuery;
  ecb.lpszPathInfo = (LPSTR)"";
  ecb.lpszPathTranslated = (LPSTR)"";
  ecb.lpszContentType = (LPSTR)pszContentType;
  ecb.cbTotalBytes = (DWORD)g_strPosted.GetLength();
  ecb.cbAvailable = cbAvailable;
  ecb.lpbData = (LPBYTE)(LPCSTR)g_strPosted;
  ecb.GetServerVariable = SrvGetServerVariable;
  ecb.WriteClient = SrvWriteClient;
  ecb.ReadClient = SrvReadClient;
  ecb.ServerSupportFunction = SrvSupport;
  DWORD dw = HttpExtensionProc(&ecb);
  g_reply.dwStatusCode = ecb.dwHttpStatusCode;
  return dw;
}

static void TestExtension() {
  HSE_VERSION_INFO ver;
  memset(&ver, 0, sizeof(ver));
  CHECK(GetExtensionVersion(&ver));
  CHECK(strcmp(ver.lpszExtensionDesc, "Test Extension") == 0);
  CHECK(HIWORD(ver.dwExtensionVersion) >= 1);

  // No command: the default one, with its page.
  CHECK(Request("") == HSE_STATUS_SUCCESS);
  CHECK(theServer.m_strLastCmd == _T("Default"));
  CHECK(g_reply.strStatus == "200 OK");
  CHECK(g_reply.dwStatusCode == 200);
  printf("default: [%s] [%s]\n", (LPCSTR)g_reply.strHeaders, (LPCSTR)g_reply.strBody);
  CHECK(g_reply.strBody ==
        "<HTML>\r\n<HEAD><TITLE>Test Extension</TITLE></HEAD>\r\n<BODY>\r\nHello</BODY>\r\n"
        "</HTML>\r\n");
  CStringA strLen;
  strLen.Format("Content-Length: %d\r\n", g_reply.strBody.GetLength());
  CHECK(g_reply.strHeaders.Find("Content-Type: text/html\r\n") == 0);
  CHECK(g_reply.strHeaders.Find(strLen) > 0);
  CHECK(g_reply.strHeaders.Right(4) == "\r\n\r\n");
  CHECK(theServer.m_nEnds == 1);

  // Named arguments in any order, decoded; a missing one takes its default.
  Request("Add&age=42&name=Ann+B%2E");
  CHECK(theServer.m_strName == _T("Ann B.") && theServer.m_nAge == 42);
  CHECK(g_reply.strBody == "Ann B. is 42");
  Request("Add&name=Bob");
  CHECK(theServer.m_strName == _T("Bob") && theServer.m_nAge == 21);
  // Arguments by position, and a command named with '?'.
  Request("add?Cy&7");
  CHECK(theServer.m_strLastCmd == _T("Add"));
  CHECK(theServer.m_strName == _T("Cy") && theServer.m_nAge == 7);
  // A required argument missing, and one the command doesn't take.
  theServer.m_strLastCmd.Empty();
  Request("Add&age=3");
  CHECK(theServer.m_strLastCmd.IsEmpty());
  CHECK(g_reply.dwStatusCode == 400 && g_reply.strStatus == "400 Bad Request");
  CHECK(g_reply.strBody.Find("<HTML>") == 0);
  Request("Add&name=X&size=3");
  CHECK(theServer.m_strLastCmd.IsEmpty() && g_reply.dwStatusCode == 400);
  // An unknown command.
  Request("Nothing&x");
  CHECK(g_reply.dwStatusCode == 404);

  // Numbers without a parameter list: by position only.
  Request("Sum&1.5&2.25");
  CHECK(theServer.m_strLastCmd == _T("Sum"));
  CHECK(g_reply.strBody == "3.75");
  theServer.m_strLastCmd.Empty();
  Request("Sum&1");
  CHECK(theServer.m_strLastCmd.IsEmpty() && g_reply.dwStatusCode == 400);
  Request("Types&-3&100000&0.5&5000000000");
  printf("types: %s\n", (LPCSTR)g_reply.strBody);
  CHECK(g_reply.strBody == "-3,100000,0.5,5");

  // A command of the base class's map.
  Request("Ping");
  CHECK(g_reply.strBody == "pong");

  // Form data posted: the arguments, read in part from the server.
  Request("Add", "POST", "application/x-www-form-urlencoded", "name=Dee&age=9", 5);
  CHECK(theServer.m_strName == _T("Dee") && theServer.m_nAge == 9);
  // Raw data, with headers of the command's own.
  Request("Upload", "POST", "application/octet-stream", "a=b&c", 2);
  CHECK(theServer.m_strLastCmd == _T("Upload"));
  printf("upload: [%s] [%s]\n", (LPCSTR)g_reply.strHeaders, (LPCSTR)g_reply.strBody);
  CHECK(g_reply.strBody == "5:a=b&c");
  CHECK(g_reply.strHeaders.Find("Content-Type: text/plain\r\nX-Bytes: yes\r\n") == 0);
  CHECK(g_reply.strHeaders.Find("text/html") < 0);

  // The command's own status, and the server's variables.
  Request("Missing");
  CHECK(g_reply.strStatus == "404 Not Found" && g_reply.strBody == "gone");
  Request("Var");
  CHECK(g_reply.strBody == "testhost");
  CHECK(TerminateExtension(HSE_TERM_MUST_UNLOAD));
}

// ---------------------------------------------------------------------
// The filter.

class CTestFilter : public CHttpFilter {
public:
  int m_nLogs;
  int m_nEnds;
  CTestFilter() : m_nLogs(0), m_nEnds(0) {}
  virtual BOOL GetFilterVersion(PHTTP_FILTER_VERSION pVer) {
    CHttpFilter::GetFilterVersion(pVer);
    pVer->dwFlags = SF_NOTIFY_ORDER_LOW | SF_NOTIFY_NONSECURE_PORT | SF_NOTIFY_URL_MAP |
                    SF_NOTIFY_LOG | SF_NOTIFY_END_OF_REQUEST;
    lstrcpyA(pVer->lpszFilterDesc, "Test Filter");
    return TRUE;
  }
  // Every page comes from one folder.
  virtual DWORD OnUrlMap(CHttpFilterContext* pfc, PHTTP_FILTER_URL_MAP pUrlMap) {
    char* psz = (char*)pfc->AllocMem(64);
    if (psz == NULL) return SF_STATUS_REQ_ERROR;
    lstrcpyA(psz, "C:\\pages");
    lstrcpynA(pUrlMap->pszPhysicalPath, psz, (int)pUrlMap->cbPathBuff);
    pfc->AddResponseHeaders((LPTSTR)_T("X-Filtered: 1\r\n"));
    return SF_STATUS_REQ_HANDLED_NOTIFICATION;
  }
  virtual DWORD OnLog(CHttpFilterContext*, PHTTP_FILTER_LOG) {
    m_nLogs++;
    return SF_STATUS_REQ_NEXT_NOTIFICATION;
  }
  virtual DWORD OnEndOfRequest(CHttpFilterContext* pfc) {
    m_nEnds++;
    DWORD cb = 4;
    pfc->WriteClient((LPVOID) "done", &cb);
    return SF_STATUS_REQ_NEXT_NOTIFICATION;
  }
};

CTestFilter theFilter;

static CStringA g_strFilterHeaders, g_strFilterWritten;
static char g_alloc[256];

static BOOL WINAPI FltAddResponseHeaders(PHTTP_FILTER_CONTEXT, LPSTR lpszHeaders, DWORD) {
  g_strFilterHeaders += lpszHeaders;
  return TRUE;
}
static BOOL WINAPI FltWriteClient(PHTTP_FILTER_CONTEXT, LPVOID Buffer, LPDWORD lpdwBytes, DWORD) {
  g_strFilterWritten += CStringA((LPCSTR)Buffer, (int)*lpdwBytes);
  return TRUE;
}
static VOID* WINAPI FltAllocMem(PHTTP_FILTER_CONTEXT, DWORD cbSize, DWORD) {
  return cbSize <= sizeof(g_alloc) ? g_alloc : NULL;
}

static void TestFilter() {
  HTTP_FILTER_VERSION ver;
  memset(&ver, 0, sizeof(ver));
  CHECK(GetFilterVersion(&ver));
  CHECK(ver.dwFilterVersion == HTTP_FILTER_REVISION);
  CHECK((ver.dwFlags & SF_NOTIFY_URL_MAP) != 0);
  CHECK(strcmp(ver.lpszFilterDesc, "Test Filter") == 0);

  HTTP_FILTER_CONTEXT fc;
  memset(&fc, 0, sizeof(fc));
  fc.cbSize = sizeof(fc);
  fc.Revision = HTTP_FILTER_REVISION;
  fc.AddResponseHeaders = FltAddResponseHeaders;
  fc.WriteClient = FltWriteClient;
  fc.AllocMem = FltAllocMem;

  char szPath[MAX_PATH] = "C:\\inetpub\\index.htm";
  HTTP_FILTER_URL_MAP map;
  map.pszURL = "/index.htm";
  map.pszPhysicalPath = szPath;
  map.cbPathBuff = sizeof(szPath);
  CHECK(HttpFilterProc(&fc, SF_NOTIFY_URL_MAP, &map) == SF_STATUS_REQ_HANDLED_NOTIFICATION);
  CHECK(strcmp(szPath, "C:\\pages") == 0);
  CHECK(g_strFilterHeaders == "X-Filtered: 1\r\n");

  HTTP_FILTER_LOG log;
  memset(&log, 0, sizeof(log));
  CHECK(HttpFilterProc(&fc, SF_NOTIFY_LOG, &log) == SF_STATUS_REQ_NEXT_NOTIFICATION);
  CHECK(theFilter.m_nLogs == 1);
  CHECK(HttpFilterProc(&fc, SF_NOTIFY_END_OF_REQUEST, NULL) == SF_STATUS_REQ_NEXT_NOTIFICATION);
  CHECK(theFilter.m_nEnds == 1 && g_strFilterWritten == "done");
  // One the filter doesn't handle.
  HTTP_FILTER_RAW_DATA raw;
  memset(&raw, 0, sizeof(raw));
  CHECK(HttpFilterProc(&fc, SF_NOTIFY_READ_RAW_DATA, &raw) == SF_STATUS_REQ_NEXT_NOTIFICATION);
  CHECK(TerminateFilter(0));
}

// ---------------------------------------------------------------------
// CHtmlStream

static void TestStream() {
  CHtmlStream stream(16);
  stream << "abc" << (short)-2 << 7 << (long)-8 << (DWORD)9 << (BYTE)'!' << 0.5;
  stream << _T("xyz") << CString(_T("Q"));
  CHECK(stream.GetStreamSize() == 17);
  CHECK(memcmp(stream.m_lpBuffer, "abc-27-89!0.5xyzQ", 17) == 0);
  CHECK(stream.m_nBufferSize == 32);  // grown by 16 twice
  CHtmlStream other;
  other << "<" << stream << ">";
  CHECK(other.GetStreamSize() == 19 && other.m_lpBuffer[18] == '>');
  stream.Reset();
  CHECK(stream.GetStreamSize() == 0);
  stream << "x";
  CHECK(stream.GetStreamSize() == 1);

  // A caller's buffer that can't grow throws when it is full; one that
  // can grows into memory of the stream's own.
  BYTE buf[4];
  CHtmlStream fixed(buf, sizeof(buf));
  fixed << "1234";
  CHECK(memcmp(buf, "1234", 4) == 0);
  BOOL bThrew = FALSE;
  try {
    fixed << "5";
  } catch (CMemoryException* e) {
    bThrew = TRUE;
    e->Delete();
  }
  CHECK(bThrew);
  CHECK(fixed.Detach() == buf);
  CHtmlStream growing(buf, sizeof(buf), 8);
  growing << "abcdef";
  CHECK(growing.m_lpBuffer != buf && growing.GetStreamSize() == 6);
  CHECK(memcmp(growing.m_lpBuffer, "abcdef", 6) == 0);
  BYTE* p = growing.Detach();
  CHECK(p != NULL && growing.GetStreamSize() == 0);
  free(p);
}

int main() {
  TestStream();
  TestExtension();
  TestFilter();
  if (g_failures == 0) printf("PASS: all ISAPI tests\n");
  else printf("%d failure(s)\n", g_failures);
  return g_failures ? 1 : 0;
}
