// sockcore.cpp - CAsyncSocket, CSocket and CSocketFile.
//
// Each thread that uses sockets has a hidden window that Winsock posts
// socket events to (WSAAsyncSelect). Its window procedure finds the
// CAsyncSocket for the socket handle and calls the matching On...
// function. A CSocket call that would block instead waits for its event
// on that window, deferring the other socket events until it is done.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
// Winsock 2 and its IPv6 types come before <windows.h> brings in Winsock 1.
#include <winsock2.h>
#include <ws2tcpip.h>
#include "afximpl.h"
#include <afxsock.h>

IMPLEMENT_DYNAMIC(CAsyncSocket, CObject)
IMPLEMENT_DYNAMIC(CSocket, CAsyncSocket)
IMPLEMENT_DYNAMIC(CSocketFile, CFile)

namespace {

struct SocketNotify {
  WPARAM wParam;
  LPARAM lParam;
};

// Per-thread socket state.
struct SockState {
  HWND hSocketWindow;
  CMapPtrToPtr mapSocket;  // SOCKET -> CAsyncSocket*
  CPtrList listDeferred;   // SocketNotify*, held while a CSocket call waits
  int nBlocking;           // CSocket calls waiting in this thread
};

const TCHAR szSocketWindowClass[] = _T("AfxSocketNotify");
LONG g_nSocketInit = 0;

LRESULT CALLBACK SocketWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
  if (message == WM_SOCKET_NOTIFY) {
    CAsyncSocket::DoCallBack(wParam, lParam);
    return 0;
  }
  return ::DefWindowProc(hWnd, message, wParam, lParam);
}

void TermSockState(void* p) {
  SockState* pState = (SockState*)p;
  if (pState->hSocketWindow != NULL) ::DestroyWindow(pState->hSocketWindow);
  while (!pState->listDeferred.IsEmpty()) delete (SocketNotify*)pState->listDeferred.RemoveHead();
  delete pState;
}

SockState* GetSockState() {
  AFX_THREAD_STATE* pThreadState = AfxGetThreadState();
  if (pThreadState->m_pSockState == NULL) {
    SockState* pState = new SockState;
    pState->hSocketWindow = NULL;
    pState->nBlocking = 0;
    pThreadState->m_pSockState = pState;
    pThreadState->m_pfnSockTerm = TermSockState;
  }
  return (SockState*)pThreadState->m_pSockState;
}

HWND GetSocketWindow() {
  SockState* pState = GetSockState();
  if (pState->hSocketWindow == NULL) {
    // The module's own instance: a DLL with this library has its own
    // window procedure.
    HINSTANCE hInst = AfxGetInstanceHandle();
    WNDCLASS wc;
    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = SocketWndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = szSocketWindowClass;
    AfxRegisterClass(&wc);
    // A hidden top-level window rather than a message-only one, which
    // Windows 95 and NT 4 do not have.
    pState->hSocketWindow = ::CreateWindowEx(0, szSocketWindowClass, _T("Socket Notification Sink"),
                                             WS_OVERLAPPED, 0, 0, 0, 0, NULL, NULL, hInst, NULL);
  }
  return pState->hSocketWindow;
}

void Defer(SockState* pState, WPARAM wParam, LPARAM lParam) {
  SocketNotify* pNotify = new SocketNotify;
  pNotify->wParam = wParam;
  pNotify->lParam = lParam;
  pState->listDeferred.AddTail(pNotify);
}

// Posts the deferred events back to the socket window, in order.
void RepostDeferred(SockState* pState) {
  while (!pState->listDeferred.IsEmpty()) {
    SocketNotify* pNotify = (SocketNotify*)pState->listDeferred.RemoveHead();
    ::PostMessage(pState->hSocketWindow, WM_SOCKET_NOTIFY, pNotify->wParam, pNotify->lParam);
    delete pNotify;
  }
}

// Drops the events still waiting for a socket handle that is going away,
// so that a new socket given the same handle does not receive them.
void PurgeNotifications(SockState* pState, SOCKET hSocket) {
  POSITION pos = pState->listDeferred.GetHeadPosition();
  while (pos != NULL) {
    POSITION posCur = pos;
    SocketNotify* pNotify = (SocketNotify*)pState->listDeferred.GetNext(pos);
    if ((SOCKET)pNotify->wParam == hSocket) {
      pState->listDeferred.RemoveAt(posCur);
      delete pNotify;
    }
  }
  if (pState->hSocketWindow == NULL) return;
  CPtrList listKeep;
  MSG msg;
  while (::PeekMessage(&msg, pState->hSocketWindow, WM_SOCKET_NOTIFY, WM_SOCKET_NOTIFY, PM_REMOVE)) {
    if ((SOCKET)msg.wParam == hSocket) continue;
    SocketNotify* pNotify = new SocketNotify;
    pNotify->wParam = msg.wParam;
    pNotify->lParam = msg.lParam;
    listKeep.AddTail(pNotify);
  }
  while (!listKeep.IsEmpty()) {
    SocketNotify* pNotify = (SocketNotify*)listKeep.RemoveHead();
    ::PostMessage(pState->hSocketWindow, WM_SOCKET_NOTIFY, pNotify->wParam, pNotify->lParam);
    delete pNotify;
  }
}

