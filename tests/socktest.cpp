// socktest.cpp - console tests for afxsock.h over the loopback interface:
// event-driven CAsyncSocket connections, blocking CSocket calls with a
// server on another thread, CArchive through a CSocketFile, datagrams,
// errors and cancelling a blocking call, and IPv6 where the system has it.
#include <afx.h>
#include <afxwin.h>
#include <afxsock.h>
#include <stdio.h>

static int g_failures = 0;
#define CHECK(cond)                                                   \
  do {                                                                \
    if (!(cond)) {                                                    \
      printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);          \
      g_failures++;                                                   \
    }                                                                 \
  } while (0)

// Runs the thread's messages until *pbDone is set or the time is up.
static BOOL PumpUntil(const BOOL* pbDone, DWORD dwTimeout = 10000) {
  DWORD dwStart = ::GetTickCount();
  while (!*pbDone) {
    MSG msg;
    while (::PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
      ::TranslateMessage(&msg);
      ::DispatchMessage(&msg);
    }
    if (*pbDone) break;
    if (::GetTickCount() - dwStart > dwTimeout) return FALSE;
    ::MsgWaitForMultipleObjects(0, NULL, FALSE, 50, QS_ALLINPUT);
  }
  return TRUE;
}

// ---------------------------------------------------------------------
// Event-driven echo with CAsyncSocket.

class CEchoConn : public CAsyncSocket {
public:
  int m_nReceived = 0;
  BOOL m_bClosed = FALSE;
  virtual void OnReceive(int nErrorCode) {
    CHECK(nErrorCode == 0);
    char buf[256];
    int n = Receive(buf, sizeof(buf));
    if (n > 0) {
      m_nReceived += n;
      Send(buf, n);
    }
  }
  virtual void OnClose(int) { m_bClosed = TRUE; }
};

class CEchoListener : public CAsyncSocket {
public:
  CEchoConn m_conn;
  int m_nAccepts = 0;
  CString m_strPeer;
  virtual void OnAccept(int nErrorCode) {
    CHECK(nErrorCode == 0);
    if (Accept(m_conn)) {
      m_nAccepts++;
      UINT nPort = 0;
      CHECK(m_conn.GetPeerName(m_strPeer, nPort));
      CHECK(nPort != 0);
    }
  }
};

class CEchoClient : public CAsyncSocket {
public:
  BOOL m_bConnected = FALSE;
  BOOL m_bDone = FALSE;
  int m_nSends = 0;
  CStringA m_strReply;
  virtual void OnConnect(int nErrorCode) {
    CHECK(nErrorCode == 0);
    m_bConnected = nErrorCode == 0;
    if (m_bConnected) Send("hello, socket", 13);
  }
  virtual void OnSend(int) { m_nSends++; }
  virtual void OnReceive(int nErrorCode) {
    CHECK(nErrorCode == 0);
    char buf[256];
    int n = Receive(buf, sizeof(buf));
    if (n > 0) m_strReply += CStringA(buf, n);
    if (m_strReply.GetLength() >= 13) m_bDone = TRUE;
  }
};

static void TestAsync() {
  CEchoListener listener;
  CHECK(listener.Create(0, SOCK_STREAM, FD_ACCEPT | FD_READ | FD_WRITE | FD_CLOSE,
                        _T("127.0.0.1")));
  CHECK(listener.Listen());
  CString strAddr;
  UINT nPort = 0;
  CHECK(listener.GetSockName(strAddr, nPort));
  CHECK(strAddr == _T("127.0.0.1"));
  CHECK(nPort != 0);
  CHECK(CAsyncSocket::FromHandle(listener.m_hSocket) == &listener);

  CEchoClient client;
  CHECK(client.Create());
  BOOL bConnect = client.Connect(_T("127.0.0.1"), nPort);
  CHECK(bConnect || CAsyncSocket::GetLastError() == WSAEWOULDBLOCK);
  CHECK(PumpUntil(&client.m_bDone));
  CHECK(client.m_bConnected);
  CHECK(client.m_strReply == "hello, socket");
  CHECK(client.m_nSends >= 1);  // FD_WRITE follows the connection
  CHECK(listener.m_nAccepts == 1);
  CHECK(listener.m_strPeer == _T("127.0.0.1"));
  CHECK(listener.m_conn.m_nReceived == 13);

  // Closing one end tells the other.
  client.Close();
  CHECK(client.m_hSocket == INVALID_SOCKET);
  CHECK(PumpUntil(&listener.m_conn.m_bClosed));

  // Attach and Detach.
  SOCKET h = listener.Detach();
  CHECK(h != INVALID_SOCKET && listener.m_hSocket == INVALID_SOCKET);
  CHECK(CAsyncSocket::FromHandle(h) == NULL);
  CAsyncSocket other;
  CHECK(other.Attach(h, FD_ACCEPT));
  CHECK(CAsyncSocket::FromHandle(h) == &other);
}

