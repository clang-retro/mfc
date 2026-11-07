// inettest.cpp - console tests for afxinet.h against an HTTP server and an
// FTP server that the test runs on the loopback interface: URL parsing,
// OpenURL, requests with headers and bodies, status codes and headers,
// cookies, the status callback, errors, and FTP directories, files,
// listings and transfers in passive and active mode.
#include <winsock2.h>
#include <afx.h>
#include <afxwin.h>
#include <afxinet.h>
#include <afxsock.h>  // for ws2_32, which the test servers use
#include <stdio.h>
#include <afxtempl.h>

static int g_failures = 0;
#define CHECK(cond)                                                   \
  do {                                                                \
    if (!(cond)) {                                                    \
      printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);          \
      g_failures++;                                                   \
    }                                                                 \
  } while (0)

// ---------------------------------------------------------------------
// Small servers. Each listens on a port of its own and runs in a thread.
// They use CStringA and CArray, since the C++ library's strings crash in a
// CreateThread thread with some MinGW runtimes.

static SOCKET Listen(u_short& nPort) {
  SOCKET s = ::socket(AF_INET, SOCK_STREAM, 0);
  sockaddr_in sa;
  memset(&sa, 0, sizeof(sa));
  sa.sin_family = AF_INET;
  sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  ::bind(s, (sockaddr*)&sa, sizeof(sa));
  int n = sizeof(sa);
  ::getsockname(s, (sockaddr*)&sa, &n);
  nPort = ntohs(sa.sin_port);
  ::listen(s, 5);
  return s;
}

static void SendAll(SOCKET s, const char* p, int nLen) {
  int n = 0;
  while (n < nLen) {
    int r = ::send(s, p + n, nLen - n, 0);
    if (r <= 0) return;
    n += r;
  }
}

static void SendAll(SOCKET s, const CStringA& str) { SendAll(s, str, str.GetLength()); }

// Reads one line (without its \r\n); FALSE when the peer has gone.
static BOOL RecvLine(SOCKET s, CStringA& line) {
  line.Empty();
  char c;
  for (;;) {
    int r = ::recv(s, &c, 1, 0);
    if (r <= 0) return !line.IsEmpty();
    if (c == '\n') break;
    if (c != '\r') line += c;
  }
  return TRUE;
}

static CStringA RecvCount(SOCKET s, int n) {
  CStringA str;
  char buf[1024];
  while (str.GetLength() < n) {
    int nWant = n - str.GetLength();
    int r = ::recv(s, buf, nWant < (int)sizeof(buf) ? nWant : (int)sizeof(buf), 0);
    if (r <= 0) break;
    str += CStringA(buf, r);
  }
  return str;
}

static CStringA RecvAll(SOCKET s) {
  CStringA str;
  char buf[1024];
  int r;
  while ((r = ::recv(s, buf, sizeof(buf), 0)) > 0) str += CStringA(buf, r);
  return str;
}

static volatile LONG g_bStop = 0;

// HTTP: GET /hello.txt, /header (the X-Test header back), /missing (404),
// POST and PUT /echo (the body back). One request per connection.
static const char kHello[] = "line one\r\nline two\nline three";
static SOCKET g_hHttpListen = INVALID_SOCKET;

static void ServeHttp(SOCKET c) {
  CStringA line, method, path, xtest;
  if (!RecvLine(c, line)) return;
  int sp1 = line.Find(' '), sp2 = line.Find(' ', sp1 + 1);
  method = line.Left(sp1);
  path = line.Mid(sp1 + 1, sp2 - sp1 - 1);
  int nLen = 0;
  BOOL bChunked = FALSE;
  while (RecvLine(c, line) && !line.IsEmpty()) {
    int i = line.Find(':');
    CStringA name = line.Left(i), value = line.Mid(i + 1);
    value.TrimLeft();
    name.MakeLower();
    if (name == "content-length") nLen = atoi(value);
    if (name == "transfer-encoding") bChunked = TRUE;
    if (name == "x-test") xtest = value;
  }
  CStringA body = bChunked ? CStringA() : RecvCount(c, nLen);
  CStringA status = "200 OK", reply, type = "text/plain";
  if (path == "/hello.txt") {
    reply = kHello;
  } else if (path == "/header") {
    reply = xtest;
  } else if (path == "/echo" && (method == "POST" || method == "PUT")) {
    reply = method + ":" + body;
  } else {
    status = "404 Not Found";
    reply = "nothing here";
    type = "text/html";
  }
  CStringA head;
  head.Format(
      "HTTP/1.1 %s\r\nContent-Type: %s\r\nContent-Length: %d\r\n"
      "Last-Modified: Tue, 15 Nov 1994 08:12:31 GMT\r\nX-Server: inettest\r\n"
      "Connection: close\r\n\r\n",
      (LPCSTR)status, (LPCSTR)type, reply.GetLength());
  SendAll(c, head);
  if (method != "HEAD") SendAll(c, reply);
}

