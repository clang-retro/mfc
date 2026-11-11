// winsock.cpp - Windows Sockets over BSD sockets: startup, errors in
// Winsock terms, closing, ioctls, and address text.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include "winport.h"

#include <winsock2.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>

using namespace winport;

namespace {

struct ErrnoMap {
  int e;
  int wsa;
};
const ErrnoMap kErrors[] = {
    {EINTR, WSAEINTR},
    {EBADF, WSAENOTSOCK},
    {EACCES, WSAEACCES},
    {EFAULT, WSAEFAULT},
    {EINVAL, WSAEINVAL},
    {EMFILE, WSAEMFILE},
    {EWOULDBLOCK, WSAEWOULDBLOCK},
    // A non-blocking connect that has started reports EINPROGRESS; Winsock
    // reports WSAEWOULDBLOCK, which is what Windows code checks for.
    {EINPROGRESS, WSAEWOULDBLOCK},
    {EALREADY, WSAEALREADY},
    {ENOTSOCK, WSAENOTSOCK},
    {EDESTADDRREQ, WSAEDESTADDRREQ},
    {EMSGSIZE, WSAEMSGSIZE},
    {EPROTOTYPE, WSAEPROTOTYPE},
    {ENOPROTOOPT, WSAENOPROTOOPT},
    {EPROTONOSUPPORT, WSAEPROTONOSUPPORT},
    {ESOCKTNOSUPPORT, WSAESOCKTNOSUPPORT},
    {EOPNOTSUPP, WSAEOPNOTSUPP},
    {EPFNOSUPPORT, WSAEPFNOSUPPORT},
    {EAFNOSUPPORT, WSAEAFNOSUPPORT},
    {EADDRINUSE, WSAEADDRINUSE},
    {EADDRNOTAVAIL, WSAEADDRNOTAVAIL},
    {ENETDOWN, WSAENETDOWN},
    {ENETUNREACH, WSAENETUNREACH},
    {ENETRESET, WSAENETRESET},
    {ECONNABORTED, WSAECONNABORTED},
    {ECONNRESET, WSAECONNRESET},
    {EPIPE, WSAECONNRESET},
    {ENOBUFS, WSAENOBUFS},
    {EISCONN, WSAEISCONN},
    {ENOTCONN, WSAENOTCONN},
    {ESHUTDOWN, WSAESHUTDOWN},
    {ETOOMANYREFS, WSAETOOMANYREFS},
    {ETIMEDOUT, WSAETIMEDOUT},
    {ECONNREFUSED, WSAECONNREFUSED},
    {ELOOP, WSAELOOP},
    {ENAMETOOLONG, WSAENAMETOOLONG},
    {EHOSTDOWN, WSAEHOSTDOWN},
    {EHOSTUNREACH, WSAEHOSTUNREACH},
    {ENOTEMPTY, WSAENOTEMPTY},
};

// A Winsock code set with WSASetLastError, which stays until the next
// system call changes errno.
thread_local int t_wsaError;
thread_local int t_wsaErrno = -1;

int ToErrno(int wsa) {
  for (const ErrnoMap& m : kErrors)
    if (m.wsa == wsa) return m.e;
  return 0;
}

}  // namespace

