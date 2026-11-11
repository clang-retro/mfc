// winsock2.h - Windows Sockets over the system's BSD sockets: the socket
// handle, error codes, startup, asynchronous selection and the calls whose
// arguments differ from the BSD ones.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#ifndef _WINPORT_WINSOCK2_H
#define _WINPORT_WINSOCK2_H

#include <windows.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>

#define WSAAPI WINAPI

typedef UINT_PTR SOCKET;
#define INVALID_SOCKET ((SOCKET)(~0))
#define SOCKET_ERROR (-1)

typedef struct sockaddr SOCKADDR, *PSOCKADDR, *LPSOCKADDR;
typedef struct sockaddr_in SOCKADDR_IN, *PSOCKADDR_IN, *LPSOCKADDR_IN;
typedef struct sockaddr_in6 SOCKADDR_IN6, *PSOCKADDR_IN6, *LPSOCKADDR_IN6;
typedef struct sockaddr_storage SOCKADDR_STORAGE, *PSOCKADDR_STORAGE, *LPSOCKADDR_STORAGE;
typedef struct in_addr IN_ADDR, *PIN_ADDR, *LPIN_ADDR;
typedef struct in6_addr IN6_ADDR, *PIN6_ADDR, *LPIN6_ADDR;
typedef struct hostent HOSTENT, *PHOSTENT, *LPHOSTENT;
typedef struct servent SERVENT, *PSERVENT, *LPSERVENT;
typedef struct protoent PROTOENT, *PPROTOENT, *LPPROTOENT;
typedef struct linger LINGER, *PLINGER, *LPLINGER;
typedef struct timeval TIMEVAL, *PTIMEVAL, *LPTIMEVAL;
typedef struct addrinfo ADDRINFOA, *PADDRINFOA;

#define SD_RECEIVE SHUT_RD
#define SD_SEND SHUT_WR
#define SD_BOTH SHUT_RDWR

#ifndef MSG_PARTIAL
#define MSG_PARTIAL 0x8000
#endif
#define SOMAXCONN_HINT(b) (-(b))

// Socket options with no BSD equivalent. The system rejects them, as it
// does any option it does not know.
#define SO_DONTLINGER ((int)(~SO_LINGER))
#define SO_EXCLUSIVEADDRUSE ((int)(~SO_REUSEADDR))
#define SO_PROTOCOL_INFOA 0x2004
#define SO_PROTOCOL_INFOW 0x2005
#define SO_CONNECT_TIME 0x700C

// Asynchronous selection
#define FD_READ_BIT 0
#define FD_READ (1 << FD_READ_BIT)
#define FD_WRITE_BIT 1
#define FD_WRITE (1 << FD_WRITE_BIT)
#define FD_OOB_BIT 2
#define FD_OOB (1 << FD_OOB_BIT)
#define FD_ACCEPT_BIT 3
#define FD_ACCEPT (1 << FD_ACCEPT_BIT)
#define FD_CONNECT_BIT 4
#define FD_CONNECT (1 << FD_CONNECT_BIT)
#define FD_CLOSE_BIT 5
#define FD_CLOSE (1 << FD_CLOSE_BIT)
#define FD_MAX_EVENTS 10
#define FD_ALL_EVENTS ((1 << FD_MAX_EVENTS) - 1)
#define WSAMAKEASYNCREPLY(buflen, error) MAKELONG(buflen, error)
#define WSAMAKESELECTREPLY(event, error) MAKELONG(event, error)
#define WSAGETASYNCBUFLEN(lParam) LOWORD(lParam)
#define WSAGETASYNCERROR(lParam) HIWORD(lParam)
#define WSAGETSELECTEVENT(lParam) LOWORD(lParam)
#define WSAGETSELECTERROR(lParam) HIWORD(lParam)