static DWORD WINAPI HttpThread(LPVOID) {
  while (!g_bStop) {
    SOCKET c = ::accept(g_hHttpListen, NULL, NULL);
    if (c == INVALID_SOCKET) break;
    ServeHttp(c);
    ::shutdown(c, SD_SEND);
    ::closesocket(c);
  }
  return 0;
}

// FTP: a folder tree in memory, for user "user" with password "pass".
static CArray<CStringA, const CStringA&> g_ftpNames, g_ftpData, g_ftpDirs;
static SOCKET g_hFtpListen = INVALID_SOCKET;
static int g_nPassive = 0, g_nActive = 0;

static int FtpFind(const CStringA& path) {
  for (int i = 0; i < g_ftpNames.GetSize(); i++)
    if (g_ftpNames[i] == path) return i;
  return -1;
}
static BOOL FtpHas(const char* path) { return FtpFind(path) >= 0; }
static CStringA FtpData(const char* path) {
  int i = FtpFind(path);
  return i >= 0 ? g_ftpData[i] : CStringA("<none>");
}
static void FtpPut(const CStringA& path, const CStringA& data) {
  int i = FtpFind(path);
  if (i >= 0) {
    g_ftpData[i] = data;
  } else {
    g_ftpNames.Add(path);
    g_ftpData.Add(data);
  }
}
static BOOL FtpDel(const CStringA& path) {
  int i = FtpFind(path);
  if (i < 0) return FALSE;
  g_ftpNames.RemoveAt(i);
  g_ftpData.RemoveAt(i);
  return TRUE;
}
static int DirFind(const CStringA& path) {
  for (int i = 0; i < g_ftpDirs.GetSize(); i++)
    if (g_ftpDirs[i] == path) return i;
  return -1;
}
static BOOL DirHas(const char* path) { return DirFind(path) >= 0; }

static CStringA FtpPath(const CStringA& cwd, const CStringA& name) {
  CStringA p;
  if (name.IsEmpty())
    p = cwd;
  else if (name[0] == '/')
    p = name;
  else
    p = (cwd == "/" ? CStringA() : cwd) + "/" + name;
  if (p.GetLength() > 1 && p[p.GetLength() - 1] == '/') p = p.Left(p.GetLength() - 1);
  return p;
}

static CStringA Parent(const CStringA& p) {
  int i = p.ReverseFind('/');
  return i <= 0 ? CStringA("/") : p.Left(i);
}

static void Reply(SOCKET c, const char* psz) { SendAll(c, CStringA(psz) + "\r\n"); }

// The data connection for a transfer: the passive listener's, or one to
// the client's PORT address.
struct FtpDataConn {
  SOCKET hPasv;
  BOOL bActive;
  sockaddr_in active;
};

static SOCKET OpenData(FtpDataConn& data) {
  if (data.bActive) {
    data.bActive = FALSE;
    SOCKET d = ::socket(AF_INET, SOCK_STREAM, 0);
    if (::connect(d, (sockaddr*)&data.active, sizeof(data.active)) != 0) {
      ::closesocket(d);
      return INVALID_SOCKET;
    }
    g_nActive++;
    return d;
  }
  if (data.hPasv == INVALID_SOCKET) return INVALID_SOCKET;
  SOCKET d = ::accept(data.hPasv, NULL, NULL);
  ::closesocket(data.hPasv);
  data.hPasv = INVALID_SOCKET;
  g_nPassive++;
  return d;
}