extern "C" {

int WSAAPI WSAStartup(WORD version, LPWSADATA data) {
  if (data == NULL) return WSAEFAULT;
  memset(data, 0, sizeof(*data));
  data->wVersion = LOBYTE(version) >= 2 ? MAKEWORD(2, 2) : version;
  data->wHighVersion = MAKEWORD(2, 2);
  strcpy(data->szDescription, "WinSock 2.0");
  strcpy(data->szSystemStatus, "Running");
  data->iMaxSockets = 0;
  data->iMaxUdpDg = 0;
  if (LOBYTE(version) < 1) return WSAVERNOTSUPPORTED;
  return 0;
}

int WSAAPI WSACleanup(void) { return 0; }

int WSAAPI WSAGetLastError(void) {
  int e = errno;
  if (t_wsaErrno == e && t_wsaError != 0) return t_wsaError;
  if (e == 0) return 0;
  for (const ErrnoMap& m : kErrors)
    if (m.e == e) return m.wsa;
  return WSABASEERR + e;
}

void WSAAPI WSASetLastError(int error) {
  int e = ToErrno(error);
  errno = e != 0 ? e : (error == 0 ? 0 : EIO);
  t_wsaError = error;
  t_wsaErrno = errno;
}

int WSAAPI closesocket(SOCKET s) { return close((int)s) == 0 ? 0 : SOCKET_ERROR; }

int WSAAPI ioctlsocket(SOCKET s, LONG cmd, ULONG* argp) {
  if (argp == NULL) {
    WSASetLastError(WSAEFAULT);
    return SOCKET_ERROR;
  }
  if (cmd == (LONG)FIONBIO) {
    int flags = fcntl((int)s, F_GETFL);
    if (flags < 0) return SOCKET_ERROR;
    flags = *argp ? (flags | O_NONBLOCK) : (flags & ~O_NONBLOCK);
    return fcntl((int)s, F_SETFL, flags) == 0 ? 0 : SOCKET_ERROR;
  }
  if (cmd == (LONG)FIONREAD) {
    int n = 0;
    if (ioctl((int)s, FIONREAD, &n) != 0) return SOCKET_ERROR;
    *argp = (ULONG)n;
    return 0;
  }
  if (cmd == (LONG)SIOCATMARK) {
    int n = 0;
    if (ioctl((int)s, SIOCATMARK, &n) != 0) return SOCKET_ERROR;
    // TRUE when there is no urgent data to read.
    *argp = n ? 0 : 1;
    return 0;
  }
  WSASetLastError(WSAEINVAL);
  return SOCKET_ERROR;
}

// Address text: "a.b.c.d[:port]" and "[v6%scope]:port" or "v6%scope".
int WSAAPI WSAStringToAddressA(LPSTR text, INT family, LPWSAPROTOCOL_INFOA, LPSOCKADDR addr, LPINT len) {
  if (text == NULL || addr == NULL || len == NULL) {
    WSASetLastError(WSAEFAULT);
    return SOCKET_ERROR;
  }
  std::string s = text;
  if (family == AF_INET) {
    if (*len < (int)sizeof(sockaddr_in)) {
      *len = sizeof(sockaddr_in);
      WSASetLastError(WSAEFAULT);
      return SOCKET_ERROR;
    }
    sockaddr_in sa;
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    size_t colon = s.find(':');
    if (colon != std::string::npos) {
      sa.sin_port = htons((u_short)atoi(s.c_str() + colon + 1));
      s.resize(colon);
    }
    if (inet_pton(AF_INET, s.c_str(), &sa.sin_addr) != 1) {
      WSASetLastError(WSAEINVAL);
      return SOCKET_ERROR;
    }
    memcpy(addr, &sa, sizeof(sa));
    *len = sizeof(sa);
    return 0;
  }
  if (family == AF_INET6) {
    if (*len < (int)sizeof(sockaddr_in6)) {
      *len = sizeof(sockaddr_in6);
      WSASetLastError(WSAEFAULT);
      return SOCKET_ERROR;
    }
    sockaddr_in6 sa;
    memset(&sa, 0, sizeof(sa));
    sa.sin6_family = AF_INET6;
    if (!s.empty() && s[0] == '[') {
      size_t close = s.find(']');
      if (close == std::string::npos) {
        WSASetLastError(WSAEINVAL);
        return SOCKET_ERROR;
      }
      if (close + 1 < s.size() && s[close + 1] == ':') sa.sin6_port = htons((u_short)atoi(s.c_str() + close + 2));
      s = s.substr(1, close - 1);
    }
    size_t pct = s.find('%');
    if (pct != std::string::npos) {
      sa.sin6_scope_id = (uint32_t)strtoul(s.c_str() + pct + 1, NULL, 10);
      s.resize(pct);
    }
    if (inet_pton(AF_INET6, s.c_str(), &sa.sin6_addr) != 1) {
      WSASetLastError(WSAEINVAL);
      return SOCKET_ERROR;
    }
    memcpy(addr, &sa, sizeof(sa));
    *len = sizeof(sa);
    return 0;
  }
  WSASetLastError(WSAEAFNOSUPPORT);
  return SOCKET_ERROR;
}

int WSAAPI WSAStringToAddressW(LPWSTR text, INT family, LPWSAPROTOCOL_INFOW, LPSOCKADDR addr, LPINT len) {
  std::string s = text ? Narrow(text) : std::string();
  return WSAStringToAddressA(text ? &s[0] : NULL, family, NULL, addr, len);
}

int WSAAPI WSAAddressToStringA(LPSOCKADDR addr, DWORD len, LPWSAPROTOCOL_INFOA, LPSTR out, LPDWORD cch) {
  if (addr == NULL || cch == NULL) {
    WSASetLastError(WSAEFAULT);
    return SOCKET_ERROR;
  }
  char host[INET6_ADDRSTRLEN];
  char text[INET6_ADDRSTRLEN + 32];
  if (addr->sa_family == AF_INET && len >= sizeof(sockaddr_in)) {
    const sockaddr_in* sa = (const sockaddr_in*)addr;
    inet_ntop(AF_INET, &sa->sin_addr, host, sizeof(host));
    if (sa->sin_port != 0)
      snprintf(text, sizeof(text), "%s:%u", host, ntohs(sa->sin_port));
    else
      snprintf(text, sizeof(text), "%s", host);
  } else if (addr->sa_family == AF_INET6 && len >= sizeof(sockaddr_in6)) {
    const sockaddr_in6* sa = (const sockaddr_in6*)addr;
    inet_ntop(AF_INET6, &sa->sin6_addr, host, sizeof(host));
    std::string h = host;
    if (sa->sin6_scope_id != 0) h += "%" + std::to_string(sa->sin6_scope_id);
    if (sa->sin6_port != 0)
      snprintf(text, sizeof(text), "[%s]:%u", h.c_str(), ntohs(sa->sin6_port));
    else
      snprintf(text, sizeof(text), "%s", h.c_str());
  } else {
    WSASetLastError(WSAEINVAL);
    return SOCKET_ERROR;
  }
  DWORD need = (DWORD)strlen(text) + 1;
  if (out == NULL || *cch < need) {
    *cch = need;
    WSASetLastError(WSAEFAULT);
    return SOCKET_ERROR;
  }
  memcpy(out, text, need);
  *cch = need;
  return 0;
}

int WSAAPI WSAAddressToStringW(LPSOCKADDR addr, DWORD len, LPWSAPROTOCOL_INFOW, LPWSTR out, LPDWORD cch) {
  char text[INET6_ADDRSTRLEN + 32];
  DWORD n = sizeof(text);
  if (cch == NULL) {
    WSASetLastError(WSAEFAULT);
    return SOCKET_ERROR;
  }
  if (WSAAddressToStringA(addr, len, NULL, text, &n) != 0) return SOCKET_ERROR;
  std::wstring w = Widen(text);
  if (out == NULL || *cch < w.size() + 1) {
    *cch = (DWORD)w.size() + 1;
    WSASetLastError(WSAEFAULT);
    return SOCKET_ERROR;
  }
  wmemcpy(out, w.c_str(), w.size() + 1);
  *cch = (DWORD)w.size() + 1;
  return 0;
}

}  // extern "C"
