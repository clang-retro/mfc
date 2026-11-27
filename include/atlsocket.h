// atlsocket.h - CSocketAddr: a host and port looked up through
// getaddrinfo, its addrinfo list kept until the object goes. Winsock must
// be started (WSAStartup) first. FindAddr and the others return 0, or
// SOCKET_ERROR with the reason in WSAGetLastError.
//
// ZEvtSyncSocket: the blocking stream socket CAtlHttpClient and
// CSMTPConnection talk through, each connect, read and write bounded by
// its timeout (select underneath, which every platform has).
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#ifndef __ATLSOCKET_H__
#define __ATLSOCKET_H__

#include <winsock2.h>
#include <ws2tcpip.h>
#include <atlbase.h>
#include <stdio.h>
#include <atlutil.h>

#if !defined(_ATL_NO_DEFAULT_LIBS) && defined(_WIN32)
__asm__(".section .drectve,\"yn\"\n"
        ".ascii \" -defaultlib:ws2_32\"\n"
        ".text");
#endif

namespace ATL {

class CSocketAddr {
public:
  CSocketAddr() : m_pAddrs(NULL) {}
  ~CSocketAddr() { Free(); }

  int FindAddr(LPCTSTR szHost, LPCTSTR szPortOrServiceName, int flags, int addr_family,
               int sock_type, int ai_proto) {
    Free();
    addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_flags = flags;
    hints.ai_family = addr_family;
    hints.ai_socktype = sock_type;
    hints.ai_protocol = ai_proto;
    char szHostA[1025], szPortA[64];
    const char* pszHost = Narrow(szHost, szHostA, sizeof(szHostA));
    const char* pszPort = Narrow(szPortOrServiceName, szPortA, sizeof(szPortA));
    int nErr = ::getaddrinfo(pszHost, pszPort, &hints, &m_pAddrs);
    if (nErr != 0) {
      m_pAddrs = NULL;
      ::WSASetLastError(nErr);
      return SOCKET_ERROR;
    }
    return 0;
  }
  int FindAddr(LPCTSTR szHost, int nPortNo, int flags, int addr_family, int sock_type,
               int ai_proto) {
    TCHAR szPort[16];
    wsprintf(szPort, _T("%d"), nPortNo);
    return FindAddr(szHost, szPort, flags, addr_family, sock_type, ai_proto);
  }
  int FindINET4Addr(LPCTSTR szHost, int nPortNo, int flags = 0, int sock_type = SOCK_STREAM) {
    return FindAddr(szHost, nPortNo, flags, PF_INET, sock_type, 0);
  }
  int FindINET6Addr(LPCTSTR szHost, int nPortNo, int flags = 0, int sock_type = SOCK_STREAM) {
    return FindAddr(szHost, nPortNo, flags, PF_INET6, sock_type, 0);
  }

  addrinfo* const GetAddrInfoList() const { return m_pAddrs; }
  addrinfo* const GetAddrInfo(int nIndex = 0) const {
    addrinfo* p = m_pAddrs;
    for (; p != NULL && nIndex > 0; nIndex--) p = p->ai_next;
    return p;
  }

private:
  addrinfo* m_pAddrs;

  void Free() {
    if (m_pAddrs != NULL) ::freeaddrinfo(m_pAddrs);
    m_pAddrs = NULL;
  }
  // Host names are ASCII (or IDNA); NULL stays NULL.
  static const char* Narrow(LPCTSTR psz, char* pszBuf, int nBuf) {
    if (psz == NULL) return NULL;
#ifdef _UNICODE
    ::WideCharToMultiByte(CP_ACP, 0, psz, -1, pszBuf, nBuf, NULL, NULL);
    pszBuf[nBuf - 1] = 0;
    return pszBuf;
#else
    (void)pszBuf;
    (void)nBuf;
    return psz;
#endif
  }