static void ServeFtp(SOCKET c) {
  CStringA cwd = "/", line, renameFrom;
  FtpDataConn data;
  data.hPasv = INVALID_SOCKET;
  data.bActive = FALSE;
  Reply(c, "220 inettest FTP");
  while (RecvLine(c, line)) {
    int sp = line.Find(' ');
    CStringA cmd = sp >= 0 ? line.Left(sp) : line;
    CStringA arg = sp >= 0 ? line.Mid(sp + 1) : CStringA();
    cmd.MakeUpper();
    CStringA path = FtpPath(cwd, arg);
    if (cmd == "USER") {
      Reply(c, arg == "user" ? "331 Password please" : "530 Unknown user");
    } else if (cmd == "PASS") {
      Reply(c, arg == "pass" ? "230 Logged in" : "530 Wrong password");
    } else if (cmd == "SYST") {
      Reply(c, "215 UNIX Type: L8");
    } else if (cmd == "PWD" || cmd == "XPWD") {
      Reply(c, "257 \"" + cwd + "\" is the current directory");
    } else if (cmd == "CWD") {
      if (DirHas(path)) {
        cwd = path;
        Reply(c, "250 OK");
      } else {
        Reply(c, "550 No such directory");
      }
    } else if (cmd == "CDUP") {
      cwd = Parent(cwd);
      Reply(c, "250 OK");
    } else if (cmd == "TYPE" || cmd == "MODE" || cmd == "STRU" || cmd == "NOOP") {
      Reply(c, "200 OK");
    } else if (cmd == "PASV") {
      u_short nPort;
      if (data.hPasv != INVALID_SOCKET) ::closesocket(data.hPasv);
      data.hPasv = Listen(nPort);
      CStringA str;
      str.Format("227 Entering Passive Mode (127,0,0,1,%d,%d)", nPort >> 8, nPort & 0xff);
      Reply(c, str);
    } else if (cmd == "PORT") {
      int h1, h2, h3, h4, p1, p2;
      sscanf(arg, "%d,%d,%d,%d,%d,%d", &h1, &h2, &h3, &h4, &p1, &p2);
      memset(&data.active, 0, sizeof(data.active));
      data.active.sin_family = AF_INET;
      data.active.sin_addr.s_addr = htonl((h1 << 24) | (h2 << 16) | (h3 << 8) | h4);
      data.active.sin_port = htons((u_short)((p1 << 8) | p2));
      data.bActive = TRUE;
      Reply(c, "200 PORT OK");
    } else if (cmd == "LIST" || cmd == "NLST") {
      // A pattern such as "*" lists the current directory.
      CStringA dir = (arg.IsEmpty() || arg[0] == '-' || arg.Find('*') >= 0) ? cwd : path;
      SOCKET d = OpenData(data);
      if (d == INVALID_SOCKET) {
        Reply(c, "425 No data connection");
        continue;
      }
      Reply(c, "150 Listing");
      CStringA out, prefix = dir == "/" ? CStringA("/") : dir + "/";
      int nPrefix = prefix.GetLength();
      for (int i = 0; i < g_ftpDirs.GetSize(); i++) {
        const CStringA& sub = g_ftpDirs[i];
        if (sub.GetLength() > nPrefix && sub.Left(nPrefix) == prefix && sub.Find('/', nPrefix) < 0)
          out += "drwxr-xr-x 1 owner group 0 Jan 02 12:00 " + sub.Mid(nPrefix) + "\r\n";
      }
      for (int i = 0; i < g_ftpNames.GetSize(); i++) {
        const CStringA& f = g_ftpNames[i];
        if (f.Left(nPrefix) == prefix && f.Find('/', nPrefix) < 0) {
          CStringA entry;
          entry.Format("-rw-r--r-- 1 owner group %d Jan 02 12:00 ", g_ftpData[i].GetLength());
          out += entry + f.Mid(nPrefix) + "\r\n";
        }
      }
      SendAll(d, out);
      ::closesocket(d);
      Reply(c, "226 Done");
    } else if (cmd == "RETR") {
      int i = FtpFind(path);
      if (i < 0) {
        if (data.hPasv != INVALID_SOCKET) ::closesocket(data.hPasv);
        data.hPasv = INVALID_SOCKET;
        data.bActive = FALSE;
        Reply(c, "550 No such file");
        continue;
      }
      SOCKET d = OpenData(data);
      Reply(c, "150 Sending");
      SendAll(d, g_ftpData[i]);
      ::closesocket(d);
      Reply(c, "226 Done");
    } else if (cmd == "STOR") {
      SOCKET d = OpenData(data);
      Reply(c, "150 Receiving");
      FtpPut(path, RecvAll(d));
      ::closesocket(d);
      Reply(c, "226 Done");
    } else if (cmd == "SIZE") {
      int i = FtpFind(path);
      if (i >= 0) {
        CStringA str;
        str.Format("213 %d", g_ftpData[i].GetLength());
        Reply(c, str);
      } else {
        Reply(c, "550 No such file");
      }
    } else if (cmd == "MKD") {
      if (DirFind(path) < 0) g_ftpDirs.Add(path);
      Reply(c, "257 \"" + path + "\" created");
    } else if (cmd == "RMD") {
      BOOL bEmpty = TRUE;
      for (int i = 0; i < g_ftpNames.GetSize(); i++)
        if (Parent(g_ftpNames[i]) == path) bEmpty = FALSE;
      int i = DirFind(path);
      if (i >= 0 && bEmpty) {
        g_ftpDirs.RemoveAt(i);
        Reply(c, "250 Removed");
      } else {
        Reply(c, "550 Directory not empty");
      }
    } else if (cmd == "DELE") {
      Reply(c, FtpDel(path) ? "250 Deleted" : "550 No such file");
    } else if (cmd == "RNFR") {
      renameFrom = path;
      Reply(c, FtpFind(path) >= 0 ? "350 Ready" : "550 No such file");
    } else if (cmd == "RNTO") {
      int i = FtpFind(renameFrom);
      if (i >= 0) {
        CStringA contents = g_ftpData[i];
        FtpDel(renameFrom);
        FtpPut(path, contents);
        Reply(c, "250 Renamed");
      } else {
        Reply(c, "550 No such file");
      }
    } else if (cmd == "QUIT") {
      Reply(c, "221 Bye");
      break;
    } else {
      Reply(c, "502 Not implemented");
    }
  }
  if (data.hPasv != INVALID_SOCKET) ::closesocket(data.hPasv);
}