// Fills in an IPv4 address from a dotted address or a host name. With no
// host, binding uses any local address and sending uses broadcast.
BOOL MakeAddress(LPCTSTR lpszHost, UINT nPort, SOCKADDR_IN& sa, u_long ulDefault) {
  memset(&sa, 0, sizeof(sa));
  sa.sin_family = AF_INET;
  sa.sin_port = htons((u_short)nPort);
  if (lpszHost == NULL || *lpszHost == 0) {
    sa.sin_addr.s_addr = htonl(ulDefault);
    return TRUE;
  }
  CStringA strHost(lpszHost);
  sa.sin_addr.s_addr = inet_addr(strHost);
  if (sa.sin_addr.s_addr == INADDR_NONE) {
    hostent* pHost = gethostbyname(strHost);
    if (pHost == NULL || pHost->h_addrtype != AF_INET) {
      if (pHost != NULL) ::WSASetLastError(WSAEAFNOSUPPORT);
      return FALSE;
    }
    memcpy(&sa.sin_addr, pHost->h_addr_list[0], sizeof(sa.sin_addr));
  }
  return TRUE;
}

void AddressToString(const SOCKADDR_IN& sa, CString& rAddress, UINT& rPort) {
  rAddress = inet_ntoa(sa.sin_addr);
  rPort = ntohs(sa.sin_port);
}

// The address family of a socket, bound or not. Winsock 1 has only IPv4.
int SocketFamily(SOCKET hSocket) {
  WSAPROTOCOL_INFOA info;
  int nLen = sizeof(info);
  if (::getsockopt(hSocket, SOL_SOCKET, SO_PROTOCOL_INFOA, (char*)&info, &nLen) == 0)
    return info.iAddressFamily;
  SOCKADDR_STORAGE ss;
  nLen = sizeof(ss);
  if (::getsockname(hSocket, (SOCKADDR*)&ss, &nLen) == 0) return ss.ss_family;
  return AF_INET;
}

// getaddrinfo is in ws2_32 from Windows XP; earlier systems have only
// numeric IPv6 addresses (WSAStringToAddress).
typedef int(WSAAPI* PFNGETADDRINFO)(const char*, const char*, const struct addrinfo*,
                                     struct addrinfo**);
typedef void(WSAAPI* PFNFREEADDRINFO)(struct addrinfo*);

// Fills in an address of the socket's family. lpszHost may be a numeric
// address (IPv6 ones with a %scope) or a host name. With no host, IPv6
// binding uses any local address; there is no IPv6 broadcast.
BOOL MakeAddressEx(int nFamily, LPCTSTR lpszHost, UINT nPort, SOCKADDR_STORAGE& ss, int& nLen,
                   u_long ulDefault) {
  memset(&ss, 0, sizeof(ss));
  if (nFamily != AF_INET6) {
    nLen = sizeof(SOCKADDR_IN);
    return MakeAddress(lpszHost, nPort, *(SOCKADDR_IN*)&ss, ulDefault);
  }
  sockaddr_in6& sa6 = *(sockaddr_in6*)&ss;
  nLen = sizeof(sockaddr_in6);
  if (lpszHost == NULL || *lpszHost == 0) {
    if (ulDefault != INADDR_ANY) {
      ::WSASetLastError(WSAEDESTADDRREQ);
      return FALSE;
    }
    sa6.sin6_family = AF_INET6;  // in6addr_any
    sa6.sin6_port = htons((u_short)nPort);
    return TRUE;
  }
  CStringA strHost(lpszHost);
  int nAddrLen = sizeof(ss);
  if (::WSAStringToAddressA(strHost.GetBuffer(), AF_INET6, NULL, (SOCKADDR*)&ss, &nAddrLen) != 0) {
    strHost.ReleaseBuffer();
    memset(&ss, 0, sizeof(ss));
    HMODULE hWS2 = ::GetModuleHandleA("ws2_32.dll");
    PFNGETADDRINFO pfnGet =
        hWS2 != NULL ? (PFNGETADDRINFO)(void*)::GetProcAddress(hWS2, "getaddrinfo") : NULL;
    PFNFREEADDRINFO pfnFree =
        hWS2 != NULL ? (PFNFREEADDRINFO)(void*)::GetProcAddress(hWS2, "freeaddrinfo") : NULL;
    if (pfnGet == NULL || pfnFree == NULL) {
      ::WSASetLastError(WSAHOST_NOT_FOUND);
      return FALSE;
    }
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET6;
    struct addrinfo* pResult = NULL;
    int nError = pfnGet(strHost, NULL, &hints, &pResult);
    if (nError != 0 || pResult == NULL || pResult->ai_addrlen > sizeof(ss)) {
      if (pResult != NULL) pfnFree(pResult);
      ::WSASetLastError(nError != 0 ? nError : WSAHOST_NOT_FOUND);
      return FALSE;
    }
    memcpy(&ss, pResult->ai_addr, pResult->ai_addrlen);
    pfnFree(pResult);
  } else {
    strHost.ReleaseBuffer();
  }
  sa6.sin6_family = AF_INET6;
  sa6.sin6_port = htons((u_short)nPort);
  return TRUE;
}

