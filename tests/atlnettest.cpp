// atlnettest.cpp - ATL's mail and web clients against servers of the
// test's own on the loopback address:
//   - atlsocket.h's ZEvtSyncSocket: connecting, a refused port, a read
//     that times out;
//   - atlmime.h: a CMimeMessage with its headers (names and subjects
//     outside ASCII "B" encoded), text (quoted-printable outside ASCII),
//     attachments from memory and from a file in base64, uuencoding and
//     quoted-printable, a message attached whole, and WriteData;
//   - atlsmtpconnection.h: CSMTPConnection greeting the server, sending a
//     message (its envelope from the message and given), a line starting
//     with a dot, a recipient the server refuses, SendRaw and SendSimple;
//   - atlhttp.h: CAtlHttpClient getting bodies with a length, chunked and
//     up to the close over one kept connection, HEAD, POST with data sent
//     in blocks and chunked, redirects (absolute, relative, a 303 turning
//     a POST into a GET, and not followed), headers, Basic authentication
//     and a proxy.
// Exits with 1 on a failure.
#include <atlsocket.h>
#include <atlbase.h>
#include <atlstr.h>
#include <atlhttp.h>
#include <atlmime.h>
#include <atlsmtpconnection.h>
#include <stdio.h>
#include <string>
#include <vector>