// ---------------------------------------------------------------------
// Blocking CSocket calls, with the server on a thread of its own.

struct ServerParams {
  HANDLE hReady;
  UINT nPort;
  BOOL bOK;
  CString strName;
  int nNumber;
  double dValue;
};

static DWORD WINAPI ServerThread(LPVOID pv) {
  ServerParams* p = (ServerParams*)pv;
  CSocket listener;
  p->bOK = listener.Create(0, SOCK_STREAM, _T("127.0.0.1")) && listener.Listen();
  CString strAddr;
  listener.GetSockName(strAddr, p->nPort);
  ::SetEvent(p->hReady);
  if (!p->bOK) return 1;

  CSocket conn;
  if (!listener.Accept(conn)) {
    p->bOK = FALSE;
    return 1;
  }
  // Read a request through an archive and answer through another.
  CSocketFile file(&conn);
  CArchive arIn(&file, CArchive::load);
  CArchive arOut(&file, CArchive::store);
  arIn >> p->strName >> p->nNumber >> p->dValue;
  CString strReply;
  strReply.Format(_T("Hello, %s"), (LPCTSTR)p->strName);
  arOut << strReply << p->nNumber * 2;
  arOut.Flush();

  // Then a plain stream: echo until the client stops sending.
  char buf[4096];
  int n;
  while ((n = conn.Receive(buf, sizeof(buf))) > 0) conn.Send(buf, n);
  arIn.Abort();
  arOut.Abort();
  conn.Close();
  return 0;
}

static void TestBlocking() {
  ServerParams params;
  params.hReady = ::CreateEvent(NULL, TRUE, FALSE, NULL);
  params.nPort = 0;
  params.bOK = FALSE;
  params.nNumber = 0;
  params.dValue = 0;
  DWORD dwThreadId;
  HANDLE hThread = ::CreateThread(NULL, 0, ServerThread, &params, 0, &dwThreadId);
  CHECK(hThread != NULL);
  ::WaitForSingleObject(params.hReady, 10000);
  CHECK(params.bOK && params.nPort != 0);

  CSocket client;
  CHECK(client.Create());
  CHECK(client.Connect(_T("127.0.0.1"), params.nPort));
  CHECK(!client.IsBlocking());
  {
    CSocketFile file(&client);
    CArchive arOut(&file, CArchive::store);
    CArchive arIn(&file, CArchive::load);
    arOut << CString(_T("archive")) << 21 << 2.5;
    arOut.Flush();
    CString strReply;
    int nTwice = 0;
    arIn >> strReply >> nTwice;
    CHECK(strReply == _T("Hello, archive"));
    CHECK(nTwice == 42);
    arIn.Abort();
    arOut.Abort();
  }
  CHECK(params.strName == _T("archive"));
  CHECK(params.dValue == 2.5);

  // A large send goes out whole even though the buffers fill up.
  const int nBig = 1 << 20;
  char* pSend = new char[nBig];
  char* pRecv = new char[nBig];
  for (int i = 0; i < nBig; i++) pSend[i] = (char)(i * 7 + (i >> 9));
  // Send in pieces while the echo comes back, keeping little in flight:
  // sending the whole megabyte first could fill the buffers both ways.
  int nSent = 0, nGot = 0;
  BOOL bOK = TRUE;
  while (nGot < nBig && bOK) {
    if (nSent < nBig && nSent - nGot < 32768) {
      int nChunk = nBig - nSent < 16384 ? nBig - nSent : 16384;
      bOK = client.Send(pSend + nSent, nChunk) == nChunk;
      nSent += nChunk;
    }
    int n = client.Receive(pRecv + nGot, nBig - nGot);
    if (n <= 0) bOK = FALSE;
    else nGot += n;
  }
  CHECK(bOK);
  CHECK(nGot == nBig);
  CHECK(memcmp(pSend, pRecv, nBig) == 0);
  delete[] pSend;
  delete[] pRecv;

  CHECK(client.ShutDown(CAsyncSocket::sends));
  char c;
  CHECK(client.Receive(&c, 1) == 0);  // the server closes after the echo ends
  client.Close();
  CHECK(::WaitForSingleObject(hThread, 10000) == WAIT_OBJECT_0);
  DWORD dwExit = 1;
  ::GetExitCodeThread(hThread, &dwExit);
  CHECK(dwExit == 0);
  ::CloseHandle(hThread);
  ::CloseHandle(params.hReady);
}