// The numeric address and the port of an IPv4 or IPv6 address.
BOOL AddressToStringEx(const SOCKADDR* pAddr, CString& rAddress, UINT& rPort) {
  if (pAddr->sa_family == AF_INET) {
    AddressToString(*(const SOCKADDR_IN*)pAddr, rAddress, rPort);
    return TRUE;
  }
  if (pAddr->sa_family != AF_INET6) {
    ::WSASetLastError(WSAEAFNOSUPPORT);
    return FALSE;
  }
  // Without the port, the text is the address alone (and its %scope).
  sockaddr_in6 sa6 = *(const sockaddr_in6*)pAddr;
  rPort = ntohs(sa6.sin6_port);
  sa6.sin6_port = 0;
  char sz[64];
  DWORD cch = sizeof(sz);
  if (::WSAAddressToStringA((SOCKADDR*)&sa6, sizeof(sa6), NULL, sz, &cch) != 0) return FALSE;
  rAddress = sz;
  return TRUE;
}

}  // namespace

// ---------------------------------------------------------------------
// Start-up

void AFXAPI AfxSocketTerm() {
  if (::InterlockedExchange(&g_nSocketInit, 0) != 0) ::WSACleanup();
}

static void __cdecl SocketTermAtExit() { AfxSocketTerm(); }

BOOL AFXAPI AfxSocketInit(WSADATA* lpwsaData) {
  WSADATA wsaData;
  if (::WSAStartup(MAKEWORD(1, 1), &wsaData) != 0) return FALSE;
  if (lpwsaData != NULL) *lpwsaData = wsaData;
  // One start-up is kept for the life of the program.
  if (::InterlockedExchange(&g_nSocketInit, 1) != 0)
    ::WSACleanup();
  else
    atexit(SocketTermAtExit);
  return TRUE;
}

// ---------------------------------------------------------------------
// CAsyncSocket

CAsyncSocket::CAsyncSocket() : m_hSocket(INVALID_SOCKET) {}

CAsyncSocket::~CAsyncSocket() {
  if (m_hSocket != INVALID_SOCKET) CAsyncSocket::Close();
}

BOOL CAsyncSocket::Create(UINT nSocketPort, int nSocketType, long lEvent,
                          LPCTSTR lpszSocketAddress) {
  if (!Socket(nSocketType, lEvent)) return FALSE;
  if (Bind(nSocketPort, lpszSocketAddress)) return TRUE;
  int nError = GetLastError();
  Close();
  ::WSASetLastError(nError);
  return FALSE;
}

BOOL CAsyncSocket::Socket(int nSocketType, long lEvent, int nProtocolType, int nAddressFormat) {
  SOCKET hSocket = ::socket(nAddressFormat, nSocketType, nProtocolType);
  if (hSocket == INVALID_SOCKET) return FALSE;
  if (CAsyncSocket::Attach(hSocket, lEvent)) return TRUE;
  int nError = GetLastError();
  Close();
  ::WSASetLastError(nError);
  return FALSE;
}

BOOL CAsyncSocket::Attach(SOCKET hSocket, long lEvent) {
  if (hSocket == INVALID_SOCKET) {
    ::WSASetLastError(WSAENOTSOCK);
    return FALSE;
  }
  m_hSocket = hSocket;
  GetSockState()->mapSocket.SetAt((void*)hSocket, this);
  return AsyncSelect(lEvent);
}