  CSocketAddr(const CSocketAddr&);
  CSocketAddr& operator=(const CSocketAddr&);
};


#ifndef ATL_SOCK_TIMEOUT
#define ATL_SOCK_TIMEOUT 10000
#endif

#if defined(_WINPORT) && !defined(_WSABUF_DEFINED)
#define _WSABUF_DEFINED
typedef struct _WSABUF {
  ULONG len;
  char* buf;
} WSABUF, *LPWSABUF;
#endif

class ZEvtSyncSocket {
public:
  ZEvtSyncSocket()
      : m_socket(INVALID_SOCKET), m_dwCreateFlags(0), m_dwLastError(0),
        m_dwSocketTimeout(ATL_SOCK_TIMEOUT) {}
  ~ZEvtSyncSocket() { Close(); }

  operator SOCKET() { return m_socket; }
  SOCKET GetSocket() const { return m_socket; }

  void Close() {
    if (m_socket != INVALID_SOCKET) {
      ::shutdown(m_socket, SD_BOTH);
      ::closesocket(m_socket);
      m_socket = INVALID_SOCKET;
    }
  }
  void Term() { Close(); }

  bool Create(const addrinfo* pAI, WORD wFlags = 0) {
    if (pAI == NULL) {
      m_dwLastError = WSAEINVAL;
      return false;
    }
    return Create(pAI->ai_family, pAI->ai_socktype, pAI->ai_protocol, wFlags);
  }
  bool Create(int af = AF_INET, int st = SOCK_STREAM, int proto = IPPROTO_TCP, WORD wFlags = 0) {
    Close();
    m_dwCreateFlags = wFlags;
    m_socket = ::socket(af, st, proto);
    if (m_socket == INVALID_SOCKET) {
      m_dwLastError = (DWORD)::WSAGetLastError();
      return false;
    }
    return true;
  }

  // A name looked up, then each of its addresses tried in turn; the
  // socket is made for the address's family if there is none yet.
  bool Connect(LPCTSTR szAddr, unsigned short nPort) {
    CSocketAddr addr;
    if (addr.FindAddr(szAddr, nPort, 0, PF_UNSPEC, SOCK_STREAM, 0) != 0) {
      m_dwLastError = (DWORD)::WSAGetLastError();
      return false;
    }
    for (addrinfo* p = addr.GetAddrInfoList(); p != NULL; p = p->ai_next) {
      if (p->ai_family != AF_INET && p->ai_family != AF_INET6) continue;
      bool bMade = false;
      if (m_socket == INVALID_SOCKET) {
        if (!Create(p, (WORD)m_dwCreateFlags)) continue;
        bMade = true;
      }
      if (Connect(p->ai_addr, (int)p->ai_addrlen)) return true;
      if (bMade) Close();
    }
    if (m_dwLastError == 0) m_dwLastError = WSAEHOSTUNREACH;
    return false;
  }
  bool Connect(const addrinfo* pAI) {
    if (pAI == NULL) {
      m_dwLastError = WSAEINVAL;
      return false;
    }
    return Connect(pAI->ai_addr, (int)pAI->ai_addrlen);
  }
  bool Connect(const SOCKADDR* psa, int len) {
    if (m_socket == INVALID_SOCKET) {
      m_dwLastError = WSAENOTSOCK;
      return false;
    }
    _SetBlocking(false);
    int nRet = ::connect(m_socket, psa, len);
    bool bOk = true;
    if (nRet != 0) {
      int nErr = ::WSAGetLastError();
      if (nErr != WSAEWOULDBLOCK && nErr != WSAEINPROGRESS) {
        m_dwLastError = (DWORD)nErr;
        bOk = false;
      } else if (!_Wait(false, true)) {
        bOk = false;
      } else {
        int nSoErr = 0;
        int nLen = sizeof(nSoErr);
        ::getsockopt(m_socket, SOL_SOCKET, SO_ERROR, (char*)&nSoErr, &nLen);
        if (nSoErr != 0) {
          m_dwLastError = (DWORD)nSoErr;
          bOk = false;
        }
      }
    }
    _SetBlocking(true);
    return bOk;
  }