// Errors
#define WSABASEERR 10000
#define WSAEINTR (WSABASEERR + 4)
#define WSAEBADF (WSABASEERR + 9)
#define WSAEACCES (WSABASEERR + 13)
#define WSAEFAULT (WSABASEERR + 14)
#define WSAEINVAL (WSABASEERR + 22)
#define WSAEMFILE (WSABASEERR + 24)
#define WSAEWOULDBLOCK (WSABASEERR + 35)
#define WSAEINPROGRESS (WSABASEERR + 36)
#define WSAEALREADY (WSABASEERR + 37)
#define WSAENOTSOCK (WSABASEERR + 38)
#define WSAEDESTADDRREQ (WSABASEERR + 39)
#define WSAEMSGSIZE (WSABASEERR + 40)
#define WSAEPROTOTYPE (WSABASEERR + 41)
#define WSAENOPROTOOPT (WSABASEERR + 42)
#define WSAEPROTONOSUPPORT (WSABASEERR + 43)
#define WSAESOCKTNOSUPPORT (WSABASEERR + 44)
#define WSAEOPNOTSUPP (WSABASEERR + 45)
#define WSAEPFNOSUPPORT (WSABASEERR + 46)
#define WSAEAFNOSUPPORT (WSABASEERR + 47)
#define WSAEADDRINUSE (WSABASEERR + 48)
#define WSAEADDRNOTAVAIL (WSABASEERR + 49)
#define WSAENETDOWN (WSABASEERR + 50)
#define WSAENETUNREACH (WSABASEERR + 51)
#define WSAENETRESET (WSABASEERR + 52)
#define WSAECONNABORTED (WSABASEERR + 53)
#define WSAECONNRESET (WSABASEERR + 54)
#define WSAENOBUFS (WSABASEERR + 55)
#define WSAEISCONN (WSABASEERR + 56)
#define WSAENOTCONN (WSABASEERR + 57)
#define WSAESHUTDOWN (WSABASEERR + 58)
#define WSAETOOMANYREFS (WSABASEERR + 59)
#define WSAETIMEDOUT (WSABASEERR + 60)
#define WSAECONNREFUSED (WSABASEERR + 61)
#define WSAELOOP (WSABASEERR + 62)
#define WSAENAMETOOLONG (WSABASEERR + 63)
#define WSAEHOSTDOWN (WSABASEERR + 64)
#define WSAEHOSTUNREACH (WSABASEERR + 65)
#define WSAENOTEMPTY (WSABASEERR + 66)
#define WSAEPROCLIM (WSABASEERR + 67)
#define WSAEUSERS (WSABASEERR + 68)
#define WSAEDQUOT (WSABASEERR + 69)
#define WSAESTALE (WSABASEERR + 70)
#define WSAEREMOTE (WSABASEERR + 71)
#define WSASYSNOTREADY (WSABASEERR + 91)
#define WSAVERNOTSUPPORTED (WSABASEERR + 92)
#define WSANOTINITIALISED (WSABASEERR + 93)
#define WSAEDISCON (WSABASEERR + 101)
#define WSAHOST_NOT_FOUND (WSABASEERR + 1001)
#define WSATRY_AGAIN (WSABASEERR + 1002)
#define WSANO_RECOVERY (WSABASEERR + 1003)
#define WSANO_DATA (WSABASEERR + 1004)
#define WSANO_ADDRESS WSANO_DATA

#define WSADESCRIPTION_LEN 256
#define WSASYS_STATUS_LEN 128
typedef struct WSAData {
  WORD wVersion;
  WORD wHighVersion;
#if defined(__LP64__) || defined(_WIN64)
  unsigned short iMaxSockets;
  unsigned short iMaxUdpDg;
  char* lpVendorInfo;
  char szDescription[WSADESCRIPTION_LEN + 1];
  char szSystemStatus[WSASYS_STATUS_LEN + 1];
#else
  char szDescription[WSADESCRIPTION_LEN + 1];
  char szSystemStatus[WSASYS_STATUS_LEN + 1];
  unsigned short iMaxSockets;
  unsigned short iMaxUdpDg;
  char* lpVendorInfo;
#endif
} WSADATA, *LPWSADATA;

#define MAX_PROTOCOL_CHAIN 7
#define WSAPROTOCOL_LEN 255
typedef struct _WSAPROTOCOLCHAIN {
  int ChainLen;
  DWORD ChainEntries[MAX_PROTOCOL_CHAIN];
} WSAPROTOCOLCHAIN, *LPWSAPROTOCOLCHAIN;
typedef struct _WSAPROTOCOL_INFOA {
  DWORD dwServiceFlags1;
  DWORD dwServiceFlags2;
  DWORD dwServiceFlags3;
  DWORD dwServiceFlags4;
  DWORD dwProviderFlags;
  GUID ProviderId;
  DWORD dwCatalogEntryId;
  WSAPROTOCOLCHAIN ProtocolChain;
  int iVersion;
  int iAddressFamily;
  int iMaxSockAddr;
  int iMinSockAddr;
  int iSocketType;
  int iProtocol;
  int iProtocolMaxOffset;
  int iNetworkByteOrder;
  int iSecurityScheme;
  DWORD dwMessageSize;
  DWORD dwProviderReserved;
  CHAR szProtocol[WSAPROTOCOL_LEN + 1];
} WSAPROTOCOL_INFOA, *LPWSAPROTOCOL_INFOA;
typedef struct _WSAPROTOCOL_INFOW {
  DWORD dwServiceFlags1;
  DWORD dwServiceFlags2;
  DWORD dwServiceFlags3;
  DWORD dwServiceFlags4;
  DWORD dwProviderFlags;
  GUID ProviderId;
  DWORD dwCatalogEntryId;
  WSAPROTOCOLCHAIN ProtocolChain;
  int iVersion;
  int iAddressFamily;
  int iMaxSockAddr;
  int iMinSockAddr;
  int iSocketType;
  int iProtocol;
  int iProtocolMaxOffset;
  int iNetworkByteOrder;
  int iSecurityScheme;
  DWORD dwMessageSize;
  DWORD dwProviderReserved;
  WCHAR szProtocol[WSAPROTOCOL_LEN + 1];
} WSAPROTOCOL_INFOW, *LPWSAPROTOCOL_INFOW;