static DWORD WINAPI FtpThread(LPVOID) {
  while (!g_bStop) {
    SOCKET c = ::accept(g_hFtpListen, NULL, NULL);
    if (c == INVALID_SOCKET) break;
    ServeFtp(c);
    ::closesocket(c);
  }
  return 0;
}

// ---------------------------------------------------------------------
// The tests

static void TestParse() {
  DWORD dwService;
  CString strServer, strObject, strUser, strPass;
  INTERNET_PORT nPort;
  CHECK(AfxParseURLEx(_T("http://me:secret@www.example.com:8080/a/b.html?x=1"), dwService,
                      strServer, strObject, nPort, strUser, strPass));
  CHECK(dwService == AFX_INET_SERVICE_HTTP && strServer == _T("www.example.com"));
  CHECK(strObject == _T("/a/b.html?x=1") && nPort == 8080);
  CHECK(strUser == _T("me") && strPass == _T("secret"));
  CHECK(AfxParseURL(_T("ftp://ftp.example.com"), dwService, strServer, strObject, nPort));
  CHECK(dwService == AFX_INET_SERVICE_FTP && strObject == _T("/") && nPort == 21);
  CHECK(AfxParseURL(_T("https://secure.example.com/x"), dwService, strServer, strObject, nPort));
  CHECK(dwService == AFX_INET_SERVICE_HTTPS && nPort == 443);
  CHECK(AfxParseURL(_T("gopher://g.example.com/"), dwService, strServer, strObject, nPort));
  CHECK(dwService == AFX_INET_SERVICE_GOPHER);
  CHECK(AfxParseURL(_T("file:///C:/dir/x.txt"), dwService, strServer, strObject, nPort));
  CHECK(dwService == AFX_INET_SERVICE_FILE);
  CHECK(!AfxParseURL(_T("not a url"), dwService, strServer, strObject, nPort));
  CHECK(!AfxParseURL(_T(""), dwService, strServer, strObject, nPort));
}