SOCKET CAsyncSocket::Detach() {
  SOCKET hSocket = m_hSocket;
  if (hSocket == INVALID_SOCKET) return hSocket;
  SockState* pState = GetSockState();
  ::WSAAsyncSelect(hSocket, GetSocketWindow(), 0, 0);
  pState->mapSocket.RemoveKey((void*)hSocket);
  PurgeNotifications(pState, hSocket);
  m_hSocket = INVALID_SOCKET;
  return hSocket;
}

CAsyncSocket* PASCAL CAsyncSocket::LookupHandle(SOCKET hSocket, BOOL) {
  AFX_THREAD_STATE* pThreadState = AfxGetThreadState();
  if (pThreadState->m_pSockState == NULL) return NULL;
  void* p = NULL;
  ((SockState*)pThreadState->m_pSockState)->mapSocket.Lookup((void*)hSocket, p);
  return (CAsyncSocket*)p;
}

CAsyncSocket* PASCAL CAsyncSocket::FromHandle(SOCKET hSocket) { return LookupHandle(hSocket); }

BOOL CAsyncSocket::AsyncSelect(long lEvent) {
  return ::WSAAsyncSelect(m_hSocket, GetSocketWindow(), WM_SOCKET_NOTIFY, lEvent) != SOCKET_ERROR;
}

void CAsyncSocket::Close() {
  if (m_hSocket == INVALID_SOCKET) return;
  SOCKET hSocket = m_hSocket;
  m_hSocket = INVALID_SOCKET;
  SockState* pState = GetSockState();
  pState->mapSocket.RemoveKey((void*)hSocket);
  ::closesocket(hSocket);
  PurgeNotifications(pState, hSocket);
}

BOOL CAsyncSocket::Bind(UINT nSocketPort, LPCTSTR lpszSocketAddress) {
  SOCKADDR_STORAGE ss;
  int nLen;
  if (!MakeAddressEx(SocketFamily(m_hSocket), lpszSocketAddress, nSocketPort, ss, nLen,
                     INADDR_ANY))
    return FALSE;
  return Bind((SOCKADDR*)&ss, nLen);
}

BOOL CAsyncSocket::Bind(const SOCKADDR* lpSockAddr, int nSockAddrLen) {
  return ::bind(m_hSocket, lpSockAddr, nSockAddrLen) != SOCKET_ERROR;
}

BOOL CAsyncSocket::Connect(LPCTSTR lpszHostAddress, UINT nHostPort) {
  SOCKADDR_STORAGE ss;
  int nLen;
  if (lpszHostAddress == NULL || !MakeAddressEx(SocketFamily(m_hSocket), lpszHostAddress,
                                                nHostPort, ss, nLen, INADDR_NONE)) {
    if (lpszHostAddress == NULL) ::WSASetLastError(WSAEINVAL);
    return FALSE;
  }
  return ConnectHelper((SOCKADDR*)&ss, nLen);
}

BOOL CAsyncSocket::Connect(const SOCKADDR* lpSockAddr, int nSockAddrLen) {
  return ConnectHelper(lpSockAddr, nSockAddrLen);
}

BOOL CAsyncSocket::ConnectHelper(const SOCKADDR* lpSockAddr, int nSockAddrLen) {
  return ::connect(m_hSocket, lpSockAddr, nSockAddrLen) != SOCKET_ERROR;
}

BOOL CAsyncSocket::Listen(int nConnectionBacklog) {
  return ::listen(m_hSocket, nConnectionBacklog) != SOCKET_ERROR;
}

BOOL CAsyncSocket::Accept(CAsyncSocket& rConnectedSocket, SOCKADDR* lpSockAddr,
                          int* lpSockAddrLen) {
  SOCKET hSocket = ::accept(m_hSocket, lpSockAddr, lpSockAddrLen);
  if (hSocket == INVALID_SOCKET) return FALSE;
  // The new socket already has the listening socket's events.
  rConnectedSocket.m_hSocket = hSocket;
  GetSockState()->mapSocket.SetAt((void*)hSocket, &rConnectedSocket);
  return TRUE;
}

BOOL CAsyncSocket::IOCtl(long lCommand, DWORD* lpArgument) {
  return ::ioctlsocket(m_hSocket, lCommand, (ULONG*)lpArgument) != SOCKET_ERROR;
}

BOOL CAsyncSocket::ShutDown(int nHow) { return ::shutdown(m_hSocket, nHow) != SOCKET_ERROR; }

int CAsyncSocket::Receive(void* lpBuf, int nBufLen, int nFlags) {
  return ::recv(m_hSocket, (char*)lpBuf, nBufLen, nFlags);
}

int CAsyncSocket::Send(const void* lpBuf, int nBufLen, int nFlags) {
  return ::send(m_hSocket, (const char*)lpBuf, nBufLen, nFlags);
}