static int g_nFailures;
#define CHECK(cond)                                  \
  do {                                               \
    if (!(cond)) {                                   \
      printf("FAIL line %d: %s\n", __LINE__, #cond); \
      g_nFailures++;                                 \
    }                                                \
  } while (0)

static CRITICAL_SECTION g_cs;

// ---------------------------------------------------------------------
// The servers: one thread listening, a thread for each connection.

struct Server {
  SOCKET listener;
  unsigned short port;
  DWORD(WINAPI* pfnConn)(LPVOID);
};

static SOCKET Listen(unsigned short* pPort) {
  SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  sockaddr_in sa;
  memset(&sa, 0, sizeof(sa));
  sa.sin_family = AF_INET;
  sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  sa.sin_port = 0;
  if (bind(s, (sockaddr*)&sa, sizeof(sa)) != 0 || listen(s, 8) != 0) return INVALID_SOCKET;
  int n = sizeof(sa);
  getsockname(s, (sockaddr*)&sa, &n);
  *pPort = ntohs(sa.sin_port);
  return s;
}

static DWORD WINAPI AcceptThread(LPVOID pv) {
  Server* p = (Server*)pv;
  for (;;) {
    SOCKET c = accept(p->listener, NULL, NULL);
    if (c == INVALID_SOCKET) break;
    HANDLE h = CreateThread(NULL, 0, p->pfnConn, (LPVOID)(UINT_PTR)c, 0, NULL);
    if (h != NULL) CloseHandle(h);
  }
  return 0;
}

static bool SendAll(SOCKET s, const std::string& str) {
  size_t n = 0;
  while (n < str.size()) {
    int r = send(s, str.data() + n, (int)(str.size() - n), 0);
    if (r <= 0) return false;
    n += (size_t)r;
  }
  return true;
}

// Bytes onto buf; false at the close.
static bool RecvMore(SOCKET s, std::string& buf) {
  char tmp[2048];
  int r = recv(s, tmp, sizeof(tmp), 0);
  if (r <= 0) return false;
  buf.append(tmp, (size_t)r);
  return true;
}

// ----- HTTP

struct HttpRequest {
  std::string line, headers, body;
};
static HttpRequest g_lastRequest;
static int g_nHttpConnections;

static std::string HeaderOf(const HttpRequest& r, const char* name) {
  std::string h = "\r\n" + r.headers;
  std::string lower = h;
  for (char& ch : lower) ch = (char)tolower((unsigned char)ch);
  std::string key = std::string("\r\n") + name + ":";
  for (char& ch : key) ch = (char)tolower((unsigned char)ch);
  size_t i = lower.find(key);
  if (i == std::string::npos) return "";
  i += key.size();
  while (i < h.size() && h[i] == ' ') i++;
  size_t e = h.find("\r\n", i);
  return h.substr(i, e - i);
}

static bool ReadRequest(SOCKET s, std::string& buf, HttpRequest& r) {
  size_t e;
  while ((e = buf.find("\r\n\r\n")) == std::string::npos)
    if (!RecvMore(s, buf)) return false;
  std::string head = buf.substr(0, e + 2);
  buf.erase(0, e + 4);
  size_t nl = head.find("\r\n");
  r.line = head.substr(0, nl);
  r.headers = head.substr(nl + 2);
  r.body.clear();
  if (HeaderOf(r, "Transfer-Encoding") == "chunked") {
    for (;;) {
      size_t eol;
      while ((eol = buf.find("\r\n")) == std::string::npos)
        if (!RecvMore(s, buf)) return false;
      size_t n = strtoul(buf.substr(0, eol).c_str(), NULL, 16);
      buf.erase(0, eol + 2);
      while (buf.size() < n + 2)
        if (!RecvMore(s, buf)) return false;
      r.body += buf.substr(0, n);
      buf.erase(0, n + 2);
      if (n == 0) break;
    }
  } else {
    size_t n = strtoul(HeaderOf(r, "Content-Length").c_str(), NULL, 10);
    while (buf.size() < n)
      if (!RecvMore(s, buf)) return false;
    r.body = buf.substr(0, n);
    buf.erase(0, n);
  }
  return true;
}

static std::string Response(int status, const char* reason, const std::string& extra,
                            const std::string& body) {
  char sz[64];
  snprintf(sz, sizeof(sz), "HTTP/1.1 %d %s\r\n", status, reason);
  std::string str = sz;
  str += extra;
  snprintf(sz, sizeof(sz), "Content-Length: %u\r\n\r\n", (unsigned)body.size());
  return str + sz + body;
}

static DWORD WINAPI HttpConn(LPVOID pv) {
  SOCKET s = (SOCKET)(UINT_PTR)pv;
  EnterCriticalSection(&g_cs);
  g_nHttpConnections++;
  LeaveCriticalSection(&g_cs);
  std::string buf;
  HttpRequest r;
  while (ReadRequest(s, buf, r)) {
    EnterCriticalSection(&g_cs);
    g_lastRequest = r;
    LeaveCriticalSection(&g_cs);
    size_t sp1 = r.line.find(' '), sp2 = r.line.rfind(' ');
    std::string method = r.line.substr(0, sp1);
    std::string target = r.line.substr(sp1 + 1, sp2 - sp1 - 1);
    // Through the proxy, the whole URL.
    if (target.compare(0, 7, "http://") == 0) target = target.substr(target.find('/', 7));
    std::string path = target.substr(0, target.find('?'));
    bool bClose = false;
    std::string out;
    if (path == "/hello") {
      out = Response(200, "OK", "Content-Type: text/plain\r\nX-Test: one\r\nX-Test: two\r\n",
                     method == "HEAD" ? "" : "Hello, world");
      if (method == "HEAD") {
        out = "HTTP/1.1 200 OK\r\nContent-Length: 12\r\n\r\n";
      }
    } else if (path == "/chunked") {
      out = "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n3\r\nabc\r\n4;x=y\r\ndefg\r\n0\r\nX-Trailer: t\r\n\r\n";
    } else if (path == "/close") {
      out = "HTTP/1.0 200 OK\r\nContent-Type: text/plain\r\n\r\nall of it, then the close";
      bClose = true;
    } else if (path == "/continue") {
      out = "HTTP/1.1 100 Continue\r\n\r\n" + Response(200, "OK", "", "after 100");
    } else if (path == "/redirect") {
      out = Response(302, "Found", "Location: http://127.0.0.1:" + HeaderOf(r, "Host").substr(10) + "/hello\r\n", "moved");
    } else if (path == "/dir/rel") {
      out = Response(301, "Moved", "Location: echo?from=rel\r\n", "");
    } else if (path == "/see") {
      out = Response(303, "See Other", "Location: /dir/echo\r\n", "");
    } else if (path == "/dir/echo") {
      out = Response(200, "OK", "", method + " " + target + " " + r.body);
    } else if (path == "/secret") {
      // ada:lovelace
      if (HeaderOf(r, "Authorization") == "Basic YWRhOmxvdmVsYWNl")
        out = Response(200, "OK", "", "welcome");
      else
        out = Response(401, "Unauthorized",
                       "WWW-Authenticate: Digest realm=\"x\", nonce=\"1\", Basic realm=\"Test, Inc\"\r\n",
                       "no");
    } else {
      out = Response(404, "Not Found", "", "none");
    }
    if (!SendAll(s, out) || bClose) break;
  }
  shutdown(s, SD_BOTH);
  closesocket(s);
  return 0;
}

// ----- SMTP

struct Mail {
  std::string from;
  std::vector<std::string> to;
  std::string data;
};
static std::vector<Mail> g_mail;
static std::string g_strHello;
static bool g_bQuit;

static DWORD WINAPI SmtpConn(LPVOID pv) {
  SOCKET s = (SOCKET)(UINT_PTR)pv;
  std::string buf;
  Mail mail;
  SendAll(s, "220 test ESMTP ready\r\n");
  for (;;) {
    size_t e;
    while ((e = buf.find("\r\n")) == std::string::npos)
      if (!RecvMore(s, buf)) goto done;
    std::string line = buf.substr(0, e);
    buf.erase(0, e + 2);
    std::string verb = line.substr(0, 4);
    if (verb == "EHLO") {
      EnterCriticalSection(&g_cs);
      g_strHello = line;
      LeaveCriticalSection(&g_cs);
      SendAll(s, "250-test greets you\r\n250-SIZE 100000\r\n250 HELP\r\n");
    } else if (verb == "MAIL") {
      mail = Mail();
      mail.from = line.substr(10);
      SendAll(s, "250 OK\r\n");
    } else if (verb == "RCPT") {
      std::string to = line.substr(8);
      if (to.find("nobody@") != std::string::npos) {
        SendAll(s, "550 no such user\r\n");
      } else {
        mail.to.push_back(to);
        SendAll(s, "250 OK\r\n");
      }
    } else if (verb == "DATA") {
      SendAll(s, "354 go ahead\r\n");
      while ((e = buf.find("\r\n.\r\n")) == std::string::npos)
        if (!RecvMore(s, buf)) goto done;
      mail.data = buf.substr(0, e + 2);
      buf.erase(0, e + 5);
      EnterCriticalSection(&g_cs);
      g_mail.push_back(mail);
      LeaveCriticalSection(&g_cs);
      SendAll(s, "250 queued\r\n");
    } else if (verb == "RSET") {
      SendAll(s, "250 OK\r\n");
    } else if (verb == "QUIT") {
      EnterCriticalSection(&g_cs);
      g_bQuit = true;
      LeaveCriticalSection(&g_cs);
      SendAll(s, "221 bye\r\n");
      break;
    } else {
      SendAll(s, "500 what\r\n");
    }
  }
done:
  shutdown(s, SD_BOTH);
  closesocket(s);
  return 0;
}

// A server that never says anything.
static DWORD WINAPI SilentConn(LPVOID pv) {
  SOCKET s = (SOCKET)(UINT_PTR)pv;
  Sleep(1500);
  closesocket(s);
  return 0;
}

static unsigned short Start(Server* p, DWORD(WINAPI* pfn)(LPVOID)) {
  p->listener = Listen(&p->port);
  p->pfnConn = pfn;
  HANDLE h = CreateThread(NULL, 0, AcceptThread, p, 0, NULL);
  if (h != NULL) CloseHandle(h);
  return p->port;
}

static Server g_http, g_smtp, g_silent;

static bool Has(const std::string& s, const char* sz) { return s.find(sz) != std::string::npos; }
static bool Has(const CStringA& s, const char* sz) { return s.Find(sz) >= 0; }

// ---------------------------------------------------------------------

static void TestSocket() {
  ZEvtSyncSocket sock;
  CHECK(sock.GetSocketTimeout() == ATL_SOCK_TIMEOUT);
  CHECK(sock.Connect(_T("127.0.0.1"), g_silent.port));
  CHECK(sock.SetSocketTimeout(200) == ATL_SOCK_TIMEOUT);
  BYTE buf[16];
  DWORD dw = sizeof(buf);
  DWORD t = GetTickCount();
  CHECK(!sock.Read(buf, &dw) && sock.GetLastError() == WSAETIMEDOUT && dw == 0);
  CHECK(GetTickCount() - t < 1200);
  sock.Close();
  CHECK((SOCKET)sock == INVALID_SOCKET);

  // A port nothing listens on: the silent server's, once it is closed.
  unsigned short nPort = 0;
  SOCKET s = Listen(&nPort);
  closesocket(s);
  ZEvtSyncSocket sock2;
  sock2.SetSocketTimeout(2000);
  CHECK(!sock2.Connect(_T("127.0.0.1"), nPort));
  CHECK(sock2.SupportsScheme(ATL_URL_SCHEME_HTTP) && !sock2.SupportsScheme(ATL_URL_SCHEME_HTTPS));
}

static void TestMime() {
  CMimeMessage msg;
  CHECK(msg.SetSender(_T("ada@example.com")));
  CHECK(msg.SetSenderName(_T("Ada Lovelace")));
  CHECK(msg.AddRecipient(_T("charles@example.com"), _T("Charles Babbage")));
  CHECK(msg.AddCc(_T("mary@example.com")));
  CHECK(msg.AddBcc(_T("hidden@example.com")));
  CHECK(!msg.AddRecipient(_T("")));
  CHECK(msg.SetSubject(_T("Notes on the engine")));
  CHECK(msg.SetPriority(ATL_MIME_HIGH_PRIORITY) && !msg.SetPriority((ATL_MIME_PRIORITY)2));
  CHECK(msg.GetPriority() == ATL_MIME_HIGH_PRIORITY);
  CHECK(msg.AddText(_T("The engine weaves algebraic patterns.\n.A line with a dot.\n")));
  static const BYTE kData[] = {0, 1, 2, 3, 'h', 'i', 0xFF};
  CHECK(msg.AttachRaw((void*)kData, sizeof(kData), ATLSMTP_BASE64_ENCODE, TRUE, _T("data.bin")));
  CHECK(msg.AttachRaw((void*)"uu text", 7, ATLSMTP_UUENCODE, TRUE, _T("uu.txt"), _T("text/plain")));
  CHECK(!msg.AttachFile(_T("no-such-file.txt")));

  DWORD dwLen = msg.GetRequiredRecipientsStringLength();
  CStringA strRecip;
  CHECK(msg.GetRecipientsString(strRecip.GetBuffer((int)dwLen), &dwLen));
  strRecip.ReleaseBuffer();
  CHECK(strRecip == "charles@example.com,mary@example.com,hidden@example.com");
  DWORD dwSmall = 3;
  char szSmall[3];
  CHECK(!msg.GetRecipientsString(szSmall, &dwSmall) && dwSmall == (DWORD)strRecip.GetLength() + 1);

  CStringA str;
  CHECK(msg.MakeMimeString(str, NULL));
  CHECK(Has(str, "From: \"Ada Lovelace\" <ada@example.com>\r\n"));
  CHECK(Has(str, "To: \"Charles Babbage\" <charles@example.com>\r\n"));
  CHECK(Has(str, "Cc: mary@example.com\r\n"));
  CHECK(!Has(str, "hidden@"));
  CHECK(Has(str, "Subject: Notes on the engine\r\n"));
  CHECK(Has(str, "X-Priority: 1 (Highest)\r\n"));
  CHECK(Has(str, "MIME-Version: 1.0\r\n"));
  CHECK(Has(str, "\r\nDate: ") || str.Left(6) == "Date: ");
  CHECK(Has(str, "Content-Type: multipart/mixed;\r\n\tboundary=\""));
  // The text, its lines ending in CRLF.
  CHECK(Has(str, "Content-Transfer-Encoding: 7bit\r\n\r\nThe engine weaves algebraic patterns.\r\n.A line with a dot.\r\n"));
  // The attachments.
  CHECK(Has(str, "name=\"data.bin\""));
  CHECK(Has(str, "Content-Transfer-Encoding: base64\r\n"));
  CHECK(Has(str, "AAECA2hp/w=="));
  CHECK(Has(str, "Content-Type: text/plain;\r\n\tname=\"uu.txt\""));
  CHECK(Has(str, "Content-Transfer-Encoding: x-uuencode\r\n"));
  CHECK(Has(str, "begin 666 uu.txt"));
  // The boundary between each part, and once more at the end.
  int nB = str.Find("boundary=\"") + 10;
  CStringA strBoundary = str.Mid(nB, str.Find('"', nB) - nB);
  CHECK(strBoundary.GetLength() > 10);
  int nCount = 0;
  for (int i = str.Find("--" + strBoundary); i >= 0; i = str.Find("--" + strBoundary, i + 1)) nCount++;
  CHECK(nCount == 4);
  CHECK(Has(str, "\r\n--" + strBoundary + "--\r\n"));

  // Text outside ASCII, in UTF-8.
  CMimeMessage msg2;
  msg2.SetSender(_T("ada@example.com"));
#ifdef _UNICODE
  CHECK(msg2.SetSubject(L"Caf\x00e9", CP_UTF8));
  CHECK(msg2.AddRecipient(_T("bob@example.com"), L"B\x00f6" L"b", CP_UTF8));
  CHECK(msg2.AddText(L"na\x00efve\n", -1, 1, CP_UTF8));
#else
  CHECK(msg2.SetSubject("Caf\xc3\xa9", CP_UTF8));
  CHECK(msg2.AddRecipient(_T("bob@example.com"), "B\xc3\xb6" "b", CP_UTF8));
  CHECK(msg2.AddText("na\xc3\xaf" "ve\n", -1, 1, CP_UTF8));
#endif
  CStringA str2;
  CHECK(msg2.MakeMimeString(str2, NULL));
  CHECK(Has(str2, "Subject: =?utf-8?B?Q2Fmw6k=?=\r\n"));
  CHECK(Has(str2, "To: \"=?utf-8?B?QsO2Yg==?=\" <bob@example.com>"));
  CHECK(Has(str2, "charset=\"utf-8\"\r\nContent-Transfer-Encoding: quoted-printable\r\n\r\nna=C3=AFve"));

  // An attachment from a file, quoted-printable, and a message attached
  // whole.
  TCHAR szDir[MAX_PATH], szFile[MAX_PATH];
  GetTempPath(MAX_PATH, szDir);
  GetTempFileName(szDir, _T("mim"), 0, szFile);
  HANDLE h = CreateFile(szFile, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
  DWORD dw = 0;
  WriteFile(h, "x=1\r\n", 5, &dw, NULL);
  CloseHandle(h);
  CMimeMessage msg3;
  msg3.SetSender(_T("ada@example.com"));
  msg3.AddRecipient(_T("charles@example.com"));
  msg3.SetSubject(_T("With a file"));
  CHECK(msg3.AttachFile(szFile, _T("settings.ini"), _T("text/plain"), ATLSMTP_QP_ENCODE));
  msg2.SetDisplayName(_T("forwarded.eml"));
  CHECK(msg3.AttachMessage(&msg2));
  CStringA str3;
  CHECK(msg3.MakeMimeString(str3, NULL));
  CHECK(Has(str3, "filename=\"settings.ini\""));
  CHECK(Has(str3, "Content-Transfer-Encoding: quoted-printable\r\n"));
  CHECK(Has(str3, "\r\n\r\nx=3D1\r\n"));
  CHECK(Has(str3, "Content-Type: message/rfc822;\r\n\tname=\"forwarded.eml\""));
  CHECK(Has(str3, "Subject: =?utf-8?B?Q2Fmw6k=?="));
  // A copy keeps its parts.
  CMimeMessage msg4(msg3);
  CHECK(msg4.GetPartCount() == 2);
  msg3.RemoveParts();
  CStringA str4;
  CHECK(msg4.MakeMimeString(str4, NULL) && Has(str4, "settings.ini"));

  // WriteData to a file.
  h = CreateFile(szFile, GENERIC_READ | GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
  CHECK(msg.WriteData(h, NULL, NULL));
  DWORD dwSize = GetFileSize(h, NULL);
  CHECK(dwSize > 200);
  SetFilePointer(h, 0, NULL, FILE_BEGIN);
  CStringA strFile;
  ReadFile(h, strFile.GetBuffer((int)dwSize), dwSize, &dw, NULL);
  strFile.ReleaseBuffer((int)dw);
  CloseHandle(h);
  DeleteFile(szFile);
  CHECK(Has(strFile, "Subject: Notes on the engine\r\n") && Has(strFile, "AAECA2hp/w=="));
}

static void TestSmtp() {
  CString strHost;
  strHost.Format(_T("127.0.0.1:%u"), (unsigned)g_smtp.port);
  CSMTPConnection conn;
  CHECK(!conn.Connected());
  CHECK(conn.Connect(strHost));
  CHECK(conn.Connected());
  CHECK(!conn.Connect(strHost));
  EnterCriticalSection(&g_cs);
  CHECK(g_strHello.compare(0, 5, "EHLO ") == 0 && g_strHello.size() > 5);
  LeaveCriticalSection(&g_cs);

  CMimeMessage msg;
  msg.SetSender(_T("ada@example.com"));
  msg.SetSenderName(_T("Ada"));
  msg.AddRecipient(_T("charles@example.com"), _T("Charles"));
  msg.AddBcc(_T("hidden@example.com"));
  msg.SetSubject(_T("By mail"));
  msg.AddText(_T("first\r\n.dot line\r\n..two dots\r\nlast"));
  CHECK(conn.SendMessage(msg));
  EnterCriticalSection(&g_cs);
  CHECK(g_mail.size() == 1);
  if (g_mail.size() == 1) {
    const Mail& m = g_mail[0];
    CHECK(m.from == "<ada@example.com>");
    CHECK(m.to.size() == 2 && m.to[0] == "<charles@example.com>" && m.to[1] == "<hidden@example.com>");
    CHECK(Has(m.data, "Subject: By mail\r\n"));
    CHECK(Has(m.data, "\r\nfirst\r\n..dot line\r\n...two dots\r\nlast\r\n"));
    CHECK(!Has(m.data, "hidden"));
  }
  LeaveCriticalSection(&g_cs);

  // The envelope given, one recipient refused.
  CHECK(!conn.SendMessage(msg, _T("nobody@example.com"), _T("other@example.com")));
  CHECK(conn.GetLastReplyCode() == 250);  // the RSET after the 550
  CHECK(conn.SendMessage(msg, _T("x@example.com; \"Y\" <y@example.com>"), _T("other@example.com")));
  CHECK(conn.SendRaw(_T("Subject: raw\r\n\r\nraw body\r\n"), 26, _T("z@example.com"), _T("ada@example.com")));
  CHECK(conn.SendSimple(_T("z@example.com"), _T("ada@example.com"), _T("Simple"), _T("simple body")));
  EnterCriticalSection(&g_cs);
  CHECK(g_mail.size() == 4);
  if (g_mail.size() == 4) {
    CHECK(g_mail[1].from == "<other@example.com>");
    CHECK(g_mail[1].to.size() == 2 && g_mail[1].to[1] == "<y@example.com>");
    CHECK(g_mail[2].data == "Subject: raw\r\n\r\nraw body\r\n");
    CHECK(Has(g_mail[3].data, "Subject: Simple\r\n") && Has(g_mail[3].data, "simple body"));
  }
  LeaveCriticalSection(&g_cs);
  CHECK(conn.Disconnect());
  CHECK(!conn.Connected());
  EnterCriticalSection(&g_cs);
  CHECK(g_bQuit);
  LeaveCriticalSection(&g_cs);
  CHECK(!conn.SendSimple(_T("z@example.com"), _T("ada@example.com"), _T("x"), _T("y")));
}

static bool WINAPI OnSend(DWORD dwSent, DWORD_PTR dwParam) {
  std::vector<DWORD>* p = (std::vector<DWORD>*)dwParam;
  p->push_back(dwSent);
  return true;
}
static int g_nChunk;
static bool WINAPI OnChunk(BYTE** ppData, DWORD* pdwSize, DWORD_PTR /*dwParam*/) {
  static BYTE a[] = "part one,", b[] = "part two";
  if (g_nChunk == 0) {
    *ppData = a;
    *pdwSize = 9;
  } else if (g_nChunk == 1) {
    *ppData = b;
    *pdwSize = 8;
  } else {
    *pdwSize = 0;
  }
  g_nChunk++;
  return true;
}

class CTestAuthInfo : public IAuthInfo {
public:
  HRESULT GetPassword(LPTSTR sz, DWORD* pdw) override { return Copy(_T("lovelace"), sz, pdw); }
  HRESULT GetUsername(LPTSTR sz, DWORD* pdw) override { return Copy(_T("ada"), sz, pdw); }
  HRESULT GetDomain(LPTSTR sz, DWORD* pdw) override { return Copy(_T(""), sz, pdw); }

private:
  static HRESULT Copy(LPCTSTR psz, LPTSTR sz, DWORD* pdw) {
    DWORD n = (DWORD)_tcslen(psz);
    if (sz == NULL || *pdw < n + 1) {
      *pdw = n + 1;
      return E_OUTOFMEMORY;
    }
    _tcscpy(sz, psz);
    *pdw = n;
    return S_OK;
  }
};

static HttpRequest LastRequest() {
  EnterCriticalSection(&g_cs);
  HttpRequest r = g_lastRequest;
  LeaveCriticalSection(&g_cs);
  return r;
}
static std::string Body(CAtlHttpClient& c) {
  return c.GetBody() != NULL ? std::string((const char*)c.GetBody(), c.GetBodyLength()) : std::string();
}

static void TestHttp() {
  CString strBase;
  strBase.Format(_T("http://127.0.0.1:%u"), (unsigned)g_http.port);
  CAtlHttpClient client;
  CHECK(client.GetStatus() == ATL_INVALID_STATUS);

  // A body with a length.
  CHECK(client.Navigate(strBase + _T("/hello?a=b#frag")));
  CHECK(client.GetStatus() == 200);
  CHECK(Body(client) == "Hello, world");
  CHECK(client.GetContentLength() == 12);
  HttpRequest r = LastRequest();
  CHECK(r.line == "GET /hello?a=b HTTP/1.1");
  CHECK(Has(HeaderOf(r, "User-Agent"), "ATL"));
  char szHost[32];
  snprintf(szHost, sizeof(szHost), "127.0.0.1:%u", (unsigned)g_http.port);
  CHECK(HeaderOf(r, "Host") == szHost);
  CString strValue;
  CHECK(client.GetHeaderValue(_T("content-type"), strValue) && strValue == _T("text/plain"));
  CHECK(client.GetHeaderValue(_T("X-Test"), strValue) && strValue == _T("one, two"));
  CHECK(!client.GetHeaderValue(_T("X-None"), strValue));
  TCHAR szBuf[4];
  DWORD dwBuf = 4;
  CHECK(!client.GetHeaderValue("Content-Type", szBuf, &dwBuf) && dwBuf == 11);
  CHECK(client.GetRawResponseHeaderLength() > 0 &&
        strncmp(client.GetRawResponseHeader(), "HTTP/1.1 200 OK\r\n", 17) == 0);
  CHECK(client.GetResponseLength() == client.GetRawResponseHeaderLength() + 12);
  CHECK(_tcscmp(client.GetCurrentUrl()->GetUrlPath(), _T("/hello")) == 0);

  // Chunked, and a 100 first, over the same connection.
  CHECK(client.Navigate(strBase + _T("/chunked")) && client.GetStatus() == 200);
  CHECK(Body(client) == "abcdefg");
  CHECK(client.Navigate(strBase + _T("/continue")) && client.GetStatus() == 200);
  CHECK(Body(client) == "after 100");
  EnterCriticalSection(&g_cs);
  CHECK(g_nHttpConnections == 1);
  LeaveCriticalSection(&g_cs);

  // Up to the close; the next request opens a new connection.
  CHECK(client.Navigate(strBase + _T("/close")) && client.GetStatus() == 200);
  CHECK(Body(client) == "all of it, then the close");
  CAtlNavigateData nav;
  nav.SetMethod(ATL_HTTP_METHOD_HEAD);
  CHECK(client.Navigate(strBase + _T("/hello"), &nav) && client.GetStatus() == 200);
  CHECK(client.GetBodyLength() == 0 && client.GetContentLength() == 12);
  EnterCriticalSection(&g_cs);
  CHECK(g_nHttpConnections == 2);
  LeaveCriticalSection(&g_cs);
  CHECK(client.Navigate(strBase + _T("/nothing")) && client.GetStatus() == 404);

  // POST, its data in blocks with the send callback.
  BYTE data[] = "name=ada&engine=analytical";
  std::vector<DWORD> sent;
  CAtlNavigateData post;
  post.SetMethod(ATL_HTTP_METHOD_POST);
  post.SetPostData(data, 26, _T("application/x-www-form-urlencoded"));
  post.SetFlags(post.GetFlags() | ATL_HTTP_FLAG_SEND_BLOCKS | ATL_HTTP_FLAG_SEND_CALLBACK);
  post.SetSendBlockSize(10);
  post.SetSendStatusCallback(OnSend, (DWORD_PTR)&sent);
  CHECK(client.Navigate(strBase + _T("/dir/echo"), &post) && client.GetStatus() == 200);
  CHECK(Body(client) == "POST /dir/echo name=ada&engine=analytical");
  r = LastRequest();
  CHECK(HeaderOf(r, "Content-Type") == "application/x-www-form-urlencoded");
  CHECK(HeaderOf(r, "Content-Length") == "26");
  CHECK(sent.size() == 3 && sent[0] == 10 && sent[2] == 26);

  // Chunked out.
  CAtlNavigateData chunked;
  chunked.SetMethod(ATL_HTTP_METHOD_PUT);
  chunked.SetChunkCallback(OnChunk, 0);
  CHECK(client.NavigateChunked(strBase + _T("/dir/echo"), &chunked) && client.GetStatus() == 200);
  CHECK(Body(client) == "PUT /dir/echo part one,part two");
  CHECK(HeaderOf(LastRequest(), "Transfer-Encoding") == "chunked");

  // Redirects: absolute, relative, a 303 turning the POST into a GET.
  CHECK(client.Navigate(strBase + _T("/redirect")) && client.GetStatus() == 200);
  CHECK(Body(client) == "Hello, world");
  CHECK(_tcscmp(client.GetCurrentUrl()->GetUrlPath(), _T("/hello")) == 0);
  CHECK(client.Navigate(strBase + _T("/dir/rel")) && client.GetStatus() == 200);
  CHECK(Body(client) == "GET /dir/echo?from=rel ");
  CHECK(client.Navigate(strBase + _T("/see"), &post) && client.GetStatus() == 200);
  CHECK(Body(client) == "GET /dir/echo ");
  CHECK(LastRequest().headers.find("Content-Length") == std::string::npos);
  CAtlNavigateData noRedirect;
  noRedirect.SetFlags(ATL_HTTP_FLAG_PROCESS_RESULT);
  CHECK(client.Navigate(strBase + _T("/redirect"), &noRedirect) && client.GetStatus() == 302);
  CHECK(Body(client) == "moved");
  CHECK(client.GetHeaderValue(_T("Location"), strValue) && strValue.Right(6) == _T("/hello"));

  // Basic authentication, offered after another scheme.
  CHECK(client.Navigate(strBase + _T("/secret")) && client.GetStatus() == 401);
  CTestAuthInfo info;
  CBasicAuthObject basic;
  CHECK(client.AddAuthObj(_T("Basic"), &basic, &info));
  CHECK(client.FindAuthObject(_T("basic")) == &basic);
  CHECK(client.Navigate(strBase + _T("/secret")) && client.GetStatus() == 200);
  CHECK(Body(client) == "welcome");
  CHECK(_tcscmp(basic.GetRealm(), _T("Test, Inc")) == 0);
  CHECK(client.RemoveAuthObj(_T("Basic")) && client.FindAuthObject(_T("Basic")) == NULL);

  // Through a proxy: the server is the proxy, asked for the whole URL.
  CAtlHttpClient proxied;
  CHECK(proxied.SetProxy(_T("127.0.0.1"), (short)g_http.port));
  CHECK(proxied.GetProxy() != NULL);
  CHECK(proxied.Navigate(_T("http://www.example.invalid/hello")) && proxied.GetStatus() == 200);
  r = LastRequest();
  CHECK(r.line == "GET http://www.example.invalid/hello HTTP/1.1");
  CHECK(HeaderOf(r, "Host") == "www.example.invalid");
  proxied.RemoveProxy();
  CHECK(proxied.GetProxy() == NULL);

  // Navigate(server, path) and a server that is not there.
  CAtlNavigateData port;
  port.SetPort((short)g_http.port);
  CHECK(proxied.Navigate(_T("127.0.0.1"), _T("/dir/echo?q=1"), &port) && proxied.GetStatus() == 200);
  CHECK(Body(proxied) == "GET /dir/echo?q=1 ");
  CHECK(!proxied.Navigate(_T("ftp://127.0.0.1/x")));
  unsigned short nDead = 0;
  closesocket(Listen(&nDead));
  CString strDead;
  strDead.Format(_T("http://127.0.0.1:%u/"), (unsigned)nDead);
  CHECK(!proxied.Navigate(strDead));
}

int main() {
  WSADATA wsa;
  if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
    printf("FAIL WSAStartup\n");
    return 1;
  }
  InitializeCriticalSection(&g_cs);
  CHECK(Start(&g_http, HttpConn) != 0);
  CHECK(Start(&g_smtp, SmtpConn) != 0);
  CHECK(Start(&g_silent, SilentConn) != 0);
  TestSocket();
  TestMime();
  TestSmtp();
  TestHttp();
  if (g_nFailures == 0) printf("ALL PASSED\n");
  fflush(stdout);
  // The server threads are left blocked in accept.
  ExitProcess(g_nFailures != 0);
}
