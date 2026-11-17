// winsock.cpp - Windows Sockets over BSD sockets: startup, errors in
// Winsock terms, closing, ioctls, and address text.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include "winport.h"

#include <winsock2.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <map>
#include <mutex>
#include <thread>
#include <vector>
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
#ifndef MSG_NOSIGNAL
  signal(SIGPIPE, SIG_IGN);
#endif
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

// ---------------------------------------------------------------------
// WSAAsyncSelect: a thread polls the sockets that asked for events and
// posts each event to its window. As on Windows, an event once posted is
// not posted again until the call that answers it (recv for FD_READ,
// accept for FD_ACCEPT, a send that would block for FD_WRITE).

}  // extern "C"

namespace {

struct AsyncSocket {
  HWND hwnd;
  UINT msg;
  LONG events;   // asked for
  LONG enabled;  // may be posted now
  bool connecting = false;
  // Once connected, a socket is watched until it closes: after both ends
  // shut down, getpeername fails, but FD_CLOSE is still to come.
  bool connected = false;
};

struct AsyncSelect {
  std::mutex lock;
  std::map<int, AsyncSocket> sockets;
  int wake[2] = {-1, -1};
  bool started = false;

  void Wake() {
    char c = 0;
    if (wake[1] >= 0) (void)!write(wake[1], &c, 1);
  }

  void Start() {
    if (started) return;
    started = true;
    if (pipe(wake) == 0) {
      fcntl(wake[0], F_SETFL, O_NONBLOCK);
      fcntl(wake[1], F_SETFL, O_NONBLOCK);
    }
    std::thread([this] { Run(); }).detach();
  }

  static bool Listening(int fd) {
    int v = 0;
    socklen_t n = sizeof v;
    return getsockopt(fd, SOL_SOCKET, SO_ACCEPTCONN, &v, &n) == 0 && v;
  }
  static bool Stream(int fd) {
    int v = 0;
    socklen_t n = sizeof v;
    return getsockopt(fd, SOL_SOCKET, SO_TYPE, &v, &n) == 0 && v == SOCK_STREAM;
  }
  static bool Connected(int fd) {
    sockaddr_storage a;
    socklen_t n = sizeof a;
    return getpeername(fd, (sockaddr*)&a, &n) == 0;
  }

  void Post(int fd, AsyncSocket& a, LONG event, int error) {
    a.enabled &= ~event;
    PostMessageW(a.hwnd, a.msg, (WPARAM)fd, WSAMAKESELECTREPLY(event, error));
  }

  void Run() {
    for (;;) {
      std::vector<pollfd> fds;
      {
        std::lock_guard<std::mutex> g(lock);
        fds.push_back({wake[0], POLLIN, 0});
        for (auto& it : sockets) {
          int fd = it.first;
          AsyncSocket& a = it.second;
          short want = 0;
          bool stream = Stream(fd);
          if (stream && !a.connected && !a.connecting && !Listening(fd)) {
            if (!Connected(fd)) continue;
            a.connected = true;
          }
          if (a.enabled & (FD_READ | FD_ACCEPT | FD_CLOSE | FD_OOB)) want |= POLLIN | POLLPRI;
          if (a.enabled & (FD_WRITE | FD_CONNECT)) want |= POLLOUT;
          if (want) fds.push_back({fd, want, 0});
        }
      }
      if (poll(fds.data(), (nfds_t)fds.size(), 1000) <= 0) continue;
      if (fds[0].revents) {
        char buf[64];
        while (read(wake[0], buf, sizeof buf) > 0) {}
      }
      std::lock_guard<std::mutex> g(lock);
      for (size_t i = 1; i < fds.size(); i++) {
        if (!fds[i].revents) continue;
        auto it = sockets.find(fds[i].fd);
        if (it == sockets.end()) continue;
        int fd = it->first;
        AsyncSocket& a = it->second;
        short re = fds[i].revents;
        if (a.connecting && (re & (POLLOUT | POLLERR | POLLHUP))) {
          int err = 0;
          socklen_t n = sizeof err;
          getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &n);
          a.connecting = false;
          a.connected = err == 0;
          int wsa = 0;
          if (err) {
            errno = err;
            wsa = WSAGetLastError();
          }
          if (a.enabled & FD_CONNECT) Post(fd, a, FD_CONNECT, wsa);
          if (err) continue;
          if (a.enabled & FD_WRITE) Post(fd, a, FD_WRITE, 0);
          continue;
        }
        if ((re & POLLPRI) && (a.enabled & FD_OOB)) Post(fd, a, FD_OOB, 0);
        if (re & (POLLIN | POLLHUP | POLLERR)) {
          if (Listening(fd)) {
            if (a.enabled & FD_ACCEPT) Post(fd, a, FD_ACCEPT, 0);
          } else {
            char c;
            ssize_t r = ::recv(fd, (void*)&c, (size_t)1, MSG_PEEK | MSG_DONTWAIT);
            if (r > 0 || !Stream(fd)) {
              if (a.enabled & FD_READ) Post(fd, a, FD_READ, 0);
            } else if (r == 0 || (errno != EAGAIN && errno != EWOULDBLOCK)) {
              int wsa = r == 0 ? 0 : WSAGetLastError();
              if (a.enabled & FD_CLOSE) Post(fd, a, FD_CLOSE, wsa);
              a.enabled &= ~(FD_READ | FD_CLOSE | FD_OOB);
            }
          }
        }
        if ((re & POLLOUT) && (a.enabled & FD_WRITE)) Post(fd, a, FD_WRITE, 0);
      }
    }
  }
};