int CAsyncSocket::ReceiveFrom(void* lpBuf, int nBufLen, CString& rSocketAddress,
                              UINT& rSocketPort, int nFlags) {
  SOCKADDR_IN sa;
  memset(&sa, 0, sizeof(sa));
  int nLen = sizeof(sa);
  int nResult = ReceiveFromHelper(lpBuf, nBufLen, (SOCKADDR*)&sa, &nLen, nFlags);
  if (nResult != SOCKET_ERROR) AddressToString(sa, rSocketAddress, rSocketPort);
  return nResult;
}

int CAsyncSocket::ReceiveFrom(void* lpBuf, int nBufLen, SOCKADDR* lpSockAddr,
                              int* lpSockAddrLen, int nFlags) {
  return ReceiveFromHelper(lpBuf, nBufLen, lpSockAddr, lpSockAddrLen, nFlags);
}

int CAsyncSocket::ReceiveFromHelper(void* lpBuf, int nBufLen, SOCKADDR* lpSockAddr,
                                    int* lpSockAddrLen, int nFlags) {
  return ::recvfrom(m_hSocket, (char*)lpBuf, nBufLen, nFlags, lpSockAddr, lpSockAddrLen);
}

int CAsyncSocket::SendTo(const void* lpBuf, int nBufLen, UINT nHostPort,
                         LPCTSTR lpszHostAddress, int nFlags) {
  SOCKADDR_IN sa;
  if (!MakeAddress(lpszHostAddress, nHostPort, sa, INADDR_BROADCAST)) return SOCKET_ERROR;
  return SendToHelper(lpBuf, nBufLen, (SOCKADDR*)&sa, sizeof(sa), nFlags);
}

int CAsyncSocket::SendTo(const void* lpBuf, int nBufLen, const SOCKADDR* lpSockAddr,
                         int nSockAddrLen, int nFlags) {
  return SendToHelper(lpBuf, nBufLen, lpSockAddr, nSockAddrLen, nFlags);
}

int CAsyncSocket::SendToHelper(const void* lpBuf, int nBufLen, const SOCKADDR* lpSockAddr,
                               int nSockAddrLen, int nFlags) {
  return ::sendto(m_hSocket, (const char*)lpBuf, nBufLen, nFlags, lpSockAddr, nSockAddrLen);
}

BOOL CAsyncSocket::GetPeerName(CString& rPeerAddress, UINT& rPeerPort) {
  SOCKADDR_IN sa;
  memset(&sa, 0, sizeof(sa));
  int nLen = sizeof(sa);
  if (!GetPeerName((SOCKADDR*)&sa, &nLen)) return FALSE;
  AddressToString(sa, rPeerAddress, rPeerPort);
  return TRUE;
}

BOOL CAsyncSocket::GetPeerName(SOCKADDR* lpSockAddr, int* lpSockAddrLen) {
  return ::getpeername(m_hSocket, lpSockAddr, lpSockAddrLen) != SOCKET_ERROR;
}

BOOL CAsyncSocket::GetSockName(CString& rSocketAddress, UINT& rSocketPort) {
  SOCKADDR_IN sa;
  memset(&sa, 0, sizeof(sa));
  int nLen = sizeof(sa);
  if (!GetSockName((SOCKADDR*)&sa, &nLen)) return FALSE;
  AddressToString(sa, rSocketAddress, rSocketPort);
  return TRUE;
}

BOOL CAsyncSocket::GetSockName(SOCKADDR* lpSockAddr, int* lpSockAddrLen) {
  return ::getsockname(m_hSocket, lpSockAddr, lpSockAddrLen) != SOCKET_ERROR;
}

// The Ex forms take and give IPv6 addresses as well as IPv4 ones.

BOOL CAsyncSocket::GetPeerNameEx(CString& rPeerAddress, UINT& rPeerPort) {
  SOCKADDR_STORAGE ss;
  memset(&ss, 0, sizeof(ss));
  int nLen = sizeof(ss);
  if (!GetPeerName((SOCKADDR*)&ss, &nLen)) return FALSE;
  return AddressToStringEx((SOCKADDR*)&ss, rPeerAddress, rPeerPort);
}

BOOL CAsyncSocket::GetSockNameEx(CString& rSocketAddress, UINT& rSocketPort) {
  SOCKADDR_STORAGE ss;
  memset(&ss, 0, sizeof(ss));
  int nLen = sizeof(ss);
  if (!GetSockName((SOCKADDR*)&ss, &nLen)) return FALSE;
  return AddressToStringEx((SOCKADDR*)&ss, rSocketAddress, rSocketPort);
}