class CTestSession : public CInternetSession {
public:
  int m_nCallbacks;
  DWORD_PTR m_dwLastContext;
  CTestSession()
      : CInternetSession(_T("inettest"), 1, INTERNET_OPEN_TYPE_DIRECT),
        m_nCallbacks(0),
        m_dwLastContext(0) {}
  virtual void OnStatusCallback(DWORD_PTR dwContext, DWORD, LPVOID, DWORD) {
    m_nCallbacks++;
    m_dwLastContext = dwContext;
  }
};

static CStringA ReadAllA(CInternetFile* pFile) {
  CStringA str;
  char buf[100];
  UINT n;
  while ((n = pFile->Read(buf, sizeof(buf))) > 0) str += CStringA(buf, n);
  return str;
}

static void TestHttp(CTestSession& session, u_short nHttpPort) {
  CString strBase;
  strBase.Format(_T("http://127.0.0.1:%u"), nHttpPort);

  // OpenURL: a CHttpFile, read by lines.
  CStdioFile* pStd = session.OpenURL(strBase + _T("/hello.txt"), 1, INTERNET_FLAG_RELOAD);
  CHECK(pStd != NULL && pStd->IsKindOf(RUNTIME_CLASS(CHttpFile)));
  CHttpFile* pHttp = (CHttpFile*)pStd;
  DWORD dwStatus = 0;
  CHECK(pHttp->QueryInfoStatusCode(dwStatus) && dwStatus == 200);
  CHECK(pHttp->GetLength() == strlen(kHello));
  CHECK(pHttp->GetFileURL() == strBase + _T("/hello.txt"));
  CHECK(pHttp->GetVerb() == _T("GET"));
  CString strLine;
  CHECK(pHttp->ReadString(strLine) && strLine == _T("line one"));
  CHECK(pHttp->ReadString(strLine) && strLine == _T("line two"));
  CHECK(pHttp->ReadString(strLine) && strLine == _T("line three"));
  CHECK(!pHttp->ReadString(strLine));
  CHECK(pHttp->GetPosition() == strlen(kHello));
  CString strType;
  CHECK(pHttp->QueryInfo(HTTP_QUERY_CONTENT_TYPE, strType) && strType == _T("text/plain"));
  SYSTEMTIME st;
  CHECK(pHttp->QueryInfo(HTTP_QUERY_LAST_MODIFIED, &st) && st.wYear == 1994 && st.wMonth == 11);
  pHttp->Close();
  delete pHttp;

  // A connection, a request and a 404.
  CHttpConnection* pConn = session.GetHttpConnection(_T("127.0.0.1"), nHttpPort);
  CHECK(pConn != NULL && pConn->GetServerName() == _T("127.0.0.1"));
  CHttpFile* pFile = pConn->OpenRequest(CHttpConnection::HTTP_VERB_GET, _T("/missing"), NULL, 1,
                                        NULL, NULL, INTERNET_FLAG_RELOAD);
  CHECK(pFile->SendRequest());
  CHECK(pFile->QueryInfoStatusCode(dwStatus) && dwStatus == 404);
  DWORD dwLen = 0;
  CHECK(pFile->QueryInfo(HTTP_QUERY_CONTENT_LENGTH, dwLen) && dwLen == 12);
  CHECK(ReadAllA(pFile) == "nothing here");
  delete pFile;

  // A header of our own.
  pFile = pConn->OpenRequest(_T("GET"), _T("/header"), NULL, 1, NULL, NULL, INTERNET_FLAG_RELOAD);
  CHECK(pFile->AddRequestHeaders(_T("X-Test: from the client\r\n")));
  CHECK(pFile->SendRequest());
  CHECK(ReadAllA(pFile) == "from the client");
  delete pFile;

  // A POST with its body.
  pFile = pConn->OpenRequest(CHttpConnection::HTTP_VERB_POST, _T("/echo"), NULL, 1, NULL, NULL,
                             INTERNET_FLAG_RELOAD);
  CString strHeaders = _T("Content-Type: application/x-www-form-urlencoded");
  char body[] = "a=1&b=2";
  CHECK(pFile->SendRequest(strHeaders, body, (DWORD)strlen(body)));
  CHECK(ReadAllA(pFile) == "POST:a=1&b=2");
  delete pFile;

  // A PUT whose body the program writes, through a write buffer.
  pFile = pConn->OpenRequest(CHttpConnection::HTTP_VERB_PUT, _T("/echo"), NULL, 1, NULL, NULL,
                             INTERNET_FLAG_RELOAD);
  CHECK(pFile->SendRequestEx(10));
  pFile->SetWriteBufferSize(4);
  pFile->Write("01234", 5);
  pFile->WriteString(_T("56789"));
  CHECK(pFile->EndRequest());
  CHECK(pFile->QueryInfoStatusCode(dwStatus) && dwStatus == 200);
  CHECK(ReadAllA(pFile) == "PUT:0123456789");
  delete pFile;
  delete pConn;

  // Errors come as CInternetException.
  BOOL bThrown = FALSE;
  try {
    CStdioFile* p = session.OpenURL(_T("bogus://x/"));
    delete p;
  } catch (CInternetException* e) {
    bThrown = e->m_dwError == ERROR_INTERNET_UNRECOGNIZED_SCHEME ||
              e->m_dwError == ERROR_INTERNET_INVALID_URL;
    e->Delete();
  }
  CHECK(bThrown);
  // Nothing listens on a port just freed.
  u_short nFree;
  ::closesocket(Listen(nFree));
  CString strDead;
  strDead.Format(_T("http://127.0.0.1:%u/"), nFree);
  bThrown = FALSE;
  try {
    CStdioFile* p = session.OpenURL(strDead, 1, INTERNET_FLAG_RELOAD);
    delete p;
  } catch (CInternetException* e) {
    bThrown = TRUE;
    TCHAR sz[256];
    CHECK(e->m_dwError == ERROR_INTERNET_CANNOT_CONNECT);
    CHECK(e->GetErrorMessage(sz, 256) && sz[0] != 0);
    e->Delete();
  }
  CHECK(bThrown);

  // Cookies for this session's process.
  CString strCookieURL = strBase + _T("/");
  CHECK(CInternetSession::SetCookie(strCookieURL, _T("flavor"), _T("oatmeal")));
  CString strCookie;
  CHECK(CInternetSession::GetCookie(strCookieURL, _T("flavor"), strCookie) &&
        strCookie.Find(_T("oatmeal")) >= 0);
  CHECK(CInternetSession::GetCookieLength(strCookieURL, _T("flavor")) > 0);

  // The status callback, with a context of our own.
  CHECK(session.EnableStatusCallback());
  pStd = session.OpenURL(strBase + _T("/hello.txt"), 42, INTERNET_FLAG_RELOAD);
  CHECK(ReadAllA((CInternetFile*)pStd) == kHello);
  delete pStd;
  CHECK(session.m_nCallbacks > 0 && session.m_dwLastContext == 42);
  CHECK(session.EnableStatusCallback(FALSE));
  int nCallbacks = session.m_nCallbacks;
  pStd = session.OpenURL(strBase + _T("/hello.txt"), 42, INTERNET_FLAG_RELOAD);
  delete pStd;
  CHECK(session.m_nCallbacks == nCallbacks);

  // A local file through OpenURL.
  TCHAR szPath[MAX_PATH];
  ::GetModuleFileName(NULL, szPath, MAX_PATH);
  CString strExe = szPath;
  CString strLocal = strExe.Left(strExe.ReverseFind('\\') + 1) + _T("inettest.txt");
  {
    CStdioFile f(strLocal, CFile::modeCreate | CFile::modeWrite | CFile::typeBinary);
    f.Write("local\n", 6);
  }
  CString strFileURL = _T("file:///") + strLocal;
  strFileURL.Replace('\\', '/');
  pStd = session.OpenURL(strFileURL);
  CHECK(pStd != NULL && !pStd->IsKindOf(RUNTIME_CLASS(CInternetFile)));
  CHECK(pStd->ReadString(strLine) && strLine.Left(5) == _T("local"));
  delete pStd;
  ::DeleteFile(strLocal);
}