AsyncSelect& Async() {
  static AsyncSelect* a = new AsyncSelect;  // the thread outlives exit
  return *a;
}

}  // namespace

extern "C" {

int WSAAPI WSAAsyncSelect(SOCKET s, HWND hWnd, UINT wMsg, LONG lEvent) {
  int fd = (int)s;
  int flags = fcntl(fd, F_GETFL);
  if (flags < 0) {
    WSASetLastError(WSAENOTSOCK);
    return SOCKET_ERROR;
  }
  // The socket turns non-blocking, as on Windows.
  fcntl(fd, F_SETFL, flags | O_NONBLOCK);
  AsyncSelect& as = Async();
  std::lock_guard<std::mutex> g(as.lock);
  if (lEvent == 0 || hWnd == NULL) {
    as.sockets.erase(fd);
  } else {
    as.Start();
    AsyncSocket& a = as.sockets[fd];
    bool connecting = a.connecting, connected = a.connected;
    a = AsyncSocket{hWnd, wMsg, lEvent, lEvent};
    a.connecting = connecting;
    a.connected = connected;
  }
  as.Wake();
  return 0;
}

extern "C" void WINAPI _WinportSocketCalled(SOCKET s, LONG events, int result) {
  int e = errno;
  AsyncSelect& as = Async();
  {
    std::lock_guard<std::mutex> g(as.lock);
    auto it = as.sockets.find((int)s);
    if (it != as.sockets.end()) {
      AsyncSocket& a = it->second;
      if (events == FD_CONNECT) {
        if (result < 0 && (e == EINPROGRESS || e == EWOULDBLOCK)) a.connecting = true;
        if (result == 0) {
          a.connected = true;
          // Connected at once: say so, and that it can send.
          if (a.events & FD_CONNECT) as.Post((int)s, a, FD_CONNECT, 0);
          a.enabled |= a.events & FD_WRITE;
        }
      } else if (events == FD_ACCEPT) {
        a.enabled |= a.events & FD_ACCEPT;
        if (result >= 0) {
          // The new socket has the listening socket's events, as on Windows.
          int flags = fcntl(result, F_GETFL);
          if (flags >= 0) fcntl(result, F_SETFL, flags | O_NONBLOCK);
          AsyncSocket& n = as.sockets[result];
          n = AsyncSocket{a.hwnd, a.msg, a.events, a.events & ~(FD_ACCEPT | FD_CONNECT)};
          n.connected = true;
        }
      } else if (events == FD_WRITE) {
        // Only a send that would block re-enables FD_WRITE.
        if (result < 0 && (e == EWOULDBLOCK || e == EAGAIN)) a.enabled |= a.events & FD_WRITE;
      } else {
        a.enabled |= a.events & (events | (events == FD_READ ? FD_CLOSE : 0));
      }
      as.Wake();
    }
  }
  errno = e;
}

int WSAAPI closesocket(SOCKET s) {
  {
    AsyncSelect& as = Async();
    std::lock_guard<std::mutex> g(as.lock);
    if (as.sockets.erase((int)s)) as.Wake();
  }
  return close((int)s) == 0 ? 0 : SOCKET_ERROR;
}

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