int CAsyncSocket::ReceiveFromEx(void* lpBuf, int nBufLen, CString& rSocketAddress,
                                UINT& rSocketPort, int nFlags) {
  SOCKADDR_STORAGE ss;
  memset(&ss, 0, sizeof(ss));
  int nLen = sizeof(ss);
  int nResult = ReceiveFromHelper(lpBuf, nBufLen, (SOCKADDR*)&ss, &nLen, nFlags);
  if (nResult != SOCKET_ERROR && !AddressToStringEx((SOCKADDR*)&ss, rSocketAddress, rSocketPort))
    return SOCKET_ERROR;
  return nResult;
}

int CAsyncSocket::SendToEx(const void* lpBuf, int nBufLen, UINT nHostPort,
                           LPCTSTR lpszHostAddress, int nFlags) {
  SOCKADDR_STORAGE ss;
  int nLen;
  if (!MakeAddressEx(SocketFamily(m_hSocket), lpszHostAddress, nHostPort, ss, nLen,
                     INADDR_BROADCAST))
    return SOCKET_ERROR;
  return SendToHelper(lpBuf, nBufLen, (SOCKADDR*)&ss, nLen, nFlags);
}

BOOL CAsyncSocket::SetSockOpt(int nOptionName, const void* lpOptionValue, int nOptionLen,
                              int nLevel) {
  return ::setsockopt(m_hSocket, nLevel, nOptionName, (const char*)lpOptionValue, nOptionLen) !=
         SOCKET_ERROR;
}

BOOL CAsyncSocket::GetSockOpt(int nOptionName, void* lpOptionValue, int* lpOptionLen,
                              int nLevel) {
  return ::getsockopt(m_hSocket, nLevel, nOptionName, (char*)lpOptionValue, lpOptionLen) !=
         SOCKET_ERROR;
}

void CAsyncSocket::OnReceive(int) {}
void CAsyncSocket::OnSend(int) {}
void CAsyncSocket::OnOutOfBandData(int) {}
void CAsyncSocket::OnAccept(int) {}
void CAsyncSocket::OnConnect(int) {}
void CAsyncSocket::OnClose(int) {}

void PASCAL CAsyncSocket::DoCallBack(WPARAM wParam, LPARAM lParam) {
  SockState* pState = GetSockState();
  if (pState->nBlocking > 0) {
    // A CSocket call is waiting; the events are delivered when it ends.
    Defer(pState, wParam, lParam);
    return;
  }
  CAsyncSocket* pSocket = LookupHandle((SOCKET)wParam);
  if (pSocket == NULL) return;
  int nErrorCode = WSAGETSELECTERROR(lParam);
  switch (WSAGETSELECTEVENT(lParam)) {
  case FD_READ: {
    // Winsock can report data that an earlier call already took.
    DWORD nBytes = 0;
    if (nErrorCode == 0 && !pSocket->IOCtl(FIONREAD, &nBytes)) nErrorCode = GetLastError();
    if (nBytes != 0 || nErrorCode != 0) pSocket->OnReceive(nErrorCode);
    break;
  }
  case FD_WRITE:
    pSocket->OnSend(nErrorCode);
    break;
  case FD_OOB:
    pSocket->OnOutOfBandData(nErrorCode);
    break;
  case FD_ACCEPT:
    pSocket->OnAccept(nErrorCode);
    break;
  case FD_CONNECT:
    if (pSocket->IsKindOf(RUNTIME_CLASS(CSocket))) ((CSocket*)pSocket)->m_nConnectError = nErrorCode;
    pSocket->OnConnect(nErrorCode);
    break;
  case FD_CLOSE:
    pSocket->OnClose(nErrorCode);
    break;
  }
}

// ---------------------------------------------------------------------
// CSocket

CSocket::CSocket() : m_nTimeOut(2000), m_pbBlocking(NULL), m_nConnectError(-1) {}

CSocket::~CSocket() {
  if (m_hSocket != INVALID_SOCKET) Close();
}

BOOL CSocket::Create(UINT nSocketPort, int nSocketType, LPCTSTR lpszSocketAddress) {
  return CAsyncSocket::Create(nSocketPort, nSocketType,
                              FD_READ | FD_WRITE | FD_OOB | FD_ACCEPT | FD_CONNECT | FD_CLOSE,
                              lpszSocketAddress);
}

BOOL CSocket::Attach(SOCKET hSocket) { return CAsyncSocket::Attach(hSocket); }

CSocket* PASCAL CSocket::FromHandle(SOCKET hSocket) {
  CAsyncSocket* pSocket = CAsyncSocket::LookupHandle(hSocket);
  return pSocket != NULL && pSocket->IsKindOf(RUNTIME_CLASS(CSocket)) ? (CSocket*)pSocket : NULL;
}

