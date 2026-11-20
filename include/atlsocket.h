// atlsocket.h - CSocketAddr: a host and port looked up through
// getaddrinfo, its addrinfo list kept until the object goes. Winsock must
// be started (WSAStartup) first. FindAddr and the others return 0, or
// SOCKET_ERROR with the reason in WSAGetLastError.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#ifndef __ATLSOCKET_H__
#define __ATLSOCKET_H__

#include <winsock2.h>
#include <ws2tcpip.h>
#include <atlbase.h>
#include <stdio.h>

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

}  // namespace ATL

#endif  // __ATLSOCKET_H__