extern "C" {
int WSAAPI WSAStartup(WORD wVersionRequested, LPWSADATA lpWSAData);
int WSAAPI WSACleanup(void);
int WSAAPI WSAGetLastError(void);
void WSAAPI WSASetLastError(int iError);
int WSAAPI WSAAsyncSelect(SOCKET s, HWND hWnd, UINT wMsg, LONG lEvent);
int WSAAPI closesocket(SOCKET s);
// argp points to a 32-bit value, as on Windows.
int WSAAPI ioctlsocket(SOCKET s, LONG cmd, ULONG* argp);
int WSAAPI WSAStringToAddressA(LPSTR AddressString, INT AddressFamily, LPWSAPROTOCOL_INFOA lpProtocolInfo,
                               LPSOCKADDR lpAddress, LPINT lpAddressLength);
int WSAAPI WSAStringToAddressW(LPWSTR AddressString, INT AddressFamily, LPWSAPROTOCOL_INFOW lpProtocolInfo,
                               LPSOCKADDR lpAddress, LPINT lpAddressLength);
int WSAAPI WSAAddressToStringA(LPSOCKADDR lpsaAddress, DWORD dwAddressLength,
                               LPWSAPROTOCOL_INFOA lpProtocolInfo, LPSTR lpszAddressString,
                               LPDWORD lpdwAddressStringLength);
int WSAAPI WSAAddressToStringW(LPSOCKADDR lpsaAddress, DWORD dwAddressLength,
                               LPWSAPROTOCOL_INFOW lpProtocolInfo, LPWSTR lpszAddressString,
                               LPDWORD lpdwAddressStringLength);
}

#ifdef UNICODE
#define WSAStringToAddress WSAStringToAddressW
#define WSAAddressToString WSAAddressToStringW
#define WSAPROTOCOL_INFO WSAPROTOCOL_INFOW
#define LPWSAPROTOCOL_INFO LPWSAPROTOCOL_INFOW
#define SO_PROTOCOL_INFO SO_PROTOCOL_INFOW
#else
#define WSAStringToAddress WSAStringToAddressA
#define WSAAddressToString WSAAddressToStringA
#define WSAPROTOCOL_INFO WSAPROTOCOL_INFOA
#define LPWSAPROTOCOL_INFO LPWSAPROTOCOL_INFOA
#define SO_PROTOCOL_INFO SO_PROTOCOL_INFOA
#endif

// Windows passes address and option lengths as int; the system takes
// socklen_t. These overloads let Windows code call the system functions.
inline SOCKET accept(SOCKET s, struct sockaddr* addr, int* addrlen) {
  socklen_t n = addrlen != NULL ? (socklen_t)*addrlen : 0;
  int fd = ::accept((int)s, addr, addrlen != NULL ? &n : NULL);
  if (addrlen != NULL) *addrlen = (int)n;
  return fd < 0 ? INVALID_SOCKET : (SOCKET)fd;
}
inline int getpeername(SOCKET s, struct sockaddr* name, int* namelen) {
  socklen_t n = (socklen_t)*namelen;
  int r = ::getpeername((int)s, name, &n);
  *namelen = (int)n;
  return r;
}
inline int getsockname(SOCKET s, struct sockaddr* name, int* namelen) {
  socklen_t n = (socklen_t)*namelen;
  int r = ::getsockname((int)s, name, &n);
  *namelen = (int)n;
  return r;
}
inline int getsockopt(SOCKET s, int level, int optname, char* optval, int* optlen) {
  socklen_t n = (socklen_t)*optlen;
  int r = ::getsockopt((int)s, level, optname, optval, &n);
  *optlen = (int)n;
  return r;
}
inline int recvfrom(SOCKET s, char* buf, int len, int flags, struct sockaddr* from, int* fromlen) {
  socklen_t n = fromlen != NULL ? (socklen_t)*fromlen : 0;
  int r = (int)::recvfrom((int)s, buf, (size_t)len, flags, from, fromlen != NULL ? &n : NULL);
  if (fromlen != NULL) *fromlen = (int)n;
  return r;
}

#endif