void CSocket::CancelBlockingCall() {
  if (m_pbBlocking != NULL) {
    *m_pbBlocking = FALSE;
    m_pbBlocking = NULL;
  }
}

void CSocket::Close() {
  CancelBlockingCall();
  CAsyncSocket::Close();
}

BOOL CSocket::OnMessagePending() {
  MSG msg;
  if (::PeekMessage(&msg, NULL, WM_PAINT, WM_PAINT, PM_REMOVE)) {
    ::DispatchMessage(&msg);
    return TRUE;
  }
  return FALSE;
}

// Waits until this socket reports one of the events in uStopFlag (or is
// closed), keeping other socket events for later. Returns FALSE if the
// call was cancelled.
BOOL CSocket::PumpMessages(UINT uStopFlag) {
  SockState* pState = GetSockState();
  HWND hWnd = GetSocketWindow();
  SOCKET hSocket = m_hSocket;
  BOOL bBlocking = TRUE;
  m_pbBlocking = &bBlocking;
  pState->nBlocking++;
  while (bBlocking) {
    MSG msg;
    if (::PeekMessage(&msg, hWnd, WM_SOCKET_NOTIFY, WM_SOCKET_NOTIFY, PM_REMOVE)) {
      if ((SOCKET)msg.wParam == hSocket) {
        UINT nEvent = WSAGETSELECTEVENT(msg.lParam);
        int nError = WSAGETSELECTERROR(msg.lParam);
        if (nEvent == FD_CONNECT) m_nConnectError = nError;
        if (nEvent == FD_CLOSE) {
          if ((uStopFlag & FD_CONNECT) && m_nConnectError == -1)
            m_nConnectError = nError != 0 ? nError : WSAECONNREFUSED;
          Defer(pState, msg.wParam, msg.lParam);  // OnClose still runs, afterwards
          break;
        }
        if (nEvent & uStopFlag) break;
      }
      Defer(pState, msg.wParam, msg.lParam);
      continue;
    }
    if (OnMessagePending()) continue;
    if (!bBlocking) break;
    ::MsgWaitForMultipleObjects(0, NULL, FALSE, (DWORD)m_nTimeOut, QS_ALLINPUT);
  }
  BOOL bResult = bBlocking;
  if (m_pbBlocking == &bBlocking) m_pbBlocking = NULL;
  if (--pState->nBlocking == 0) RepostDeferred(pState);
  if (!bResult) ::WSASetLastError(WSAEINTR);
  return bResult;
}

BOOL CSocket::Accept(CAsyncSocket& rConnectedSocket, SOCKADDR* lpSockAddr, int* lpSockAddrLen) {
  if (m_pbBlocking != NULL) {
    ::WSASetLastError(WSAEINPROGRESS);
    return FALSE;
  }
  while (!CAsyncSocket::Accept(rConnectedSocket, lpSockAddr, lpSockAddrLen)) {
    if (GetLastError() != WSAEWOULDBLOCK || !PumpMessages(FD_ACCEPT)) return FALSE;
  }
  return TRUE;
}

BOOL CSocket::ConnectHelper(const SOCKADDR* lpSockAddr, int nSockAddrLen) {
  if (m_pbBlocking != NULL) {
    ::WSASetLastError(WSAEINPROGRESS);
    return FALSE;
  }
  m_nConnectError = -1;
  if (CAsyncSocket::ConnectHelper(lpSockAddr, nSockAddrLen)) return TRUE;
  if (GetLastError() != WSAEWOULDBLOCK) return FALSE;
  while (PumpMessages(FD_CONNECT)) {
    if (m_nConnectError != -1) {
      ::WSASetLastError(m_nConnectError);
      return m_nConnectError == 0;
    }
  }
  return FALSE;
}

int CSocket::Receive(void* lpBuf, int nBufLen, int nFlags) {
  if (m_pbBlocking != NULL) {
    ::WSASetLastError(WSAEINPROGRESS);
    return SOCKET_ERROR;
  }
  int nResult;
  while ((nResult = CAsyncSocket::Receive(lpBuf, nBufLen, nFlags)) == SOCKET_ERROR) {
    if (GetLastError() != WSAEWOULDBLOCK || !PumpMessages(FD_READ | FD_OOB)) return SOCKET_ERROR;
  }
  return nResult;
}

int CSocket::SendChunk(const void* lpBuf, int nBufLen, int nFlags) {
  int nResult;
  while ((nResult = CAsyncSocket::Send(lpBuf, nBufLen, nFlags)) == SOCKET_ERROR) {
    if (GetLastError() != WSAEWOULDBLOCK || !PumpMessages(FD_WRITE)) return SOCKET_ERROR;
  }
  return nResult;
}