// ---------------------------------------------------------------------
// Datagrams, errors and cancelling.

static void TestDatagram() {
  CSocket receiver;
  CHECK(receiver.Create(0, SOCK_DGRAM, _T("127.0.0.1")));
  CString strAddr;
  UINT nPort = 0;
  CHECK(receiver.GetSockName(strAddr, nPort));

  CAsyncSocket sender;
  CHECK(sender.Create(0, SOCK_DGRAM, FD_READ | FD_WRITE));
  CHECK(sender.SendTo("datagram", 8, nPort, _T("127.0.0.1")) == 8);
  char buf[64];
  CString strFrom;
  UINT nFromPort = 0;
  int n = receiver.ReceiveFrom(buf, sizeof(buf), strFrom, nFromPort);
  CHECK(n == 8 && memcmp(buf, "datagram", 8) == 0);
  CHECK(strFrom == _T("127.0.0.1"));
  CString strSenderAddr;
  UINT nSenderPort = 0;
  CHECK(sender.GetSockName(strSenderAddr, nSenderPort));
  CHECK(nFromPort == nSenderPort);
}

class CCancelSocket : public CSocket {
public:
  int m_nPending = 0;
  virtual BOOL OnMessagePending() {
    if (++m_nPending == 2) CancelBlockingCall();
    return CSocket::OnMessagePending();
  }
};

static void TestErrors() {
  // Nothing listens on a port that was just freed.
  UINT nPort = 0;
  {
    CAsyncSocket probe;
    CHECK(probe.Create(0, SOCK_STREAM, 0, _T("127.0.0.1")));
    CString s;
    probe.GetSockName(s, nPort);
  }
  CSocket refused;
  CHECK(refused.Create());
  CHECK(!refused.Connect(_T("127.0.0.1"), nPort));
  CHECK(CAsyncSocket::GetLastError() == WSAECONNREFUSED);

  CAsyncSocket listener;
  CHECK(listener.Create(0, SOCK_STREAM, FD_ACCEPT, _T("127.0.0.1")));
  CHECK(listener.Listen());
  CString s;
  UINT nListenPort = 0;
  listener.GetSockName(s, nListenPort);

  // A port in use cannot be bound again; Create cleans up after itself.
  CAsyncSocket bad;
  CHECK(!bad.Create(nListenPort, SOCK_STREAM, FD_READ, _T("127.0.0.1")));
  CHECK(CAsyncSocket::GetLastError() == WSAEADDRINUSE);
  CHECK(bad.m_hSocket == INVALID_SOCKET);

  // A receive that nothing answers, cancelled from OnMessagePending.
  CCancelSocket client;
  client.m_nTimeOut = 50;
  CHECK(client.Create());
  CHECK(client.Connect(_T("127.0.0.1"), nListenPort));
  char c;
  CHECK(client.Receive(&c, 1) == SOCKET_ERROR);
  CHECK(CAsyncSocket::GetLastError() == WSAEINTR);
  CHECK(client.m_nPending >= 2);
  CHECK(!client.IsBlocking());
}