  bool Write(WSABUF* pBuffers, int nCount, DWORD* pdwSize) {
    DWORD dwTotal = 0;
    for (int i = 0; i < nCount; i++) {
      DWORD dw = pBuffers[i].len;
      bool b = Write((const unsigned char*)pBuffers[i].buf, &dw);
      dwTotal += dw;
      if (!b) {
        if (pdwSize != NULL) *pdwSize = dwTotal;
        return false;
      }
    }
    if (pdwSize != NULL) *pdwSize = dwTotal;
    return true;
  }
  // All of it written, or false with *pdwSize what was.
  bool Write(const unsigned char* pBuffIn, DWORD* pdwSize) {
    if (pdwSize == NULL) return false;
    DWORD dwLeft = *pdwSize, dwDone = 0;
    *pdwSize = 0;
    if (m_socket == INVALID_SOCKET) {
      m_dwLastError = WSAENOTSOCK;
      return false;
    }
    while (dwLeft > 0) {
      if (!_Wait(false, true)) {
        *pdwSize = dwDone;
        return false;
      }
      int n = ::send(m_socket, (const char*)pBuffIn + dwDone, (int)dwLeft, 0);
      if (n == SOCKET_ERROR) {
        m_dwLastError = (DWORD)::WSAGetLastError();
        *pdwSize = dwDone;
        return false;
      }
      dwDone += (DWORD)n;
      dwLeft -= (DWORD)n;
    }
    *pdwSize = dwDone;
    return true;
  }
  // What has come, up to *pdwSize; zero once the other end has closed.
  bool Read(const unsigned char* pBuff, DWORD* pdwSize) {
    if (pdwSize == NULL) return false;
    DWORD dwMax = *pdwSize;
    *pdwSize = 0;
    if (m_socket == INVALID_SOCKET) {
      m_dwLastError = WSAENOTSOCK;
      return false;
    }
    if (!_Wait(true, false)) return false;
    int n = ::recv(m_socket, (char*)pBuff, (int)dwMax, 0);
    if (n == SOCKET_ERROR) {
      m_dwLastError = (DWORD)::WSAGetLastError();
      return false;
    }
    *pdwSize = (DWORD)n;
    return true;
  }

  bool Init(SOCKET hSocket, void* /*pData*/ = NULL) {
    Close();
    m_socket = hSocket;
    return true;
  }
  DWORD GetSocketTimeout() const { return m_dwSocketTimeout; }
  DWORD SetSocketTimeout(DWORD dwNewTimeout) {
    DWORD dwOld = m_dwSocketTimeout;
    m_dwSocketTimeout = dwNewTimeout;
    return dwOld;
  }
  DWORD GetLastError() const { return m_dwLastError; }
  bool SupportsScheme(ATL_URL_SCHEME scheme) { return scheme == ATL_URL_SCHEME_HTTP; }

protected:
  SOCKET m_socket;
  DWORD m_dwCreateFlags;
  DWORD m_dwLastError;
  DWORD m_dwSocketTimeout;

  void _SetBlocking(bool bBlock) {
    ULONG ul = bBlock ? 0 : 1;
    ::ioctlsocket(m_socket, FIONBIO, &ul);
  }
  // Ready to read or write within the timeout (INFINITE waits).
  bool _Wait(bool bRead, bool bWrite) {
    fd_set fds, fdsErr;
    FD_ZERO(&fds);
    FD_ZERO(&fdsErr);
    FD_SET(m_socket, &fds);
    FD_SET(m_socket, &fdsErr);
    timeval tv;
    tv.tv_sec = (long)(m_dwSocketTimeout / 1000);
    tv.tv_usec = (long)((m_dwSocketTimeout % 1000) * 1000);
    int n = ::select((int)m_socket + 1, bRead ? &fds : NULL, bWrite ? &fds : NULL, &fdsErr,
                     m_dwSocketTimeout == INFINITE ? NULL : &tv);
    if (n == 0) {
      m_dwLastError = WSAETIMEDOUT;
      return false;
    }
    if (n == SOCKET_ERROR) {
      m_dwLastError = (DWORD)::WSAGetLastError();
      return false;
    }
    return true;
  }

private:
  ZEvtSyncSocket(const ZEvtSyncSocket&);
  ZEvtSyncSocket& operator=(const ZEvtSyncSocket&);
};

}  // namespace ATL

#endif  // __ATLSOCKET_H__