// Sends the whole buffer, waiting for room as needed.
int CSocket::Send(const void* lpBuf, int nBufLen, int nFlags) {
  if (m_pbBlocking != NULL) {
    ::WSASetLastError(WSAEINPROGRESS);
    return SOCKET_ERROR;
  }
  const char* p = (const char*)lpBuf;
  int nLeft = nBufLen;
  while (nLeft > 0) {
    int nSent = SendChunk(p, nLeft, nFlags);
    if (nSent == SOCKET_ERROR) return SOCKET_ERROR;
    p += nSent;
    nLeft -= nSent;
  }
  return nBufLen;
}

int CSocket::ReceiveFromHelper(void* lpBuf, int nBufLen, SOCKADDR* lpSockAddr,
                               int* lpSockAddrLen, int nFlags) {
  if (m_pbBlocking != NULL) {
    ::WSASetLastError(WSAEINPROGRESS);
    return SOCKET_ERROR;
  }
  int nResult;
  while ((nResult = CAsyncSocket::ReceiveFromHelper(lpBuf, nBufLen, lpSockAddr, lpSockAddrLen,
                                                    nFlags)) == SOCKET_ERROR) {
    if (GetLastError() != WSAEWOULDBLOCK || !PumpMessages(FD_READ)) return SOCKET_ERROR;
  }
  return nResult;
}

int CSocket::SendToHelper(const void* lpBuf, int nBufLen, const SOCKADDR* lpSockAddr,
                          int nSockAddrLen, int nFlags) {
  if (m_pbBlocking != NULL) {
    ::WSASetLastError(WSAEINPROGRESS);
    return SOCKET_ERROR;
  }
  int nResult;
  while ((nResult = CAsyncSocket::SendToHelper(lpBuf, nBufLen, lpSockAddr, nSockAddrLen,
                                               nFlags)) == SOCKET_ERROR) {
    if (GetLastError() != WSAEWOULDBLOCK || !PumpMessages(FD_WRITE)) return SOCKET_ERROR;
  }
  return nResult;
}

// ---------------------------------------------------------------------
// CSocketFile

CSocketFile::CSocketFile(CSocket* pSocket, BOOL bArchiveCompatible)
    : m_pSocket(pSocket), m_bArchiveCompatible(bArchiveCompatible) {}

CSocketFile::~CSocketFile() {}

UINT CSocketFile::Read(void* lpBuf, UINT nCount) {
  if (m_pSocket == NULL) AfxThrowFileException(CFileException::genericException, WSAENOTSOCK);
  BYTE* p = (BYTE*)lpBuf;
  UINT nLeft = nCount;
  while (nLeft > 0) {
    int nRead = m_pSocket->Receive(p, (int)nLeft);
    if (nRead == SOCKET_ERROR)
      AfxThrowFileException(CFileException::genericException, CAsyncSocket::GetLastError());
    if (nRead == 0) break;  // the other end has finished sending
    p += nRead;
    nLeft -= (UINT)nRead;
    if (m_bArchiveCompatible) break;
  }
  return nCount - nLeft;
}

void CSocketFile::Write(const void* lpBuf, UINT nCount) {
  if (m_pSocket == NULL) AfxThrowFileException(CFileException::genericException, WSAENOTSOCK);
  if (m_pSocket->Send(lpBuf, (int)nCount) == SOCKET_ERROR)
    AfxThrowFileException(CFileException::genericException, CAsyncSocket::GetLastError());
}

void CSocketFile::Close() { m_pSocket = NULL; }

UINT CSocketFile::GetBufferPtr(UINT nCommand, UINT, void**, void**) {
  return nCommand == bufferCheck ? (UINT)bufferBlocking : 0;
}

ULONGLONG CSocketFile::GetPosition() const {
  AfxThrowNotSupportedException();
  return 0;
}
BOOL CSocketFile::Open(LPCTSTR, UINT, CFileException*) {
  AfxThrowNotSupportedException();
  return FALSE;
}
CFile* CSocketFile::Duplicate() const {
  AfxThrowNotSupportedException();
  return NULL;
}
ULONGLONG CSocketFile::Seek(LONGLONG, UINT) {
  AfxThrowNotSupportedException();
  return 0;
}
void CSocketFile::SetLength(ULONGLONG) { AfxThrowNotSupportedException(); }
ULONGLONG CSocketFile::GetLength() const {
  AfxThrowNotSupportedException();
  return 0;
}
void CSocketFile::LockRange(ULONGLONG, ULONGLONG) { AfxThrowNotSupportedException(); }
void CSocketFile::UnlockRange(ULONGLONG, ULONGLONG) { AfxThrowNotSupportedException(); }