// The Ex functions with IPv4, and IPv6 over its loopback where the
// system has IPv6.
static void TestIPv6() {
  CSocket v4;
  CHECK(v4.Create(0, SOCK_DGRAM, _T("127.0.0.1")));
  CString strAddr;
  UINT nPort = 0;
  CHECK(v4.GetSockNameEx(strAddr, nPort) && strAddr == _T("127.0.0.1") && nPort != 0);
  CHECK(v4.SendToEx("four", 4, nPort, _T("127.0.0.1")) == 4);
  char buf[64];
  CString strFrom;
  UINT nFromPort = 0;
  CHECK(v4.ReceiveFromEx(buf, sizeof(buf), strFrom, nFromPort) == 4);
  CHECK(memcmp(buf, "four", 4) == 0 && strFrom == _T("127.0.0.1") && nFromPort == nPort);
  v4.Close();

  CSocket dgram;
  if (!dgram.Socket(SOCK_DGRAM, FD_READ | FD_WRITE, 0, AF_INET6)) {
    int nError = CAsyncSocket::GetLastError();
    CHECK(nError == WSAEAFNOSUPPORT || nError == WSAEPROTONOSUPPORT || nError == WSAEINVAL);
    printf("note: no IPv6 here (error %d); IPv6 checks not run\n", nError);
    return;
  }
  CHECK(dgram.Bind(0, _T("::1")));
  CHECK(dgram.GetSockNameEx(strAddr, nPort) && strAddr == _T("::1") && nPort != 0);
  // An IPv4 string form cannot hold the address.
  CString strOld;
  UINT nOld = 0;
  CHECK(!dgram.GetSockName(strOld, nOld));
  CHECK(dgram.SendToEx("six", 3, nPort, _T("::1")) == 3);
  CHECK(dgram.ReceiveFromEx(buf, sizeof(buf), strFrom, nFromPort) == 3);
  CHECK(memcmp(buf, "six", 3) == 0 && strFrom == _T("::1") && nFromPort == nPort);
  // IPv6 has no broadcast for SendTo without an address.
  CHECK(dgram.SendToEx("x", 1, nPort) == SOCKET_ERROR);
  CHECK(CAsyncSocket::GetLastError() == WSAEDESTADDRREQ);
  CHECK(dgram.SendToEx("x", 1, nPort, _T("no such host.invalid")) == SOCKET_ERROR);
  dgram.Close();

  // A stream connection: the string forms of Bind and Connect follow the
  // socket's family.
  CAsyncSocket listener;
  CHECK(listener.Socket(SOCK_STREAM, FD_ACCEPT, 0, AF_INET6));
  CHECK(listener.Bind(0, _T("::1")) && listener.Listen());
  UINT nListenPort = 0;
  CHECK(listener.GetSockNameEx(strAddr, nListenPort));
  CSocket client;
  CHECK(client.Socket(SOCK_STREAM, FD_READ | FD_WRITE | FD_CONNECT | FD_CLOSE, 0, AF_INET6));
  CHECK(client.Connect(_T("::1"), nListenPort));
  CHECK(client.GetPeerNameEx(strAddr, nPort) && strAddr == _T("::1") && nPort == nListenPort);
  CAsyncSocket conn;
  CHECK(listener.Accept(conn));
  CHECK(client.Send("hello6", 6) == 6);
  ::Sleep(50);
  CHECK(conn.Receive(buf, sizeof(buf)) == 6 && memcmp(buf, "hello6", 6) == 0);
  CHECK(conn.GetPeerNameEx(strAddr, nPort) && strAddr == _T("::1"));
  UINT nClientPort = 0;
  CHECK(client.GetSockNameEx(strFrom, nClientPort) && nPort == nClientPort);
}

int main() {
  CHECK(AfxSocketInit());
  TestAsync();
  TestBlocking();
  TestDatagram();
  TestErrors();
  TestIPv6();
  if (g_failures == 0) printf("PASS: all socket tests\n");
  else printf("%d failure(s)\n", g_failures);
  return g_failures ? 1 : 0;
}