static void TestFtp(CTestSession& session, u_short nFtpPort) {
  g_ftpDirs.Add("/");
  FtpPut("/readme.txt", "read me\r\n");

  CFtpConnection* pFtp = NULL;
  try {
    pFtp = session.GetFtpConnection(_T("127.0.0.1"), _T("user"), _T("pass"), nFtpPort, TRUE);
  } catch (CInternetException* e) {
    printf("FTP connection failed: %lu\n", (unsigned long)e->m_dwError);
    e->Delete();
  }
  CHECK(pFtp != NULL);
  if (pFtp == NULL) return;

  CString strDir;
  CHECK(pFtp->GetCurrentDirectory(strDir) && strDir == _T("/"));
  CHECK(pFtp->CreateDirectory(_T("sub")));
  CHECK(pFtp->SetCurrentDirectory(_T("sub")));
  CHECK(pFtp->GetCurrentDirectory(strDir) && strDir == _T("/sub"));
  CString strURL, strExpect;
  strExpect.Format(_T("ftp://127.0.0.1:%u/sub"), nFtpPort);
  CHECK(pFtp->GetCurrentDirectoryAsURL(strURL) && strURL == strExpect);
  CHECK(!pFtp->SetCurrentDirectory(_T("/nowhere")));

  // Writing and reading a file.
  CInternetFile* pFile = pFtp->OpenFile(_T("a.txt"), GENERIC_WRITE);
  pFile->Write("first line\r\n", 12);
  pFile->WriteString(_T("second line\r\n"));
  pFile->Close();
  delete pFile;
  CHECK(FtpData("/sub/a.txt") == "first line\r\nsecond line\r\n");
  pFile = pFtp->OpenFile(_T("a.txt"));
  CString strLine;
  CHECK(pFile->ReadString(strLine) && strLine == _T("first line"));
  CHECK(pFile->ReadString(strLine) && strLine == _T("second line"));
  CHECK(!pFile->ReadString(strLine));
  delete pFile;

  // A file that is not there.
  BOOL bThrown = FALSE;
  try {
    pFile = pFtp->OpenFile(_T("none.txt"));
    delete pFile;
  } catch (CInternetException* e) {
    bThrown = TRUE;
    TCHAR sz[256];
    CHECK(e->GetErrorMessage(sz, 256) && sz[0] != 0);
    e->Delete();
  }
  CHECK(bThrown);

  // Local files up and down.
  TCHAR szPath[MAX_PATH];
  ::GetModuleFileName(NULL, szPath, MAX_PATH);
  CString strExe = szPath;
  CString strDirLocal = strExe.Left(strExe.ReverseFind('\\') + 1);
  CString strUp = strDirLocal + _T("inetup.txt"), strDown = strDirLocal + _T("inetdown.txt");
  {
    CFile f(strUp, CFile::modeCreate | CFile::modeWrite);
    f.Write("uploaded", 8);
  }
  CHECK(pFtp->PutFile(strUp, _T("b.txt")));
  CHECK(FtpData("/sub/b.txt") == "uploaded");
  ::DeleteFile(strDown);
  CHECK(pFtp->GetFile(_T("b.txt"), strDown, FALSE));
  {
    CFile f(strDown, CFile::modeRead);
    char buf[16] = {0};
    CHECK(f.Read(buf, sizeof(buf)) == 8 && memcmp(buf, "uploaded", 8) == 0);
  }
  ::DeleteFile(strUp);
  ::DeleteFile(strDown);

  // A listing.
  {
    CFtpFileFind finder(pFtp);
    BOOL bWorking = finder.FindFile(_T("*"));
    CHECK(bWorking);
    int nFound = 0;
    BOOL bSawA = FALSE;
    while (bWorking) {
      bWorking = finder.FindNextFile();
      nFound++;
      if (finder.GetFileName() == _T("a.txt")) {
        bSawA = TRUE;
        CHECK(finder.GetLength() == 25 && !finder.IsDirectory());
        CHECK(finder.GetFilePath() == _T("/sub/a.txt"));
        CString strFileURL;
        strFileURL.Format(_T("ftp://127.0.0.1:%u/sub/a.txt"), nFtpPort);
        CHECK(finder.GetFileURL() == strFileURL);
      }
    }
    CHECK(nFound == 2 && bSawA);
  }

  CHECK(pFtp->Rename(_T("b.txt"), _T("c.txt")));
  CHECK(FtpHas("/sub/c.txt") && !FtpHas("/sub/b.txt"));
  CHECK(pFtp->Remove(_T("c.txt")));
  CHECK(!pFtp->Remove(_T("c.txt")));
  CHECK(pFtp->SetCurrentDirectory(_T("/")));
  CHECK(!pFtp->RemoveDirectory(_T("sub")));  // a.txt is still there
  {
    CFtpFileFind finder(pFtp);
    BOOL bWorking = finder.FindFile();
    BOOL bSawSub = FALSE;
    while (bWorking) {
      bWorking = finder.FindNextFile();
      if (finder.GetFileName() == _T("sub")) bSawSub = finder.IsDirectory();
    }
    CHECK(bSawSub);
  }
  CHECK(pFtp->Remove(_T("/sub/a.txt")));
  CHECK(pFtp->RemoveDirectory(_T("sub")));
  CHECK(!DirHas("/sub"));
  pFtp->Close();
  delete pFtp;
  CHECK(g_nPassive > 0);

  // Active mode: the server connects back.
  int nActive = g_nActive;
  pFtp = session.GetFtpConnection(_T("127.0.0.1"), _T("user"), _T("pass"), nFtpPort, FALSE);
  pFile = pFtp->OpenFile(_T("readme.txt"));
  CHECK(ReadAllA(pFile) == "read me\r\n");
  delete pFile;
  delete pFtp;
  CHECK(g_nActive > nActive);

  // And through OpenURL.
  CString strReadme;
  strReadme.Format(_T("ftp://user:pass@127.0.0.1:%u/readme.txt"), nFtpPort);
  CStdioFile* pStd = session.OpenURL(strReadme, 1, INTERNET_FLAG_RELOAD | INTERNET_FLAG_PASSIVE);
  CHECK(pStd != NULL && pStd->IsKindOf(RUNTIME_CLASS(CInternetFile)) &&
        !pStd->IsKindOf(RUNTIME_CLASS(CHttpFile)));
  CHECK(ReadAllA((CInternetFile*)pStd) == "read me\r\n");
  delete pStd;

  // A wrong password.
  BOOL bThrown2 = FALSE;
  try {
    CFtpConnection* p = session.GetFtpConnection(_T("127.0.0.1"), _T("user"), _T("wrong"),
                                                 nFtpPort, TRUE);
    delete p;
  } catch (CInternetException* e) {
    bThrown2 = TRUE;
    e->Delete();
  }
  CHECK(bThrown2);
}

int main() {
  WSADATA wsa;
  ::WSAStartup(MAKEWORD(2, 2), &wsa);
  u_short nHttpPort, nFtpPort;
  g_hHttpListen = Listen(nHttpPort);
  g_hFtpListen = Listen(nFtpPort);
  HANDLE hHttp = ::CreateThread(NULL, 0, HttpThread, NULL, 0, NULL);
  HANDLE hFtp = ::CreateThread(NULL, 0, FtpThread, NULL, 0, NULL);

  TestParse();
  try {
    CTestSession session;
    CHECK((HINTERNET)session != NULL && session.GetContext() == 1);
    DWORD dwTimeout = 0;
    CHECK(session.QueryOption(INTERNET_OPTION_CONNECT_TIMEOUT, dwTimeout));
    CHECK(session.SetOption(INTERNET_OPTION_CONNECT_TIMEOUT, 5000));
    TestHttp(session, nHttpPort);
    TestFtp(session, nFtpPort);
    session.Close();
  } catch (CException* e) {
    TCHAR sz[256] = {0};
    e->GetErrorMessage(sz, 256);
    _tprintf(_T("FAIL: unexpected exception: %s\n"), sz);
    g_failures++;
    e->Delete();
  }

  g_bStop = 1;
  ::closesocket(g_hHttpListen);
  ::closesocket(g_hFtpListen);
  ::WaitForSingleObject(hHttp, 5000);
  ::WaitForSingleObject(hFtp, 5000);
  if (g_failures == 0) printf("PASS: all internet tests\n");
  else printf("%d failure(s)\n", g_failures);
  return g_failures ? 1 : 0;
}
