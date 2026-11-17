// wininet.cpp - the Internet functions over BSD sockets: sessions,
// connections and status callbacks, URLs, HTTP requests and their
// headers, FTP (passive and active transfers, directories and listings),
// and the process's cookies.
//
// Each handle is an object of its own kind: the session (InternetOpen),
// a connection to a server (InternetConnect), and what a connection
// opens (an HTTP request, an FTP file or listing). Calls block, as they
// do on Windows without INTERNET_FLAG_ASYNC. HTTPS is not there: an
// https URL fails to connect.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include "winport.h"

#include <wininet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <algorithm>
#include <map>
#include <mutex>
#include <string>
#include <vector>

using namespace winport;

namespace {

// ---------------------------------------------------------------------
// Handles

const DWORD kMagic = 0x74656e69;  // "inet"

struct Handle {
  DWORD magic = kMagic;
  DWORD type = 0;  // INTERNET_HANDLE_TYPE_*
  Handle* parent = NULL;
  DWORD_PTR context = 0;
  DWORD flags = 0;
  INTERNET_STATUS_CALLBACK callback = NULL;
  bool ownsParent = false;  // from InternetOpenUrl: closing it closes the connection
  virtual ~Handle() { magic = 0; }
};

struct Session : Handle {
  std::wstring agent;
  DWORD connectTimeout = 60000, sendTimeout = 30000, receiveTimeout = 30000;
};

struct Connection : Handle {
  DWORD service = 0;
  std::wstring server, user, password;
  INTERNET_PORT port = 0;
  int control = -1;  // FTP's control connection
  bool busy = false;  // an FTP transfer is open
  Session* session() {
    Handle* h = parent;
    return (Session*)h;
  }
};

struct Request : Handle {
  std::wstring verb, object, version;
  std::vector<std::pair<std::string, std::string>> headers;  // the request's
  int sock = -1;
  bool sent = false;
  // The response.
  std::string statusLine, httpVersion, statusText;
  DWORD status = 0;
  std::vector<std::pair<std::string, std::string>> response;
  long long contentLength = -1;
  bool chunked = false;
  long long chunkLeft = 0;
  bool bodyDone = false;
  std::string pending;  // body bytes read but not yet given out
  DWORD position = 0;
  // HttpSendRequestEx: the body goes out through InternetWriteFile.
  bool writing = false;
  Connection* conn() { return (Connection*)parent; }
};

struct FtpFile : Handle {
  int data = -1;
  bool write = false;
  bool closed = false;
  long long size = -1;
  DWORD position = 0;
  Connection* conn() { return (Connection*)parent; }
};

struct FtpFind : Handle {
  std::vector<WIN32_FIND_DATAW> entries;
  size_t next = 0;
};

std::mutex g_lock;  // the set of live handles
std::vector<Handle*>& LiveHandles() {
  static auto* v = new std::vector<Handle*>;
  return *v;
}

Handle* Register(Handle* h) {
  std::lock_guard<std::mutex> g(g_lock);
  LiveHandles().push_back(h);
  return h;
}

Handle* InetHandle(HINTERNET hi) {
  std::lock_guard<std::mutex> g(g_lock);
  for (Handle* h : LiveHandles())
    if (h == (Handle*)hi) return h;
  return NULL;
}

template <class T>
T* As(HINTERNET hi, DWORD type1, DWORD type2 = 0) {
  Handle* h = InetHandle(hi);
  if (!h) {
    SetLastError(ERROR_INVALID_HANDLE);
    return NULL;
  }
  if (h->type != type1 && (type2 == 0 || h->type != type2)) {
    SetLastError(ERROR_INTERNET_INCORRECT_HANDLE_TYPE);
    return NULL;
  }
  return (T*)h;
}

Session* SessionOf(Handle* h) {
  while (h && h->type != INTERNET_HANDLE_TYPE_INTERNET) h = h->parent;
  return (Session*)h;
}

void Notify(Handle* h, DWORD status, LPVOID info = NULL, DWORD len = 0) {
  if (h && h->callback && h->callback != INTERNET_INVALID_STATUS_CALLBACK)
    h->callback((HINTERNET)h, h->context, status, info, len);
}

// A new handle under a parent takes its status callback.
template <class T>
T* NewChild(Handle* parent, DWORD type, DWORD_PTR context, DWORD flags) {
  T* h = new T;
  h->type = type;
  h->parent = parent;
  h->context = context;
  h->flags = flags;
  h->callback = parent ? parent->callback : NULL;
  Register(h);
  HINTERNET hi = (HINTERNET)h;
  Notify(h, INTERNET_STATUS_HANDLE_CREATED, &hi, sizeof(hi));
  return h;
}

// The last FTP reply, for InternetGetLastResponseInfo.
thread_local std::string t_response;
thread_local DWORD t_responseCode;

// ---------------------------------------------------------------------
// Text

std::string Lower(std::string s) {
  for (char& c : s) c = (char)tolower((unsigned char)c);
  return s;
}

std::string Trim(const std::string& s) {
  size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
  return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}

std::wstring W(LPCSTR s, int n = -1) { return s ? Widen(s, n) : std::wstring(); }
std::string U(LPCWSTR s, int n = -1) { return s ? Narrow(s, n) : std::string(); }

// A string result into a caller's buffer: lengths in bytes or characters.
template <class C>
BOOL PutString(const std::basic_string<C>& s, LPVOID buf, LPDWORD len, bool bytes) {
  DWORD unit = bytes ? sizeof(C) : 1;
  DWORD need = (DWORD)(s.size() + 1) * unit;
  if (!len) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  if (!buf || *len < need) {
    *len = need;
    SetLastError(ERROR_INSUFFICIENT_BUFFER);
    return FALSE;
  }
  memcpy(buf, s.c_str(), (s.size() + 1) * sizeof(C));
  *len = (DWORD)s.size() * unit;
  return TRUE;
}

// ---------------------------------------------------------------------
// Sockets

int ConnectTo(Handle* h, const std::string& host, INTERNET_PORT port, DWORD timeout) {
  std::wstring wh = Widen(host.c_str());
  Notify(h, INTERNET_STATUS_RESOLVING_NAME, (LPVOID)wh.c_str(), (DWORD)(wh.size() + 1) * sizeof(WCHAR));
  addrinfo hints = {}, *res = NULL;
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  char ps[16];
  snprintf(ps, sizeof ps, "%u", port);
  if (getaddrinfo(host.c_str(), ps, &hints, &res) != 0 || !res) {
    SetLastError(ERROR_INTERNET_NAME_NOT_RESOLVED);
    return -1;
  }
  Notify(h, INTERNET_STATUS_NAME_RESOLVED, (LPVOID)wh.c_str(), (DWORD)(wh.size() + 1) * sizeof(WCHAR));
  int fd = -1;
  for (addrinfo* a = res; a && fd < 0; a = a->ai_next) {
    Notify(h, INTERNET_STATUS_CONNECTING_TO_SERVER, a->ai_addr, (DWORD)a->ai_addrlen);
    int s = socket(a->ai_family, a->ai_socktype, a->ai_protocol);
    if (s < 0) continue;
    int fl = fcntl(s, F_GETFL);
    fcntl(s, F_SETFL, fl | O_NONBLOCK);
    int r = connect(s, a->ai_addr, a->ai_addrlen);
    if (r < 0 && errno == EINPROGRESS) {
      pollfd p = {s, POLLOUT, 0};
      r = poll(&p, 1, timeout ? (int)timeout : -1) == 1 ? 0 : -1;
      if (r == 0) {
        int err = 0;
        socklen_t n = sizeof err;
        getsockopt(s, SOL_SOCKET, SO_ERROR, &err, &n);
        if (err) r = -1;
      }
    }
    if (r == 0) {
      fcntl(s, F_SETFL, fl);
      fd = s;
      Notify(h, INTERNET_STATUS_CONNECTED_TO_SERVER, a->ai_addr, (DWORD)a->ai_addrlen);
    } else {
      close(s);
    }
  }
  freeaddrinfo(res);
  if (fd < 0) {
    SetLastError(ERROR_INTERNET_CANNOT_CONNECT);
    return -1;
  }
#ifdef SO_NOSIGPIPE
  int one = 1;
  setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
#endif
  return fd;
}

#ifdef MSG_NOSIGNAL
const int kSendFlags = MSG_NOSIGNAL;
#else
const int kSendFlags = 0;
#endif

bool SendAll(int fd, const void* p, size_t n) {
  const char* c = (const char*)p;
  while (n > 0) {
    ssize_t r = send(fd, c, n, kSendFlags);
    if (r < 0 && errno == EINTR) continue;
    if (r <= 0) {
      SetLastError(ERROR_INTERNET_CONNECTION_ABORTED);
      return false;
    }
    c += r;
    n -= (size_t)r;
  }
  return true;
}

bool SendAll(int fd, const std::string& s) { return SendAll(fd, s.data(), s.size()); }

// Reads a line (without its CR LF) from a socket, through a buffer of
// what came after it.
bool ReadLine(int fd, std::string& buffer, std::string& line) {
  for (;;) {
    size_t nl = buffer.find('\n');
    if (nl != std::string::npos) {
      line = buffer.substr(0, nl);
      if (!line.empty() && line.back() == '\r') line.pop_back();
      buffer.erase(0, nl + 1);
      return true;
    }
    char tmp[1024];
    ssize_t r = recv(fd, tmp, sizeof tmp, 0);
    if (r < 0 && errno == EINTR) continue;
    if (r <= 0) {
      if (buffer.empty()) return false;
      line = buffer;
      buffer.clear();
      return true;
    }
    buffer.append(tmp, (size_t)r);
  }
}

// ---------------------------------------------------------------------
// URLs

struct Url {
  std::string scheme, user, password, host, path, extra;
  INTERNET_PORT port = 0;
  INTERNET_SCHEME nScheme = INTERNET_SCHEME_UNKNOWN;
  // Where each part is in the original, for InternetCrackUrl's pointers.
  size_t schemeAt = 0, userAt = 0, passwordAt = 0, hostAt = 0, pathAt = 0, extraAt = 0;
};

INTERNET_SCHEME SchemeOf(const std::string& s) {
  std::string l = Lower(s);
  if (l == "http") return INTERNET_SCHEME_HTTP;
  if (l == "https") return INTERNET_SCHEME_HTTPS;
  if (l == "ftp") return INTERNET_SCHEME_FTP;
  if (l == "gopher") return INTERNET_SCHEME_GOPHER;
  if (l == "file") return INTERNET_SCHEME_FILE;
  if (l == "news") return INTERNET_SCHEME_NEWS;
  if (l == "mailto") return INTERNET_SCHEME_MAILTO;
  if (l == "socks") return INTERNET_SCHEME_SOCKS;
  return INTERNET_SCHEME_UNKNOWN;
}

INTERNET_PORT DefaultPort(INTERNET_SCHEME s) {
  switch (s) {
    case INTERNET_SCHEME_HTTP: return INTERNET_DEFAULT_HTTP_PORT;
    case INTERNET_SCHEME_HTTPS: return INTERNET_DEFAULT_HTTPS_PORT;
    case INTERNET_SCHEME_FTP: return INTERNET_DEFAULT_FTP_PORT;
    case INTERNET_SCHEME_GOPHER: return INTERNET_DEFAULT_GOPHER_PORT;
    default: return 0;
  }
}

std::string Decode(const std::string& s) {
  std::string r;
  for (size_t i = 0; i < s.size(); i++) {
    if (s[i] == '%' && i + 2 < s.size() && isxdigit((unsigned char)s[i + 1]) && isxdigit((unsigned char)s[i + 2])) {
      r += (char)strtol(s.substr(i + 1, 2).c_str(), NULL, 16);
      i += 2;
    } else {
      r += s[i];
    }
  }
  return r;
}

bool ParseUrl(const std::string& url, Url* u) {
  size_t colon = url.find(':');
  if (colon == std::string::npos || colon == 0) {
    SetLastError(ERROR_INTERNET_UNRECOGNIZED_SCHEME);
    return false;
  }
  for (size_t i = 0; i < colon; i++)
    if (!isalnum((unsigned char)url[i]) && url[i] != '+' && url[i] != '-' && url[i] != '.') {
      SetLastError(ERROR_INTERNET_UNRECOGNIZED_SCHEME);
      return false;
    }
  u->scheme = url.substr(0, colon);
  u->nScheme = SchemeOf(u->scheme);
  size_t p = colon + 1;
  if (url.compare(p, 2, "//") != 0) {
    // No authority: "mailto:x", "news:x".
    u->pathAt = p;
    u->path = url.substr(p);
    return true;
  }
  p += 2;
  size_t end = url.find_first_of("/?#", p);
  if (u->nScheme == INTERNET_SCHEME_FILE) end = url.find('/', p);
  if (end == std::string::npos) end = url.size();
  std::string auth = url.substr(p, end - p);
  size_t at = auth.rfind('@');
  size_t hostStart = p;
  if (at != std::string::npos) {
    std::string cred = auth.substr(0, at);
    size_t c = cred.find(':');
    u->userAt = p;
    u->user = cred.substr(0, c);
    if (c != std::string::npos) {
      u->passwordAt = p + c + 1;
      u->password = cred.substr(c + 1);
    }
    hostStart = p + at + 1;
    auth = auth.substr(at + 1);
  }
  u->hostAt = hostStart;
  size_t pc = auth.rfind(':');
  if (pc != std::string::npos && auth.find(']', pc) == std::string::npos) {
    std::string ps = auth.substr(pc + 1);
    for (char c : ps)
      if (!isdigit((unsigned char)c)) {
        SetLastError(ERROR_INTERNET_INVALID_URL);
        return false;
      }
    u->port = (INTERNET_PORT)atoi(ps.c_str());
    auth = auth.substr(0, pc);
  }
  u->host = auth;
  if (u->host.empty() && u->nScheme != INTERNET_SCHEME_FILE) {
    SetLastError(ERROR_INTERNET_INVALID_URL);
    return false;
  }
  if (!u->port) u->port = DefaultPort(u->nScheme);
  size_t q = u->nScheme == INTERNET_SCHEME_FILE ? std::string::npos : url.find_first_of("?#", end);
  u->pathAt = end;
  u->path = url.substr(end, q == std::string::npos ? std::string::npos : q - end);
  if (q != std::string::npos) {
    u->extraAt = q;
    u->extra = url.substr(q);
  }
  return true;
}

// ---------------------------------------------------------------------
// Cookies: name, value and path, by host.

struct Cookie {
  std::string name, value, path;
};
std::mutex g_cookieLock;
std::map<std::string, std::vector<Cookie>>& Cookies() {
  static auto* m = new std::map<std::string, std::vector<Cookie>>;
  return *m;
}

void StoreCookie(const std::string& host, const std::string& urlPath, const std::string& text) {
  // "name=value; path=/x; expires=..."
  std::vector<std::string> parts;
  size_t s = 0;
  while (s <= text.size()) {
    size_t e = text.find(';', s);
    if (e == std::string::npos) e = text.size();
    parts.push_back(Trim(text.substr(s, e - s)));
    s = e + 1;
  }
  if (parts.empty() || parts[0].empty()) return;
  Cookie c;
  size_t eq = parts[0].find('=');
  c.name = Trim(parts[0].substr(0, eq));
  c.value = eq == std::string::npos ? std::string() : Trim(parts[0].substr(eq + 1));
  size_t slash = urlPath.rfind('/');
  c.path = slash == std::string::npos || slash == 0 ? "/" : urlPath.substr(0, slash);
  for (size_t i = 1; i < parts.size(); i++) {
    std::string l = Lower(parts[i]);
    if (l.compare(0, 5, "path=") == 0) c.path = parts[i].substr(5);
  }
  std::lock_guard<std::mutex> g(g_cookieLock);
  auto& list = Cookies()[Lower(host)];
  for (auto& o : list)
    if (o.name == c.name && o.path == c.path) {
      o.value = c.value;
      return;
    }
  list.push_back(c);
}

std::string CookiesFor(const std::string& host, const std::string& path) {
  std::lock_guard<std::mutex> g(g_cookieLock);
  std::string r;
  auto it = Cookies().find(Lower(host));
  if (it == Cookies().end()) return r;
  std::string p = path.empty() ? "/" : path;
  for (auto& c : it->second) {
    if (p.compare(0, c.path.size(), c.path) != 0) continue;
    if (!r.empty()) r += "; ";
    r += c.name;
    if (!c.value.empty() || !c.name.empty()) r += "=" + c.value;
  }
  return r;
}

// ---------------------------------------------------------------------
// HTTP

std::string HostHeader(Connection* c) {
  std::string h = U(c->server.c_str());
  bool secure = c->flags & INTERNET_FLAG_SECURE;
  if (c->port != (secure ? INTERNET_DEFAULT_HTTPS_PORT : INTERNET_DEFAULT_HTTP_PORT))
    h += ":" + std::to_string(c->port);
  return h;
}

const char* HeaderName(DWORD level) {
  switch (level) {
    case HTTP_QUERY_MIME_VERSION: return "MIME-Version";
    case HTTP_QUERY_CONTENT_TYPE: return "Content-Type";
    case HTTP_QUERY_CONTENT_TRANSFER_ENCODING: return "Content-Transfer-Encoding";
    case HTTP_QUERY_CONTENT_ID: return "Content-ID";
    case HTTP_QUERY_CONTENT_DESCRIPTION: return "Content-Description";
    case HTTP_QUERY_CONTENT_LENGTH: return "Content-Length";
    case HTTP_QUERY_CONTENT_LANGUAGE: return "Content-Language";
    case HTTP_QUERY_ALLOW: return "Allow";
    case HTTP_QUERY_PUBLIC: return "Public";
    case HTTP_QUERY_DATE: return "Date";
    case HTTP_QUERY_EXPIRES: return "Expires";
    case HTTP_QUERY_LAST_MODIFIED: return "Last-Modified";
    case HTTP_QUERY_MESSAGE_ID: return "Message-ID";
    case HTTP_QUERY_URI: return "URI";
    case HTTP_QUERY_DERIVED_FROM: return "Derived-From";
    case HTTP_QUERY_COST: return "Cost";
    case HTTP_QUERY_LINK: return "Link";
    case HTTP_QUERY_PRAGMA: return "Pragma";
    case HTTP_QUERY_CONNECTION: return "Connection";
    case HTTP_QUERY_ACCEPT: return "Accept";
    case HTTP_QUERY_LOCATION: return "Location";
    case HTTP_QUERY_SERVER: return "Server";
    case HTTP_QUERY_USER_AGENT: return "User-Agent";
    case 25: return "Accept-Charset";
    case 26: return "Accept-Encoding";
    case 27: return "Accept-Language";
    case 28: return "Authorization";
    case 29: return "Content-Encoding";
    case 31: return "Forwarded";
    case 32: return "From";
    case 34: return "Orig-URI";
    case 35: return "Referer";
    case 36: return "Retry-After";
    case 38: return "Title";
    case 40: return "WWW-Authenticate";
    case 41: return "Proxy-Authenticate";
    case 42: return "Accept-Ranges";
    case 43: return "Set-Cookie";
    case 44: return "Cookie";
    case 46: return "Refresh";
    case 47: return "Content-Disposition";
    case 48: return "Age";
    case 49: return "Cache-Control";
    case 50: return "Content-Base";
    case 51: return "Content-Location";
    case 52: return "Content-MD5";
    case 53: return "Content-Range";
    case 54: return "ETag";
    case 55: return "Host";
    case 56: return "If-Match";
    case 57: return "If-Modified-Since";
    case 58: return "If-None-Match";
    case 59: return "If-Range";
    case 60: return "If-Unmodified-Since";
    case 61: return "Max-Forwards";
    case 62: return "Proxy-Authorization";
    case 63: return "Range";
    case 64: return "Transfer-Encoding";
    case 65: return "Upgrade";
    case 66: return "Vary";
    case 67: return "Via";
    case 68: return "Warning";
    case 69: return "Expect";
    default: return NULL;
  }
}

// Adds request headers from text ("Name: value\r\n..."), by the
// HTTP_ADDREQ_FLAG_* rules.
BOOL AddHeaders(Request* r, const std::string& text, DWORD mods) {
  size_t s = 0;
  BOOL ok = TRUE;
  while (s < text.size()) {
    size_t e = text.find('\n', s);
    if (e == std::string::npos) e = text.size();
    std::string line = text.substr(s, e - s);
    s = e + 1;
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (Trim(line).empty()) continue;
    size_t c = line.find(':');
    if (c == std::string::npos) {
      SetLastError(ERROR_HTTP_INVALID_HEADER);
      ok = FALSE;
      continue;
    }
    std::string name = Trim(line.substr(0, c)), value = Trim(line.substr(c + 1));
    auto found = r->headers.end();
    for (auto it = r->headers.begin(); it != r->headers.end(); ++it)
      if (Lower(it->first) == Lower(name)) {
        found = it;
        break;
      }
    if ((mods & HTTP_ADDREQ_FLAG_ADD_IF_NEW) && found != r->headers.end()) {
      SetLastError(ERROR_HTTP_HEADER_ALREADY_EXISTS);
      ok = FALSE;
    } else if (mods & HTTP_ADDREQ_FLAG_REPLACE) {
      if (found != r->headers.end()) {
        if (value.empty())
          r->headers.erase(found);
        else
          found->second = value;
      } else if (!value.empty() && (mods & HTTP_ADDREQ_FLAG_ADD)) {
        r->headers.push_back({name, value});
      }
    } else if ((mods & (HTTP_ADDREQ_FLAG_COALESCE_WITH_COMMA | HTTP_ADDREQ_FLAG_COALESCE_WITH_SEMICOLON)) &&
               found != r->headers.end()) {
      found->second += (mods & HTTP_ADDREQ_FLAG_COALESCE_WITH_SEMICOLON) ? "; " : ", ";
      found->second += value;
    } else {
      r->headers.push_back({name, value});
    }
  }
  return ok;
}

std::string RequestUrl(Request* r) {
  Connection* c = r->conn();
  std::string obj = U(r->object.c_str());
  if (obj.empty() || obj[0] != '/') obj = "/" + obj;
  return std::string((c->flags & INTERNET_FLAG_SECURE) ? "https://" : "http://") + HostHeader(c) + obj;
}

void ResetResponse(Request* r) {
  if (r->sock >= 0) close(r->sock);
  r->sock = -1;
  r->sent = false;
  r->statusLine.clear();
  r->httpVersion.clear();
  r->statusText.clear();
  r->status = 0;
  r->response.clear();
  r->contentLength = -1;
  r->chunked = false;
  r->chunkLeft = 0;
  r->bodyDone = false;
  r->pending.clear();
  r->position = 0;
  r->writing = false;
}

// Connects and sends the request line and headers (and the body, if it
// is here).
BOOL SendHead(Request* r, const std::string& extra, const void* body, DWORD bodyLen, long long totalLen) {
  Connection* c = r->conn();
  if (c->flags & INTERNET_FLAG_SECURE) {
    // No TLS in this layer.
    SetLastError(ERROR_INTERNET_CANNOT_CONNECT);
    return FALSE;
  }
  ResetResponse(r);
  Session* s = SessionOf(r);
  r->sock = ConnectTo(r, U(c->server.c_str()), c->port, s ? s->connectTimeout : 0);
  if (r->sock < 0) return FALSE;
  if (s && s->receiveTimeout) {
    timeval tv = {(time_t)(s->receiveTimeout / 1000), (int)(s->receiveTimeout % 1000) * 1000};
    setsockopt(r->sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
  }
  std::string obj = U(r->object.c_str());
  if (obj.empty()) obj = "/";
  std::string head = U(r->verb.c_str()) + " " + obj + " " + U(r->version.c_str()) + "\r\n";
  bool hasHost = false, hasAgent = false, hasLength = false;
  std::vector<std::pair<std::string, std::string>> all = r->headers;
  // The extra headers count for this request only.
  Request tmp;
  tmp.headers = all;
  AddHeaders(&tmp, extra, HTTP_ADDREQ_FLAG_ADD | HTTP_ADDREQ_FLAG_REPLACE);
  all = tmp.headers;
  tmp.magic = 0;
  for (auto& h : all) {
    std::string l = Lower(h.first);
    hasHost |= l == "host";
    hasAgent |= l == "user-agent";
    hasLength |= l == "content-length" || l == "transfer-encoding";
  }
  if (!hasHost) head += "Host: " + HostHeader(c) + "\r\n";
  if (!hasAgent && s && !s->agent.empty()) head += "User-Agent: " + U(s->agent.c_str()) + "\r\n";
  for (auto& h : all) head += h.first + ": " + h.second + "\r\n";
  if (!(r->flags & INTERNET_FLAG_NO_COOKIES)) {
    std::string ck = CookiesFor(U(c->server.c_str()), obj);
    if (!ck.empty()) head += "Cookie: " + ck + "\r\n";
  }
  std::string verb = U(r->verb.c_str());
  if (!hasLength && (totalLen > 0 || verb == "POST" || verb == "PUT"))
    head += "Content-Length: " + std::to_string(totalLen < 0 ? 0 : totalLen) + "\r\n";
  if (r->flags & (INTERNET_FLAG_RELOAD | INTERNET_FLAG_PRAGMA_NOCACHE)) head += "Pragma: no-cache\r\n";
  head += "\r\n";
  Notify(r, INTERNET_STATUS_SENDING_REQUEST);
  if (!SendAll(r->sock, head)) return FALSE;
  if (body && bodyLen && !SendAll(r->sock, body, bodyLen)) return FALSE;
  DWORD sentLen = (DWORD)(head.size() + bodyLen);
  Notify(r, INTERNET_STATUS_REQUEST_SENT, &sentLen, sizeof sentLen);
  return TRUE;
}

const std::string* FindResponseHeader(Request* r, const std::string& name, DWORD index = 0) {
  DWORD n = 0;
  for (auto& h : r->response)
    if (Lower(h.first) == Lower(name) && n++ == index) return &h.second;
  return NULL;
}

// Reads the status line and the headers.
BOOL ReadHead(Request* r) {
  Notify(r, INTERNET_STATUS_RECEIVING_RESPONSE);
  std::string line;
  for (;;) {
    if (!ReadLine(r->sock, r->pending, line)) {
      SetLastError(ERROR_INTERNET_CONNECTION_RESET);
      return FALSE;
    }
    if (line.compare(0, 5, "HTTP/") != 0) {
      SetLastError(ERROR_HTTP_INVALID_SERVER_RESPONSE);
      return FALSE;
    }
    r->statusLine = line;
    size_t sp1 = line.find(' ');
    r->httpVersion = line.substr(0, sp1);
    r->status = sp1 == std::string::npos ? 0 : (DWORD)atoi(line.c_str() + sp1 + 1);
    size_t sp2 = sp1 == std::string::npos ? std::string::npos : line.find(' ', sp1 + 1);
    r->statusText = sp2 == std::string::npos ? std::string() : line.substr(sp2 + 1);
    r->response.clear();
    while (ReadLine(r->sock, r->pending, line) && !line.empty()) {
      size_t c = line.find(':');
      if (c == std::string::npos) continue;
      r->response.push_back({Trim(line.substr(0, c)), Trim(line.substr(c + 1))});
    }
    if (r->status >= 200 || r->status < 100) break;  // 1xx: another head follows
  }
  if (const std::string* te = FindResponseHeader(r, "Transfer-Encoding"))
    r->chunked = Lower(*te).find("chunked") != std::string::npos;
  if (const std::string* cl = FindResponseHeader(r, "Content-Length"))
    if (!r->chunked) r->contentLength = atoll(cl->c_str());
  std::string verb = U(r->verb.c_str());
  if (verb == "HEAD" || r->status == 204 || r->status == 304) {
    r->contentLength = 0;
    r->chunked = false;
  }
  if (r->contentLength == 0) r->bodyDone = true;
  // Cookies the server sets.
  Connection* c = r->conn();
  if (!(r->flags & INTERNET_FLAG_NO_COOKIES))
    for (auto& h : r->response)
      if (Lower(h.first) == "set-cookie") StoreCookie(U(c->server.c_str()), U(r->object.c_str()), h.second);
  DWORD n = (DWORD)r->statusLine.size();
  Notify(r, INTERNET_STATUS_RESPONSE_RECEIVED, &n, sizeof n);
  r->sent = true;
  return TRUE;
}

// Raw bytes from the socket into pending; false at the end.
bool Fill(Request* r) {
  char tmp[4096];
  for (;;) {
    ssize_t n = recv(r->sock, tmp, sizeof tmp, 0);
    if (n < 0 && errno == EINTR) continue;
    if (n <= 0) return false;
    r->pending.append(tmp, (size_t)n);
    return true;
  }
}

// Up to max bytes of the body.
long long ReadBody(Request* r, char* out, size_t max) {
  if (r->bodyDone || r->sock < 0) return 0;
  size_t got = 0;
  while (got < max && !r->bodyDone) {
    if (r->chunked) {
      if (r->chunkLeft == 0) {
        std::string line;
        if (!ReadLine(r->sock, r->pending, line)) {
          r->bodyDone = true;
          break;
        }
        if (Trim(line).empty()) continue;  // the CR LF after a chunk
        r->chunkLeft = strtoll(line.c_str(), NULL, 16);
        if (r->chunkLeft == 0) {
          // Trailers, to the empty line.
          while (ReadLine(r->sock, r->pending, line) && !line.empty()) {
          }
          r->bodyDone = true;
          break;
        }
      }
      if (r->pending.empty() && !Fill(r)) {
        r->bodyDone = true;
        break;
      }
      size_t n = std::min({max - got, r->pending.size(), (size_t)r->chunkLeft});
      memcpy(out + got, r->pending.data(), n);
      r->pending.erase(0, n);
      r->chunkLeft -= (long long)n;
      got += n;
    } else {
      if (r->contentLength >= 0 && r->position + got >= (unsigned long long)r->contentLength) {
        r->bodyDone = true;
        break;
      }
      if (r->pending.empty() && !Fill(r)) {
        r->bodyDone = true;
        break;
      }
      size_t n = std::min(max - got, r->pending.size());
      if (r->contentLength >= 0) n = std::min(n, (size_t)(r->contentLength - (long long)(r->position + got)));
      memcpy(out + got, r->pending.data(), n);
      r->pending.erase(0, n);
      got += n;
      // A whole buffer is not needed: what arrived will do.
      if (got > 0) break;
    }
  }
  r->position += (DWORD)got;
  if (r->contentLength >= 0 && r->position >= (unsigned long long)r->contentLength) r->bodyDone = true;
  return (long long)got;
}

BOOL SendRequest(Request* r, const std::string& extra, LPVOID body, DWORD bodyLen) {
  for (int redirects = 0;; redirects++) {
    if (!SendHead(r, extra, body, bodyLen, body ? (long long)bodyLen : -1)) return FALSE;
    if (!ReadHead(r)) return FALSE;
    bool redirect = r->status == 301 || r->status == 302 || r->status == 303 || r->status == 307;
    const std::string* loc = FindResponseHeader(r, "Location");
    if (!redirect || !loc || (r->flags & INTERNET_FLAG_NO_AUTO_REDIRECT) || redirects >= 5) return TRUE;
    // Follows a redirection on the same server; another server is left to
    // the caller, who sees the status.
    std::string to = *loc;
    if (to.compare(0, 7, "http://") == 0) {
      Url u;
      if (!ParseUrl(to, &u) || Lower(u.host) != Lower(U(r->conn()->server.c_str())) || u.port != r->conn()->port)
        return TRUE;
      to = u.path + u.extra;
    }
    if (to.empty() || to[0] != '/') return TRUE;
    std::wstring wto = Widen(to.c_str());
    Notify(r, INTERNET_STATUS_REDIRECT, (LPVOID)wto.c_str(), (DWORD)(wto.size() + 1) * sizeof(WCHAR));
    r->object = wto;
    if (r->status == 303 || (r->status != 307 && r->verb == L"POST")) {
      r->verb = L"GET";
      body = NULL;
      bodyLen = 0;
    }
  }
}

// Date: "Tue, 15 Nov 1994 08:12:31 GMT" (and the older forms).
bool ParseHttpDate(const std::string& s, SYSTEMTIME* st) {
  static const char* const months[] = {"jan", "feb", "mar", "apr", "may", "jun",
                                       "jul", "aug", "sep", "oct", "nov", "dec"};
  int day = 0, year = 0, h = 0, m = 0, sec = 0, mon = -1;
  char mbuf[8] = {0};
  const char* p = strchr(s.c_str(), ',');
  p = p ? p + 1 : s.c_str();
  if (sscanf(p, " %d %3s %d %d:%d:%d", &day, mbuf, &year, &h, &m, &sec) != 6 &&
      sscanf(p, " %d-%3s-%d %d:%d:%d", &day, mbuf, &year, &h, &m, &sec) != 6) {
    // asctime: "Sun Nov  6 08:49:37 1994"
    char wd[8];
    if (sscanf(s.c_str(), "%3s %3s %d %d:%d:%d %d", wd, mbuf, &day, &h, &m, &sec, &year) != 7) return false;
  }
  for (int i = 0; i < 12; i++)
    if (strcasecmp(mbuf, months[i]) == 0) mon = i;
  if (mon < 0) return false;
  if (year < 100) year += year < 70 ? 2000 : 1900;
  struct tm t = {};
  t.tm_year = year - 1900;
  t.tm_mon = mon;
  t.tm_mday = day;
  t.tm_hour = h;
  t.tm_min = m;
  t.tm_sec = sec;
  time_t tt = timegm(&t);
  struct tm g;
  gmtime_r(&tt, &g);
  st->wYear = (WORD)(g.tm_year + 1900);
  st->wMonth = (WORD)(g.tm_mon + 1);
  st->wDayOfWeek = (WORD)g.tm_wday;
  st->wDay = (WORD)g.tm_mday;
  st->wHour = (WORD)g.tm_hour;
  st->wMinute = (WORD)g.tm_min;
  st->wSecond = (WORD)g.tm_sec;
  st->wMilliseconds = 0;
  return true;
}

template <class C>
BOOL QueryInfo(HINTERNET hr, DWORD level, LPVOID buf, LPDWORD len, LPDWORD index) {
  Request* r = As<Request>(hr, INTERNET_HANDLE_TYPE_HTTP_REQUEST);
  if (!r) return FALSE;
  if (!len) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  DWORD modifiers = level & 0xF0000000, what = level & 0x0000FFFF;
  DWORD idx = index ? *index : 0;
  std::string value;
  bool found = false;
  if (modifiers & HTTP_QUERY_FLAG_REQUEST_HEADERS) {
    std::string name;
    if (what == HTTP_QUERY_CUSTOM)
      name = sizeof(C) == 1 ? std::string((const char*)buf) : U((const wchar_t*)buf);
    else if (const char* n = HeaderName(what))
      name = n;
    if (what == HTTP_QUERY_RAW_HEADERS_CRLF) {
      for (auto& h : r->headers) value += h.first + ": " + h.second + "\r\n";
      found = true;
    } else {
      DWORD n = 0;
      for (auto& h : r->headers)
        if (Lower(h.first) == Lower(name) && n++ == idx) {
          value = h.second;
          found = true;
          break;
        }
    }
  } else {
    if (!r->sent) {
      SetLastError(ERROR_INTERNET_INCORRECT_HANDLE_STATE);
      return FALSE;
    }
    switch (what) {
      case HTTP_QUERY_STATUS_CODE: value = std::to_string(r->status); found = idx == 0; break;
      case HTTP_QUERY_STATUS_TEXT: value = r->statusText; found = idx == 0; break;
      case HTTP_QUERY_VERSION: value = r->httpVersion; found = idx == 0; break;
      case HTTP_QUERY_RAW_HEADERS_CRLF:
        value = r->statusLine + "\r\n";
        for (auto& h : r->response) value += h.first + ": " + h.second + "\r\n";
        value += "\r\n";
        found = idx == 0;
        break;
      case HTTP_QUERY_RAW_HEADERS: {
        // Each line ends with a zero, and the whole with another.
        value = r->statusLine;
        value += '\0';
        for (auto& h : r->response) {
          value += h.first + ": " + h.second;
          value += '\0';
        }
        found = idx == 0;
        break;
      }
      default: {
        std::string name;
        if (what == HTTP_QUERY_CUSTOM)
          name = sizeof(C) == 1 ? std::string((const char*)buf) : U((const wchar_t*)buf);
        else if (const char* n = HeaderName(what))
          name = n;
        else {
          SetLastError(ERROR_HTTP_INVALID_QUERY_REQUEST);
          return FALSE;
        }
        if (const std::string* v = FindResponseHeader(r, name, idx)) {
          value = *v;
          found = true;
        }
      }
    }
  }
  if (!found) {
    SetLastError(ERROR_HTTP_HEADER_NOT_FOUND);
    return FALSE;
  }
  if (modifiers & HTTP_QUERY_FLAG_NUMBER) {
    if (!buf || *len < sizeof(DWORD)) {
      *len = sizeof(DWORD);
      SetLastError(ERROR_INSUFFICIENT_BUFFER);
      return FALSE;
    }
    *(DWORD*)buf = (DWORD)strtoul(value.c_str(), NULL, 10);
    *len = sizeof(DWORD);
  } else if (modifiers & HTTP_QUERY_FLAG_SYSTEMTIME) {
    if (!buf || *len < sizeof(SYSTEMTIME)) {
      *len = sizeof(SYSTEMTIME);
      SetLastError(ERROR_INSUFFICIENT_BUFFER);
      return FALSE;
    }
    if (!ParseHttpDate(value, (SYSTEMTIME*)buf)) {
      SetLastError(ERROR_HTTP_INVALID_HEADER);
      return FALSE;
    }
    *len = sizeof(SYSTEMTIME);
  } else if (sizeof(C) == 1) {
    if (!PutString(value, buf, len, true)) return FALSE;
  } else {
    std::wstring w = what == HTTP_QUERY_RAW_HEADERS ? Widen(value.data(), (int)value.size()) : Widen(value.c_str());
    if (!PutString(w, buf, len, true)) return FALSE;
  }
  if (index) *index = idx + 1;
  return TRUE;
}

// ---------------------------------------------------------------------
// FTP

// Reads a reply ("ddd text", or "ddd-" lines up to "ddd text").
int FtpReply(Connection* c, std::string& buffer) {
  std::string line, all;
  if (!ReadLine(c->control, buffer, line)) {
    SetLastError(ERROR_FTP_DROPPED);
    return -1;
  }
  all = line + "\r\n";
  if (line.size() >= 4 && line[3] == '-') {
    std::string code = line.substr(0, 3);
    while (ReadLine(c->control, buffer, line)) {
      all += line + "\r\n";
      if (line.size() >= 4 && line.compare(0, 3, code) == 0 && line[3] == ' ') break;
    }
  }
  t_response = all;
  t_responseCode = (DWORD)atoi(all.c_str());
  return atoi(all.c_str());
}

// The control connection's buffer of what came after a reply.
std::map<Connection*, std::string>& FtpBuffers() {
  static auto* m = new std::map<Connection*, std::string>;
  return *m;
}

int FtpCommand(Connection* c, const std::string& cmd) {
  if (c->control < 0) {
    SetLastError(ERROR_FTP_DROPPED);
    return -1;
  }
  if (!SendAll(c->control, cmd + "\r\n")) {
    SetLastError(ERROR_FTP_DROPPED);
    return -1;
  }
  return FtpReply(c, FtpBuffers()[c]);
}

// A command that should succeed (2xx); otherwise the reply is the error.
BOOL FtpOk(Connection* c, const std::string& cmd) {
  int code = FtpCommand(c, cmd);
  if (code < 0) return FALSE;
  if (code >= 200 && code < 300) return TRUE;
  SetLastError(ERROR_INTERNET_EXTENDED_ERROR);
  return FALSE;
}

BOOL FtpBusy(Connection* c) {
  if (c->busy) {
    SetLastError(ERROR_FTP_TRANSFER_IN_PROGRESS);
    return TRUE;
  }
  return FALSE;
}

// Starts a transfer: the data connection (passive, or a listener for the
// server to connect to), the command, and its preliminary reply. Gives
// the data socket.
int FtpTransfer(Connection* c, DWORD typeFlags, const std::string& cmd) {
  if (!FtpOk(c, (typeFlags & FTP_TRANSFER_TYPE_ASCII) ? "TYPE A" : "TYPE I")) return -1;
  int data = -1, listener = -1;
  if (c->flags & INTERNET_FLAG_PASSIVE) {
    int code = FtpCommand(c, "PASV");
    if (code != 227) {
      if (code >= 0) SetLastError(ERROR_INTERNET_EXTENDED_ERROR);
      return -1;
    }
    size_t lp = t_response.find('(');
    int h1, h2, h3, h4, p1, p2;
    const char* s = lp == std::string::npos ? t_response.c_str() + 4 : t_response.c_str() + lp + 1;
    while (*s && !isdigit((unsigned char)*s)) s++;
    if (sscanf(s, "%d,%d,%d,%d,%d,%d", &h1, &h2, &h3, &h4, &p1, &p2) != 6) {
      SetLastError(ERROR_INTERNET_EXTENDED_ERROR);
      return -1;
    }
    char host[32];
    snprintf(host, sizeof host, "%d.%d.%d.%d", h1, h2, h3, h4);
    Session* ses = SessionOf(c);
    data = ConnectTo(c, host, (INTERNET_PORT)((p1 << 8) | p2), ses ? ses->connectTimeout : 0);
    if (data < 0) return -1;
  } else {
    // Active: listen where the control connection is, and say so.
    sockaddr_in local = {};
    socklen_t n = sizeof local;
    getsockname(c->control, (sockaddr*)&local, &n);
    listener = socket(AF_INET, SOCK_STREAM, 0);
    local.sin_port = 0;
    if (listener < 0 || bind(listener, (sockaddr*)&local, sizeof local) != 0 || listen(listener, 1) != 0) {
      if (listener >= 0) close(listener);
      SetLastError(ERROR_INTERNET_CANNOT_CONNECT);
      return -1;
    }
    n = sizeof local;
    getsockname(listener, (sockaddr*)&local, &n);
    unsigned a = ntohl(local.sin_addr.s_addr), p = ntohs(local.sin_port);
    char port[64];
    snprintf(port, sizeof port, "PORT %u,%u,%u,%u,%u,%u", a >> 24, (a >> 16) & 255, (a >> 8) & 255, a & 255, p >> 8,
             p & 255);
    if (!FtpOk(c, port)) {
      close(listener);
      return -1;
    }
  }
  int code = FtpCommand(c, cmd);
  if (code < 100 || code >= 200) {
    if (data >= 0) close(data);
    if (listener >= 0) close(listener);
    if (code >= 0) SetLastError(ERROR_INTERNET_EXTENDED_ERROR);
    return -1;
  }
  if (listener >= 0) {
    Session* ses = SessionOf(c);
    pollfd p = {listener, POLLIN, 0};
    if (poll(&p, 1, ses && ses->connectTimeout ? (int)ses->connectTimeout : -1) == 1) data = accept(listener, NULL, NULL);
    close(listener);
    if (data < 0) {
      SetLastError(ERROR_INTERNET_CANNOT_CONNECT);
      return -1;
    }
  }
#ifdef SO_NOSIGPIPE
  int one = 1;
  setsockopt(data, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
#endif
  return data;
}

// Ends a transfer: closes the data connection and reads the final reply.
BOOL FtpFinish(Connection* c, int data) {
  if (data >= 0) close(data);
  int code = FtpReply(c, FtpBuffers()[c]);
  c->busy = false;
  if (code >= 200 && code < 300) return TRUE;
  if (code >= 0) SetLastError(ERROR_INTERNET_EXTENDED_ERROR);
  return FALSE;
}

bool Wildcard(const wchar_t* pat, const wchar_t* s) {
  if (!*pat) return !*s;
  if (*pat == L'*') return Wildcard(pat + 1, s) || (*s && Wildcard(pat, s + 1));
  if (*s && (*pat == L'?' || towlower(*pat) == towlower(*s))) return Wildcard(pat + 1, s + 1);
  return false;
}

FILETIME ListingTime(const std::string& mon, int day, const std::string& yearOrTime) {
  static const char* const months[] = {"jan", "feb", "mar", "apr", "may", "jun",
                                       "jul", "aug", "sep", "oct", "nov", "dec"};
  struct tm t = {};
  for (int i = 0; i < 12; i++)
    if (strcasecmp(mon.c_str(), months[i]) == 0) t.tm_mon = i;
  t.tm_mday = day;
  time_t now = time(NULL);
  struct tm g;
  gmtime_r(&now, &g);
  int h = 0, m = 0;
  if (sscanf(yearOrTime.c_str(), "%d:%d", &h, &m) == 2) {
    t.tm_year = g.tm_year;
    t.tm_hour = h;
    t.tm_min = m;
  } else {
    t.tm_year = atoi(yearOrTime.c_str()) - 1900;
  }
  return FileTimeFromUnix((long long)timegm(&t), 0);
}

// One line of a listing, Unix ("-rw-r--r-- 1 o g 25 Jan 02 12:00 name")
// or DOS ("01-02-24  12:00PM  <DIR>  name").
bool ParseListing(const std::string& line, WIN32_FIND_DATAW* fd) {
  memset(fd, 0, sizeof *fd);
  if (line.empty() || line.compare(0, 5, "total") == 0) return false;
  std::vector<std::string> f;
  std::vector<size_t> at;
  size_t i = 0;
  while (i < line.size() && f.size() < 9) {
    while (i < line.size() && line[i] == ' ') i++;
    if (i >= line.size()) break;
    size_t s = i;
    while (i < line.size() && line[i] != ' ') i++;
    f.push_back(line.substr(s, i - s));
    at.push_back(s);
  }
  std::string name;
  if (strchr("-dlbcps", line[0]) && f.size() >= 9 && f[0].size() >= 10) {
    // Unix: the name is everything after the eighth field.
    name = line.substr(at[8]);
    if (line[0] == 'l') {
      size_t arrow = name.find(" -> ");
      if (arrow != std::string::npos) name = name.substr(0, arrow);
    }
    fd->dwFileAttributes = line[0] == 'd' ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
    unsigned long long size = strtoull(f[4].c_str(), NULL, 10);
    fd->nFileSizeLow = (DWORD)size;
    fd->nFileSizeHigh = (DWORD)(size >> 32);
    fd->ftLastWriteTime = ListingTime(f[5], atoi(f[6].c_str()), f[7]);
  } else if (f.size() >= 4 && isdigit((unsigned char)line[0])) {
    int mo, d, y, h, mi;
    char ap[3] = {0};
    if (sscanf(f[0].c_str(), "%d-%d-%d", &mo, &d, &y) != 3) return false;
    sscanf(f[1].c_str(), "%d:%d%2s", &h, &mi, ap);
    if ((ap[0] == 'P' || ap[0] == 'p') && h < 12) h += 12;
    if ((ap[0] == 'A' || ap[0] == 'a') && h == 12) h = 0;
    if (y < 100) y += y < 70 ? 2000 : 1900;
    struct tm t = {};
    t.tm_year = y - 1900;
    t.tm_mon = mo - 1;
    t.tm_mday = d;
    t.tm_hour = h;
    t.tm_min = mi;
    fd->ftLastWriteTime = FileTimeFromUnix((long long)timegm(&t), 0);
    if (f[2] == "<DIR>") {
      fd->dwFileAttributes = FILE_ATTRIBUTE_DIRECTORY;
    } else {
      fd->dwFileAttributes = FILE_ATTRIBUTE_NORMAL;
      unsigned long long size = strtoull(f[2].c_str(), NULL, 10);
      fd->nFileSizeLow = (DWORD)size;
      fd->nFileSizeHigh = (DWORD)(size >> 32);
    }
    name = line.substr(at[3]);
  } else {
    return false;
  }
  if (name.empty() || name == "." || name == "..") return false;
  fd->ftCreationTime = fd->ftLastAccessTime = fd->ftLastWriteTime;
  std::wstring w = Widen(name.c_str());
  wcsncpy(fd->cFileName, w.c_str(), MAX_PATH - 1);
  return true;
}

void ToFindDataA(const WIN32_FIND_DATAW& w, WIN32_FIND_DATAA* a) {
  memset(a, 0, sizeof *a);
  a->dwFileAttributes = w.dwFileAttributes;
  a->ftCreationTime = w.ftCreationTime;
  a->ftLastAccessTime = w.ftLastAccessTime;
  a->ftLastWriteTime = w.ftLastWriteTime;
  a->nFileSizeHigh = w.nFileSizeHigh;
  a->nFileSizeLow = w.nFileSizeLow;
  std::string n = Narrow(w.cFileName);
  strncpy(a->cFileName, n.c_str(), MAX_PATH - 1);
}

HINTERNET FtpFindFirst(HINTERNET hc, LPCWSTR pattern, WIN32_FIND_DATAW* out, DWORD flags, DWORD_PTR ctx) {
  Connection* c = As<Connection>(hc, INTERNET_HANDLE_TYPE_CONNECT_FTP);
  if (!c || FtpBusy(c)) return NULL;
  std::wstring pat = pattern ? pattern : L"";
  bool wild = pat.find_first_of(L"*?") != std::wstring::npos;
  std::string cmd = "LIST";
  // A pattern of its own is matched here; a path goes to the server.
  std::wstring dir, match;
  if (wild) {
    size_t slash = pat.find_last_of(L'/');
    if (slash != std::wstring::npos) {
      dir = pat.substr(0, slash + 1);
      match = pat.substr(slash + 1);
    } else {
      match = pat;
    }
    if (!dir.empty()) cmd += " " + U(dir.c_str());
  } else if (!pat.empty()) {
    cmd += " " + U(pat.c_str());
  }
  int data = FtpTransfer(c, FTP_TRANSFER_TYPE_ASCII, cmd);
  if (data < 0) return NULL;
  c->busy = true;
  std::string text, buffer, line;
  while (ReadLine(data, buffer, line)) text += line + "\n";
  if (!FtpFinish(c, data)) return NULL;
  FtpFind* f = NewChild<FtpFind>(c, INTERNET_HANDLE_TYPE_FTP_FIND, ctx, flags);
  size_t s = 0;
  while (s < text.size()) {
    size_t e = text.find('\n', s);
    std::string l = text.substr(s, e - s);
    s = e + 1;
    WIN32_FIND_DATAW fd;
    if (!ParseListing(l, &fd)) continue;
    if (!match.empty() && match != L"*" && match != L"*.*" && !Wildcard(match.c_str(), fd.cFileName)) continue;
    f->entries.push_back(fd);
  }
  if (f->entries.empty()) {
    InternetCloseHandle((HINTERNET)f);
    SetLastError(ERROR_NO_MORE_FILES);
    return NULL;
  }
  if (out) *out = f->entries[0];
  f->next = 1;
  return (HINTERNET)f;
}

HINTERNET FtpOpen(HINTERNET hc, LPCWSTR name, DWORD access, DWORD flags, DWORD_PTR ctx) {
  Connection* c = As<Connection>(hc, INTERNET_HANDLE_TYPE_CONNECT_FTP);
  if (!c || FtpBusy(c)) return NULL;
  if (!name || (access != GENERIC_READ && access != GENERIC_WRITE)) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return NULL;
  }
  std::string n = U(name);
  long long size = -1;
  if (access == GENERIC_READ) {
    // Its size first, while no transfer is open.
    if (FtpCommand(c, "SIZE " + n) == 213) size = atoll(t_response.c_str() + 4);
  }
  int data = FtpTransfer(c, flags, (access == GENERIC_READ ? "RETR " : "STOR ") + n);
  if (data < 0) return NULL;
  c->busy = true;
  FtpFile* f = NewChild<FtpFile>(c, INTERNET_HANDLE_TYPE_FTP_FILE, ctx, flags);
  f->data = data;
  f->write = access == GENERIC_WRITE;
  f->size = size;
  return (HINTERNET)f;
}

BOOL CloseFtpFile(FtpFile* f) {
  if (f->closed) return TRUE;
  f->closed = true;
  return FtpFinish(f->conn(), f->data);
}

BOOL FtpGet(HINTERNET hc, LPCWSTR remote, LPCWSTR local, BOOL failIfExists, DWORD flags, DWORD_PTR ctx) {
  if (!local) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  HANDLE out = CreateFileW(local, GENERIC_WRITE, 0, NULL, failIfExists ? CREATE_NEW : CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, NULL);
  if (out == INVALID_HANDLE_VALUE) return FALSE;
  HINTERNET f = FtpOpen(hc, remote, GENERIC_READ, flags, ctx);
  if (!f) {
    DWORD e = GetLastError();
    CloseHandle(out);
    DeleteFileW(local);
    SetLastError(e);
    return FALSE;
  }
  char buf[8192];
  DWORD got = 0, wrote;
  BOOL ok = TRUE;
  while (ok && (ok = InternetReadFile(f, buf, sizeof buf, &got)) && got > 0)
    ok = WriteFile(out, buf, got, &wrote, NULL) && wrote == got;
  DWORD e = GetLastError();
  BOOL closed = InternetCloseHandle(f);
  CloseHandle(out);
  if (!ok || !closed) {
    DeleteFileW(local);
    SetLastError(ok ? GetLastError() : e);
    return FALSE;
  }
  return TRUE;
}

BOOL FtpPut(HINTERNET hc, LPCWSTR local, LPCWSTR remote, DWORD flags, DWORD_PTR ctx) {
  HANDLE in = CreateFileW(local, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
  if (in == INVALID_HANDLE_VALUE) return FALSE;
  HINTERNET f = FtpOpen(hc, remote, GENERIC_WRITE, flags, ctx);
  if (!f) {
    DWORD e = GetLastError();
    CloseHandle(in);
    SetLastError(e);
    return FALSE;
  }
  char buf[8192];
  DWORD got = 0, wrote;
  BOOL ok = TRUE;
  while (ok && ReadFile(in, buf, sizeof buf, &got, NULL) && got > 0) ok = InternetWriteFile(f, buf, got, &wrote);
  CloseHandle(in);
  BOOL closed = InternetCloseHandle(f);
  return ok && closed;
}

BOOL FtpGetDir(HINTERNET hc, std::string* dir) {
  Connection* c = As<Connection>(hc, INTERNET_HANDLE_TYPE_CONNECT_FTP);
  if (!c || FtpBusy(c)) return FALSE;
  if (FtpCommand(c, "PWD") != 257) {
    SetLastError(ERROR_INTERNET_EXTENDED_ERROR);
    return FALSE;
  }
  size_t a = t_response.find('"'), b = a == std::string::npos ? a : t_response.find('"', a + 1);
  *dir = a == std::string::npos || b == std::string::npos ? Trim(t_response.substr(4)) : t_response.substr(a + 1, b - a - 1);
  return TRUE;
}

BOOL FtpSimple(HINTERNET hc, const std::string& cmd) {
  Connection* c = As<Connection>(hc, INTERNET_HANDLE_TYPE_CONNECT_FTP);
  if (!c || FtpBusy(c)) return FALSE;
  return FtpOk(c, cmd);
}

// ---------------------------------------------------------------------
// Opening

HINTERNET Open(const std::wstring& agent, DWORD access, DWORD flags) {
  Session* s = new Session;
  s->type = INTERNET_HANDLE_TYPE_INTERNET;
  s->agent = agent;
  s->flags = flags;
  (void)access;
  Register(s);
  return (HINTERNET)s;
}

HINTERNET Connect(HINTERNET hs, const std::wstring& server, INTERNET_PORT port, const std::wstring& user,
                  const std::wstring& pass, DWORD service, DWORD flags, DWORD_PTR ctx) {
  Session* s = As<Session>(hs, INTERNET_HANDLE_TYPE_INTERNET);
  if (!s) return NULL;
  DWORD type;
  switch (service) {
    case INTERNET_SERVICE_FTP: type = INTERNET_HANDLE_TYPE_CONNECT_FTP; break;
    case INTERNET_SERVICE_HTTP: type = INTERNET_HANDLE_TYPE_CONNECT_HTTP; break;
    default: SetLastError(ERROR_INTERNET_PROTOCOL_NOT_FOUND); return NULL;
  }
  if (server.empty()) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return NULL;
  }
  Connection* c = NewChild<Connection>(s, type, ctx, flags);
  c->service = service;
  c->server = server;
  c->user = user;
  c->password = pass;
  c->port = port ? port
                 : service == INTERNET_SERVICE_FTP ? INTERNET_DEFAULT_FTP_PORT
                 : (flags & INTERNET_FLAG_SECURE) ? INTERNET_DEFAULT_HTTPS_PORT
                                                  : INTERNET_DEFAULT_HTTP_PORT;
  if (service != INTERNET_SERVICE_FTP) return (HINTERNET)c;
  // FTP logs in now.
  c->control = ConnectTo(c, U(server.c_str()), c->port, s->connectTimeout);
  int code = c->control < 0 ? -1 : FtpReply(c, FtpBuffers()[c]);
  if (code >= 0 && code / 100 == 2) {
    std::string u = user.empty() ? "anonymous" : U(user.c_str());
    std::string p = user.empty() && pass.empty() ? "user@" : U(pass.c_str());
    code = FtpCommand(c, "USER " + u);
    if (code == 331) code = FtpCommand(c, "PASS " + p);
    if (code < 0 || code / 100 != 2) {
      if (code >= 0) SetLastError(ERROR_INTERNET_LOGIN_FAILURE);
      code = -1;
    }
  } else if (code >= 0) {
    SetLastError(ERROR_INTERNET_EXTENDED_ERROR);
    code = -1;
  }
  if (code < 0) {
    DWORD e = GetLastError();
    InternetCloseHandle((HINTERNET)c);
    SetLastError(e);
    return NULL;
  }
  return (HINTERNET)c;
}

HINTERNET OpenRequest(HINTERNET hc, const std::wstring& verb, const std::wstring& object,
                      const std::wstring& version, const std::wstring& referrer, LPCWSTR* accept, DWORD flags,
                      DWORD_PTR ctx) {
  Connection* c = As<Connection>(hc, INTERNET_HANDLE_TYPE_CONNECT_HTTP);
  if (!c) return NULL;
  Request* r = NewChild<Request>(c, INTERNET_HANDLE_TYPE_HTTP_REQUEST, ctx, flags | (c->flags & INTERNET_FLAG_SECURE));
  r->verb = verb.empty() ? L"GET" : verb;
  r->object = object.empty() ? L"/" : object;
  r->version = version.empty() ? L"HTTP/1.1" : version;
  if (!referrer.empty()) r->headers.push_back({"Referer", U(referrer.c_str())});
  if (accept && accept[0]) {
    std::string a;
    for (int i = 0; accept[i]; i++) a += (i ? ", " : "") + U(accept[i]);
    r->headers.push_back({"Accept", a});
  }
  return (HINTERNET)r;
}

HINTERNET OpenUrl(HINTERNET hs, const std::string& url, const std::string& headers, DWORD flags, DWORD_PTR ctx) {
  Session* s = As<Session>(hs, INTERNET_HANDLE_TYPE_INTERNET);
  if (!s) return NULL;
  Url u;
  if (!ParseUrl(url, &u)) return NULL;
  u.user = Decode(u.user);
  u.password = Decode(u.password);
  switch (u.nScheme) {
    case INTERNET_SCHEME_HTTP:
    case INTERNET_SCHEME_HTTPS: {
      HINTERNET c = Connect(hs, Widen(u.host.c_str()), u.port, Widen(u.user.c_str()), Widen(u.password.c_str()),
                            INTERNET_SERVICE_HTTP, (u.nScheme == INTERNET_SCHEME_HTTPS ? INTERNET_FLAG_SECURE : 0),
                            ctx);
      if (!c) return NULL;
      std::string obj = u.path.empty() ? "/" : u.path;
      HINTERNET r = OpenRequest(c, L"GET", Widen((obj + u.extra).c_str()), L"", L"", NULL, flags, ctx);
      Request* rq = (Request*)r;
      if (!r || !SendRequest(rq, headers, NULL, 0)) {
        DWORD e = GetLastError();
        if (r) InternetCloseHandle(r);
        InternetCloseHandle(c);
        SetLastError(e);
        return NULL;
      }
      rq->ownsParent = true;
      return r;
    }
    case INTERNET_SCHEME_FTP: {
      HINTERNET c = Connect(hs, Widen(u.host.c_str()), u.port, Widen(u.user.c_str()), Widen(u.password.c_str()),
                            INTERNET_SERVICE_FTP, flags & INTERNET_FLAG_PASSIVE, ctx);
      if (!c) return NULL;
      std::string path = Decode(u.path);
      HINTERNET f = NULL;
      if (path.empty() || path.back() == '/') {
        WIN32_FIND_DATAW fd;
        f = FtpFindFirst(c, path.empty() ? NULL : Widen(path.c_str()).c_str(), &fd, flags, ctx);
      } else {
        f = FtpOpen(c, Widen(path.c_str()).c_str(), GENERIC_READ, flags | FTP_TRANSFER_TYPE_BINARY, ctx);
      }
      if (!f) {
        DWORD e = GetLastError();
        InternetCloseHandle(c);
        SetLastError(e);
        return NULL;
      }
      ((Handle*)f)->ownsParent = true;
      return f;
    }
    case INTERNET_SCHEME_UNKNOWN: SetLastError(ERROR_INTERNET_UNRECOGNIZED_SCHEME); return NULL;
    default: SetLastError(ERROR_INTERNET_UNRECOGNIZED_SCHEME); return NULL;
  }
}

// ---------------------------------------------------------------------
// InternetCrackUrl, for both character sizes: each part either points
// into the URL (no buffer given, a length asked for) or is copied.

template <class C>
BOOL CrackPart(const C* url, size_t at, const std::basic_string<C>& value, bool present, C** ptr, DWORD* len) {
  if (!ptr || !len) return TRUE;
  if (*ptr == NULL) {
    if (*len) {
      *ptr = present ? (C*)url + at : NULL;
      *len = present ? (DWORD)value.size() : 0;
    }
    return TRUE;
  }
  if (*len <= value.size()) {
    *len = (DWORD)value.size() + 1;
    SetLastError(ERROR_INSUFFICIENT_BUFFER);
    return FALSE;
  }
  memcpy(*ptr, value.c_str(), (value.size() + 1) * sizeof(C));
  *len = (DWORD)value.size();
  return TRUE;
}

std::basic_string<char> Conv(const std::string& s, char*) { return s; }
std::basic_string<wchar_t> Conv(const std::string& s, wchar_t*) { return Widen(s.c_str()); }

template <class C, class UC>
BOOL Crack(const C* url, DWORD urlLen, DWORD flags, UC* uc) {
  if (!url || !uc) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  std::basic_string<C> in = urlLen ? std::basic_string<C>(url, urlLen) : std::basic_string<C>(url);
  std::string s;
  // Offsets in characters of the original: this works when each character
  // is one unit, which holds for the URL's ASCII structure.
  for (C ch : in) s += ch < 0x80 ? (char)ch : '?';
  std::string utf = sizeof(C) == 1 ? std::string((const char*)in.c_str()) : U((const wchar_t*)in.c_str());
  Url u;
  if (!ParseUrl(s, &u)) return FALSE;
  // The values from the real text (which may hold non-ASCII).
  auto real = [&](size_t at, size_t n) { return in.substr(at, n); };
  bool hasAuth = s.compare(u.scheme.size() + 1, 2, "//") == 0;
  std::basic_string<C> scheme = real(0, u.scheme.size());
  std::basic_string<C> user = real(u.userAt, u.user.size()), pass = real(u.passwordAt, u.password.size());
  std::basic_string<C> host = real(u.hostAt, u.host.size()), path = real(u.pathAt, u.path.size());
  std::basic_string<C> extra = u.extra.empty() ? std::basic_string<C>() : real(u.extraAt, u.extra.size());
  if (flags & ICU_DECODE) {
    C* z = NULL;
    std::string ut = sizeof(C) == 1 ? std::string((const char*)path.c_str()) : U((const wchar_t*)path.c_str());
    path = Conv(Decode(ut), z);
    ut = sizeof(C) == 1 ? std::string((const char*)user.c_str()) : U((const wchar_t*)user.c_str());
    user = Conv(Decode(ut), z);
    ut = sizeof(C) == 1 ? std::string((const char*)pass.c_str()) : U((const wchar_t*)pass.c_str());
    pass = Conv(Decode(ut), z);
  }
  uc->nScheme = u.nScheme;
  uc->nPort = u.port;
  return CrackPart(in.c_str(), 0, scheme, true, &uc->lpszScheme, &uc->dwSchemeLength) &&
         CrackPart(in.c_str(), u.hostAt, host, hasAuth, &uc->lpszHostName, &uc->dwHostNameLength) &&
         CrackPart(in.c_str(), u.userAt, user, !u.user.empty(), &uc->lpszUserName, &uc->dwUserNameLength) &&
         CrackPart(in.c_str(), u.passwordAt, pass, !u.password.empty(), &uc->lpszPassword, &uc->dwPasswordLength) &&
         CrackPart(in.c_str(), u.pathAt, path, true, &uc->lpszUrlPath, &uc->dwUrlPathLength) &&
         CrackPart(in.c_str(), u.extraAt, extra, !u.extra.empty(), &uc->lpszExtraInfo, &uc->dwExtraInfoLength);
}

// Options

template <class C>
BOOL QueryOption(HINTERNET hi, DWORD option, LPVOID buf, LPDWORD len) {
  if (!len) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  Handle* h = NULL;
  if (hi) {
    h = InetHandle(hi);
    if (!h) {
      SetLastError(ERROR_INVALID_HANDLE);
      return FALSE;
    }
  }
  auto dword = [&](DWORD v) -> BOOL {
    if (!buf || *len < sizeof(DWORD)) {
      *len = sizeof(DWORD);
      SetLastError(ERROR_INSUFFICIENT_BUFFER);
      return FALSE;
    }
    *(DWORD*)buf = v;
    *len = sizeof(DWORD);
    return TRUE;
  };
  auto ptr = [&](void* v) -> BOOL {
    if (!buf || *len < sizeof(void*)) {
      *len = sizeof(void*);
      SetLastError(ERROR_INSUFFICIENT_BUFFER);
      return FALSE;
    }
    memcpy(buf, &v, sizeof v);
    *len = sizeof(void*);
    return TRUE;
  };
  auto str = [&](const std::string& v) -> BOOL {
    C* z = NULL;
    return PutString(Conv(v, z), buf, len, true);
  };
  Session* s = SessionOf(h);
  switch (option) {
    case INTERNET_OPTION_CONNECT_TIMEOUT: return dword(s ? s->connectTimeout : 60000);
    case INTERNET_OPTION_SEND_TIMEOUT:
    case INTERNET_OPTION_DATA_SEND_TIMEOUT: return dword(s ? s->sendTimeout : 30000);
    case INTERNET_OPTION_RECEIVE_TIMEOUT:
    case INTERNET_OPTION_DATA_RECEIVE_TIMEOUT: return dword(s ? s->receiveTimeout : 30000);
    case INTERNET_OPTION_CONNECT_RETRIES: return dword(5);
    case INTERNET_OPTION_READ_BUFFER_SIZE:
    case INTERNET_OPTION_WRITE_BUFFER_SIZE: return dword(4096);
    case INTERNET_OPTION_KEEP_CONNECTION: return dword(0);
  }
  if (!h) {
    SetLastError(ERROR_INTERNET_INCORRECT_HANDLE_TYPE);
    return FALSE;
  }
  switch (option) {
    case INTERNET_OPTION_HANDLE_TYPE: return dword(h->type);
    case INTERNET_OPTION_CONTEXT_VALUE: return ptr((void*)h->context);
    case INTERNET_OPTION_PARENT_HANDLE: return ptr(h->parent);
    case INTERNET_OPTION_CALLBACK: return ptr((void*)h->callback);
    case INTERNET_OPTION_REQUEST_FLAGS: return dword(0);
    case INTERNET_OPTION_USER_AGENT: return str(s ? U(s->agent.c_str()) : std::string());
    case INTERNET_OPTION_USERNAME:
    case INTERNET_OPTION_PASSWORD: {
      Handle* p = h;
      while (p && p->type != INTERNET_HANDLE_TYPE_CONNECT_FTP && p->type != INTERNET_HANDLE_TYPE_CONNECT_HTTP)
        p = p->parent;
      if (!p) break;
      Connection* c = (Connection*)p;
      return str(U((option == INTERNET_OPTION_USERNAME ? c->user : c->password).c_str()));
    }
    case INTERNET_OPTION_URL: {
      if (h->type == INTERNET_HANDLE_TYPE_HTTP_REQUEST) return str(RequestUrl((Request*)h));
      break;
    }
  }
  SetLastError(ERROR_INTERNET_INVALID_OPTION);
  return FALSE;
}

template <class C>
BOOL SetOption(HINTERNET hi, DWORD option, LPVOID buf, DWORD len) {
  Handle* h = hi ? InetHandle(hi) : NULL;
  if (hi && !h) {
    SetLastError(ERROR_INVALID_HANDLE);
    return FALSE;
  }
  Session* s = SessionOf(h);
  auto dword = [&](DWORD* out) -> BOOL {
    if (!buf || len < sizeof(DWORD)) {
      SetLastError(ERROR_INTERNET_BAD_OPTION_LENGTH);
      return FALSE;
    }
    if (out) *out = *(DWORD*)buf;
    return TRUE;
  };
  switch (option) {
    case INTERNET_OPTION_CONNECT_TIMEOUT: return dword(s ? &s->connectTimeout : NULL);
    case INTERNET_OPTION_SEND_TIMEOUT:
    case INTERNET_OPTION_DATA_SEND_TIMEOUT: return dword(s ? &s->sendTimeout : NULL);
    case INTERNET_OPTION_RECEIVE_TIMEOUT:
    case INTERNET_OPTION_DATA_RECEIVE_TIMEOUT: return dword(s ? &s->receiveTimeout : NULL);
    case INTERNET_OPTION_CONNECT_RETRIES:
    case INTERNET_OPTION_CONNECT_BACKOFF:
    case INTERNET_OPTION_READ_BUFFER_SIZE:
    case INTERNET_OPTION_WRITE_BUFFER_SIZE:
    case INTERNET_OPTION_KEEP_CONNECTION: return dword(NULL);
    case INTERNET_OPTION_CONTEXT_VALUE:
      if (!h || !buf || len < sizeof(DWORD_PTR)) break;
      memcpy(&h->context, buf, sizeof(DWORD_PTR));
      return TRUE;
    case INTERNET_OPTION_USER_AGENT:
      if (!s || !buf) break;
      s->agent = sizeof(C) == 1 ? W((LPCSTR)buf) : std::wstring((LPCWSTR)buf);
      return TRUE;
    case INTERNET_OPTION_USERNAME:
    case INTERNET_OPTION_PASSWORD: {
      if (!h || !buf || (h->type != INTERNET_HANDLE_TYPE_CONNECT_FTP && h->type != INTERNET_HANDLE_TYPE_CONNECT_HTTP))
        break;
      Connection* c = (Connection*)h;
      std::wstring v = sizeof(C) == 1 ? W((LPCSTR)buf) : std::wstring((LPCWSTR)buf);
      (option == INTERNET_OPTION_USERNAME ? c->user : c->password) = v;
      return TRUE;
    }
  }
  SetLastError(ERROR_INTERNET_INVALID_OPTION);
  return FALSE;
}

}  // namespace

// ---------------------------------------------------------------------
// The functions

extern "C" {

HINTERNET WINAPI InternetOpenW(LPCWSTR agent, DWORD access, LPCWSTR, LPCWSTR, DWORD flags) {
  return Open(agent ? agent : L"", access, flags);
}
HINTERNET WINAPI InternetOpenA(LPCSTR agent, DWORD access, LPCSTR, LPCSTR, DWORD flags) {
  return Open(W(agent), access, flags);
}

HINTERNET WINAPI InternetConnectW(HINTERNET hs, LPCWSTR server, INTERNET_PORT port, LPCWSTR user, LPCWSTR pass,
                                  DWORD service, DWORD flags, DWORD_PTR ctx) {
  return Connect(hs, server ? server : L"", port, user ? user : L"", pass ? pass : L"", service, flags, ctx);
}
HINTERNET WINAPI InternetConnectA(HINTERNET hs, LPCSTR server, INTERNET_PORT port, LPCSTR user, LPCSTR pass,
                                  DWORD service, DWORD flags, DWORD_PTR ctx) {
  return Connect(hs, W(server), port, W(user), W(pass), service, flags, ctx);
}

BOOL WINAPI InternetCloseHandle(HINTERNET hi) {
  Handle* h = InetHandle(hi);
  if (!h) {
    SetLastError(ERROR_INVALID_HANDLE);
    return FALSE;
  }
  // Its children go first.
  std::vector<Handle*> children;
  {
    std::lock_guard<std::mutex> g(g_lock);
    for (Handle* c : LiveHandles())
      if (c->parent == h) children.push_back(c);
  }
  for (Handle* c : children) InternetCloseHandle((HINTERNET)c);
  Notify(h, INTERNET_STATUS_HANDLE_CLOSING, &hi, sizeof hi);
  BOOL ok = TRUE;
  Handle* owner = h->ownsParent ? h->parent : NULL;
  switch (h->type) {
    case INTERNET_HANDLE_TYPE_HTTP_REQUEST: {
      Request* r = (Request*)h;
      if (r->sock >= 0) close(r->sock);
      r->sock = -1;
      break;
    }
    case INTERNET_HANDLE_TYPE_FTP_FILE: ok = CloseFtpFile((FtpFile*)h); break;
    case INTERNET_HANDLE_TYPE_CONNECT_FTP: {
      Connection* c = (Connection*)h;
      if (c->control >= 0) {
        if (!c->busy) FtpCommand(c, "QUIT");
        close(c->control);
      }
      FtpBuffers().erase(c);
      break;
    }
  }
  {
    std::lock_guard<std::mutex> g(g_lock);
    auto& v = LiveHandles();
    for (size_t i = 0; i < v.size(); i++)
      if (v[i] == h) {
        v.erase(v.begin() + i);
        break;
      }
  }
  delete h;
  if (owner) InternetCloseHandle((HINTERNET)owner);
  return ok;
}

INTERNET_STATUS_CALLBACK WINAPI InternetSetStatusCallbackW(HINTERNET hi, INTERNET_STATUS_CALLBACK cb) {
  Handle* h = InetHandle(hi);
  if (!h) return INTERNET_INVALID_STATUS_CALLBACK;
  INTERNET_STATUS_CALLBACK old = h->callback;
  h->callback = cb;
  return old;
}
INTERNET_STATUS_CALLBACK WINAPI InternetSetStatusCallbackA(HINTERNET hi, INTERNET_STATUS_CALLBACK cb) {
  return InternetSetStatusCallbackW(hi, cb);
}

HINTERNET WINAPI InternetOpenUrlW(HINTERNET hs, LPCWSTR url, LPCWSTR headers, DWORD len, DWORD flags, DWORD_PTR ctx) {
  if (!url) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return NULL;
  }
  return OpenUrl(hs, U(url), headers ? U(headers, len == (DWORD)-1 || !len ? -1 : (int)len) : std::string(), flags,
                 ctx);
}
HINTERNET WINAPI InternetOpenUrlA(HINTERNET hs, LPCSTR url, LPCSTR headers, DWORD len, DWORD flags, DWORD_PTR ctx) {
  if (!url) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return NULL;
  }
  std::string h = headers ? (len == (DWORD)-1 || !len ? std::string(headers) : std::string(headers, len)) : "";
  return OpenUrl(hs, url, h, flags, ctx);
}

BOOL WINAPI InternetCrackUrlW(LPCWSTR url, DWORD len, DWORD flags, LPURL_COMPONENTSW uc) {
  return Crack(url, len, flags, uc);
}
BOOL WINAPI InternetCrackUrlA(LPCSTR url, DWORD len, DWORD flags, LPURL_COMPONENTSA uc) {
  return Crack(url, len, flags, uc);
}

BOOL WINAPI InternetQueryOptionW(HINTERNET h, DWORD option, LPVOID buf, LPDWORD len) {
  return QueryOption<wchar_t>(h, option, buf, len);
}
BOOL WINAPI InternetQueryOptionA(HINTERNET h, DWORD option, LPVOID buf, LPDWORD len) {
  return QueryOption<char>(h, option, buf, len);
}
BOOL WINAPI InternetSetOptionW(HINTERNET h, DWORD option, LPVOID buf, DWORD len) {
  return SetOption<wchar_t>(h, option, buf, len);
}
BOOL WINAPI InternetSetOptionA(HINTERNET h, DWORD option, LPVOID buf, DWORD len) {
  return SetOption<char>(h, option, buf, len);
}

BOOL WINAPI InternetGetLastResponseInfoW(LPDWORD error, LPWSTR buf, LPDWORD len) {
  if (error) *error = t_responseCode;
  if (!len) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  std::wstring w = Widen(t_response.c_str());
  if (!buf || *len <= w.size()) {
    *len = (DWORD)w.size();
    SetLastError(ERROR_INSUFFICIENT_BUFFER);
    return FALSE;
  }
  wcscpy(buf, w.c_str());
  *len = (DWORD)w.size();
  return TRUE;
}
BOOL WINAPI InternetGetLastResponseInfoA(LPDWORD error, LPSTR buf, LPDWORD len) {
  if (error) *error = t_responseCode;
  if (!len) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  if (!buf || *len <= t_response.size()) {
    *len = (DWORD)t_response.size();
    SetLastError(ERROR_INSUFFICIENT_BUFFER);
    return FALSE;
  }
  strcpy(buf, t_response.c_str());
  *len = (DWORD)t_response.size();
  return TRUE;
}

BOOL WINAPI InternetReadFile(HINTERNET hi, LPVOID buf, DWORD want, LPDWORD got) {
  Handle* h = InetHandle(hi);
  if (!h || !got) {
    SetLastError(h ? ERROR_INVALID_PARAMETER : ERROR_INVALID_HANDLE);
    return FALSE;
  }
  *got = 0;
  if (h->type == INTERNET_HANDLE_TYPE_HTTP_REQUEST) {
    Request* r = (Request*)h;
    if (!r->sent) {
      SetLastError(ERROR_INTERNET_INCORRECT_HANDLE_STATE);
      return FALSE;
    }
    // Fills the buffer, as WinINet does for a blocking read.
    DWORD total = 0;
    while (total < want) {
      long long n = ReadBody(r, (char*)buf + total, want - total);
      if (n <= 0) break;
      total += (DWORD)n;
    }
    *got = total;
    if (total == 0) Notify(r, INTERNET_STATUS_REQUEST_COMPLETE);
    return TRUE;
  }
  if (h->type == INTERNET_HANDLE_TYPE_FTP_FILE) {
    FtpFile* f = (FtpFile*)h;
    if (f->write || f->closed) {
      SetLastError(ERROR_INTERNET_INCORRECT_HANDLE_STATE);
      return FALSE;
    }
    DWORD total = 0;
    while (total < want) {
      ssize_t n = recv(f->data, (char*)buf + total, want - total, 0);
      if (n < 0 && errno == EINTR) continue;
      if (n <= 0) break;
      total += (DWORD)n;
    }
    f->position += total;
    *got = total;
    return TRUE;
  }
  SetLastError(ERROR_INTERNET_INCORRECT_HANDLE_TYPE);
  return FALSE;
}

BOOL WINAPI InternetWriteFile(HINTERNET hi, LPCVOID buf, DWORD n, LPDWORD wrote) {
  Handle* h = InetHandle(hi);
  if (!h || !wrote) {
    SetLastError(h ? ERROR_INVALID_PARAMETER : ERROR_INVALID_HANDLE);
    return FALSE;
  }
  *wrote = 0;
  if (h->type == INTERNET_HANDLE_TYPE_HTTP_REQUEST) {
    Request* r = (Request*)h;
    if (!r->writing || r->sock < 0) {
      SetLastError(ERROR_INTERNET_INCORRECT_HANDLE_STATE);
      return FALSE;
    }
    if (!SendAll(r->sock, buf, n)) return FALSE;
    *wrote = n;
    return TRUE;
  }
  if (h->type == INTERNET_HANDLE_TYPE_FTP_FILE) {
    FtpFile* f = (FtpFile*)h;
    if (!f->write || f->closed) {
      SetLastError(ERROR_INTERNET_INCORRECT_HANDLE_STATE);
      return FALSE;
    }
    if (!SendAll(f->data, buf, n)) return FALSE;
    f->position += n;
    *wrote = n;
    return TRUE;
  }
  SetLastError(ERROR_INTERNET_INCORRECT_HANDLE_TYPE);
  return FALSE;
}

DWORD WINAPI InternetSetFilePointer(HINTERNET hi, LONG dist, PLONG high, DWORD method, DWORD_PTR) {
  Handle* h = InetHandle(hi);
  if (!h) {
    SetLastError(ERROR_INVALID_HANDLE);
    return (DWORD)-1;
  }
  if (high && *high) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return (DWORD)-1;
  }
  if (h->type != INTERNET_HANDLE_TYPE_HTTP_REQUEST) {
    SetLastError(ERROR_INTERNET_INVALID_OPERATION);
    return (DWORD)-1;
  }
  Request* r = (Request*)h;
  long long target;
  switch (method) {
    case FILE_BEGIN: target = dist; break;
    case FILE_CURRENT: target = (long long)r->position + dist; break;
    default:
      if (r->contentLength < 0) {
        SetLastError(ERROR_INTERNET_INVALID_OPERATION);
        return (DWORD)-1;
      }
      target = r->contentLength + dist;
  }
  if (target < (long long)r->position) {
    // Back: only by asking again.
    SetLastError(ERROR_INTERNET_INVALID_OPERATION);
    return (DWORD)-1;
  }
  char tmp[4096];
  while ((long long)r->position < target) {
    long long n = ReadBody(r, tmp, (size_t)std::min<long long>((long long)sizeof tmp, target - r->position));
    if (n <= 0) break;
  }
  return r->position;
}

BOOL WINAPI InternetQueryDataAvailable(HINTERNET hi, LPDWORD avail, DWORD, DWORD_PTR) {
  Handle* h = InetHandle(hi);
  if (!h || !avail) {
    SetLastError(h ? ERROR_INVALID_PARAMETER : ERROR_INVALID_HANDLE);
    return FALSE;
  }
  *avail = 0;
  if (h->type == INTERNET_HANDLE_TYPE_HTTP_REQUEST) {
    Request* r = (Request*)h;
    if (r->bodyDone) return TRUE;
    if (r->pending.empty()) Fill(r);  // waits for some, as WinINet does
    size_t n = r->pending.size();
    if (!r->chunked && r->contentLength >= 0) n = std::min(n, (size_t)(r->contentLength - r->position));
    *avail = (DWORD)n;
    return TRUE;
  }
  if (h->type == INTERNET_HANDLE_TYPE_FTP_FILE) {
    FtpFile* f = (FtpFile*)h;
    int n = 0;
    pollfd p = {f->data, POLLIN, 0};
    if (poll(&p, 1, -1) == 1 && ioctl(f->data, FIONREAD, &n) == 0) *avail = (DWORD)n;
    return TRUE;
  }
  if (h->type == INTERNET_HANDLE_TYPE_FTP_FIND) {
    FtpFind* f = (FtpFind*)h;
    *avail = (DWORD)((f->entries.size() - f->next) * sizeof(WIN32_FIND_DATAW));
    return TRUE;
  }
  SetLastError(ERROR_INTERNET_INCORRECT_HANDLE_TYPE);
  return FALSE;
}

BOOL WINAPI InternetFindNextFileW(HINTERNET hi, LPVOID out) {
  FtpFind* f = As<FtpFind>(hi, INTERNET_HANDLE_TYPE_FTP_FIND);
  if (!f) return FALSE;
  if (f->next >= f->entries.size()) {
    SetLastError(ERROR_NO_MORE_FILES);
    return FALSE;
  }
  if (out) *(WIN32_FIND_DATAW*)out = f->entries[f->next];
  f->next++;
  return TRUE;
}
BOOL WINAPI InternetFindNextFileA(HINTERNET hi, LPVOID out) {
  WIN32_FIND_DATAW w;
  if (!InternetFindNextFileW(hi, &w)) return FALSE;
  if (out) ToFindDataA(w, (WIN32_FIND_DATAA*)out);
  return TRUE;
}

// Cookies

BOOL WINAPI InternetSetCookieW(LPCWSTR url, LPCWSTR name, LPCWSTR data) {
  if (!url || !data) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  Url u;
  if (!ParseUrl(U(url), &u)) return FALSE;
  std::string text = name && *name ? U(name) + "=" + U(data) : U(data);
  StoreCookie(u.host, u.path.empty() ? "/" : u.path, text);
  return TRUE;
}
BOOL WINAPI InternetSetCookieA(LPCSTR url, LPCSTR name, LPCSTR data) {
  if (!url || !data) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  return InternetSetCookieW(W(url).c_str(), name ? W(name).c_str() : NULL, W(data).c_str());
}

BOOL WINAPI InternetGetCookieW(LPCWSTR url, LPCWSTR, LPWSTR buf, LPDWORD size) {
  if (!url || !size) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  Url u;
  if (!ParseUrl(U(url), &u)) return FALSE;
  std::wstring c = Widen(CookiesFor(u.host, u.path.empty() ? "/" : u.path).c_str());
  if (c.empty()) {
    SetLastError(ERROR_NO_MORE_ITEMS);
    return FALSE;
  }
  // The size is in characters, with the zero.
  if (!buf) {
    *size = (DWORD)c.size() + 1;
    return TRUE;
  }
  if (*size <= c.size()) {
    *size = (DWORD)c.size() + 1;
    SetLastError(ERROR_INSUFFICIENT_BUFFER);
    return FALSE;
  }
  wcscpy(buf, c.c_str());
  *size = (DWORD)c.size();
  return TRUE;
}
BOOL WINAPI InternetGetCookieA(LPCSTR url, LPCSTR, LPSTR buf, LPDWORD size) {
  if (!url || !size) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  Url u;
  if (!ParseUrl(url, &u)) return FALSE;
  std::string c = CookiesFor(u.host, u.path.empty() ? "/" : u.path);
  if (c.empty()) {
    SetLastError(ERROR_NO_MORE_ITEMS);
    return FALSE;
  }
  if (!buf) {
    *size = (DWORD)c.size() + 1;
    return TRUE;
  }
  if (*size <= c.size()) {
    *size = (DWORD)c.size() + 1;
    SetLastError(ERROR_INSUFFICIENT_BUFFER);
    return FALSE;
  }
  strcpy(buf, c.c_str());
  *size = (DWORD)c.size();
  return TRUE;
}

DWORD WINAPI InternetErrorDlg(HWND, HINTERNET, DWORD error, DWORD, LPVOID*) {
  // No dialogs for certificates or passwords: the caller sees the error.
  return error ? error : ERROR_CANCELLED;
}

// HTTP

HINTERNET WINAPI HttpOpenRequestW(HINTERNET hc, LPCWSTR verb, LPCWSTR object, LPCWSTR version, LPCWSTR referrer,
                                  LPCWSTR* accept, DWORD flags, DWORD_PTR ctx) {
  return OpenRequest(hc, verb ? verb : L"", object ? object : L"", version ? version : L"",
                     referrer ? referrer : L"", accept, flags, ctx);
}
HINTERNET WINAPI HttpOpenRequestA(HINTERNET hc, LPCSTR verb, LPCSTR object, LPCSTR version, LPCSTR referrer,
                                  LPCSTR* accept, DWORD flags, DWORD_PTR ctx) {
  std::vector<std::wstring> types;
  std::vector<LPCWSTR> ptrs;
  if (accept)
    for (int i = 0; accept[i]; i++) types.push_back(W(accept[i]));
  for (auto& t : types) ptrs.push_back(t.c_str());
  ptrs.push_back(NULL);
  return OpenRequest(hc, W(verb), W(object), W(version), W(referrer), accept ? ptrs.data() : NULL, flags, ctx);
}

BOOL WINAPI HttpAddRequestHeadersW(HINTERNET hr, LPCWSTR headers, DWORD len, DWORD mods) {
  Request* r = As<Request>(hr, INTERNET_HANDLE_TYPE_HTTP_REQUEST);
  if (!r) return FALSE;
  if (!headers) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  return AddHeaders(r, U(headers, len == (DWORD)-1 || !len ? -1 : (int)len), mods);
}
BOOL WINAPI HttpAddRequestHeadersA(HINTERNET hr, LPCSTR headers, DWORD len, DWORD mods) {
  Request* r = As<Request>(hr, INTERNET_HANDLE_TYPE_HTTP_REQUEST);
  if (!r) return FALSE;
  if (!headers) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  return AddHeaders(r, len == (DWORD)-1 || !len ? std::string(headers) : std::string(headers, len), mods);
}

BOOL WINAPI HttpSendRequestW(HINTERNET hr, LPCWSTR headers, DWORD len, LPVOID body, DWORD bodyLen) {
  Request* r = As<Request>(hr, INTERNET_HANDLE_TYPE_HTTP_REQUEST);
  if (!r) return FALSE;
  return SendRequest(r, headers ? U(headers, len == (DWORD)-1 || !len ? -1 : (int)len) : std::string(), body,
                     bodyLen);
}
BOOL WINAPI HttpSendRequestA(HINTERNET hr, LPCSTR headers, DWORD len, LPVOID body, DWORD bodyLen) {
  Request* r = As<Request>(hr, INTERNET_HANDLE_TYPE_HTTP_REQUEST);
  if (!r) return FALSE;
  std::string h = headers ? (len == (DWORD)-1 || !len ? std::string(headers) : std::string(headers, len)) : "";
  return SendRequest(r, h, body, bodyLen);
}

static BOOL SendRequestEx(HINTERNET hr, const std::string& headers, LPVOID buf, DWORD bufLen, DWORD total) {
  Request* r = As<Request>(hr, INTERNET_HANDLE_TYPE_HTTP_REQUEST);
  if (!r) return FALSE;
  long long length = total ? (long long)total : (long long)bufLen;
  if (!SendHead(r, headers, buf, bufLen, length)) return FALSE;
  r->writing = true;
  return TRUE;
}

BOOL WINAPI HttpSendRequestExW(HINTERNET hr, LPINTERNET_BUFFERSW in, LPINTERNET_BUFFERSW, DWORD, DWORD_PTR) {
  std::string h;
  if (in && in->lpcszHeader)
    h = U(in->lpcszHeader, in->dwHeadersLength == (DWORD)-1 || !in->dwHeadersLength ? -1 : (int)in->dwHeadersLength);
  return SendRequestEx(hr, h, in ? in->lpvBuffer : NULL, in ? in->dwBufferLength : 0, in ? in->dwBufferTotal : 0);
}
BOOL WINAPI HttpSendRequestExA(HINTERNET hr, LPINTERNET_BUFFERSA in, LPINTERNET_BUFFERSA, DWORD, DWORD_PTR) {
  std::string h;
  if (in && in->lpcszHeader)
    h = in->dwHeadersLength == (DWORD)-1 || !in->dwHeadersLength ? std::string(in->lpcszHeader)
                                                                  : std::string(in->lpcszHeader, in->dwHeadersLength);
  return SendRequestEx(hr, h, in ? in->lpvBuffer : NULL, in ? in->dwBufferLength : 0, in ? in->dwBufferTotal : 0);
}

BOOL WINAPI HttpEndRequestW(HINTERNET hr, LPINTERNET_BUFFERSW, DWORD, DWORD_PTR) {
  Request* r = As<Request>(hr, INTERNET_HANDLE_TYPE_HTTP_REQUEST);
  if (!r) return FALSE;
  if (!r->writing) {
    SetLastError(ERROR_INTERNET_INCORRECT_HANDLE_STATE);
    return FALSE;
  }
  r->writing = false;
  return ReadHead(r);
}
BOOL WINAPI HttpEndRequestA(HINTERNET hr, LPINTERNET_BUFFERSA, DWORD f, DWORD_PTR c) {
  return HttpEndRequestW(hr, NULL, f, c);
}

BOOL WINAPI HttpQueryInfoW(HINTERNET hr, DWORD level, LPVOID buf, LPDWORD len, LPDWORD index) {
  return QueryInfo<wchar_t>(hr, level, buf, len, index);
}
BOOL WINAPI HttpQueryInfoA(HINTERNET hr, DWORD level, LPVOID buf, LPDWORD len, LPDWORD index) {
  return QueryInfo<char>(hr, level, buf, len, index);
}

// FTP

HINTERNET WINAPI FtpFindFirstFileW(HINTERNET hc, LPCWSTR pattern, LPWIN32_FIND_DATAW out, DWORD flags, DWORD_PTR ctx) {
  return FtpFindFirst(hc, pattern, out, flags, ctx);
}
HINTERNET WINAPI FtpFindFirstFileA(HINTERNET hc, LPCSTR pattern, LPWIN32_FIND_DATAA out, DWORD flags, DWORD_PTR ctx) {
  WIN32_FIND_DATAW w;
  std::wstring p = W(pattern);
  HINTERNET h = FtpFindFirst(hc, pattern ? p.c_str() : NULL, &w, flags, ctx);
  if (h && out) ToFindDataA(w, out);
  return h;
}

BOOL WINAPI FtpGetFileW(HINTERNET hc, LPCWSTR remote, LPCWSTR local, BOOL failIfExists, DWORD, DWORD flags,
                        DWORD_PTR ctx) {
  return FtpGet(hc, remote, local, failIfExists, flags, ctx);
}
BOOL WINAPI FtpGetFileA(HINTERNET hc, LPCSTR remote, LPCSTR local, BOOL failIfExists, DWORD, DWORD flags,
                        DWORD_PTR ctx) {
  return FtpGet(hc, remote ? W(remote).c_str() : NULL, local ? W(local).c_str() : NULL, failIfExists, flags, ctx);
}

BOOL WINAPI FtpPutFileW(HINTERNET hc, LPCWSTR local, LPCWSTR remote, DWORD flags, DWORD_PTR ctx) {
  return FtpPut(hc, local, remote, flags, ctx);
}
BOOL WINAPI FtpPutFileA(HINTERNET hc, LPCSTR local, LPCSTR remote, DWORD flags, DWORD_PTR ctx) {
  return FtpPut(hc, local ? W(local).c_str() : NULL, remote ? W(remote).c_str() : NULL, flags, ctx);
}

HINTERNET WINAPI FtpOpenFileW(HINTERNET hc, LPCWSTR name, DWORD access, DWORD flags, DWORD_PTR ctx) {
  return FtpOpen(hc, name, access, flags, ctx);
}
HINTERNET WINAPI FtpOpenFileA(HINTERNET hc, LPCSTR name, DWORD access, DWORD flags, DWORD_PTR ctx) {
  return FtpOpen(hc, name ? W(name).c_str() : NULL, access, flags, ctx);
}

DWORD WINAPI FtpGetFileSize(HINTERNET hf, LPDWORD high) {
  Handle* h = InetHandle(hf);
  if (!h || h->type != INTERNET_HANDLE_TYPE_FTP_FILE || ((FtpFile*)h)->size < 0) {
    SetLastError(h ? ERROR_INTERNET_INCORRECT_HANDLE_TYPE : ERROR_INVALID_HANDLE);
    return (DWORD)-1;
  }
  long long s = ((FtpFile*)h)->size;
  if (high) *high = (DWORD)(s >> 32);
  SetLastError(NO_ERROR);
  return (DWORD)s;
}

BOOL WINAPI FtpDeleteFileW(HINTERNET hc, LPCWSTR name) { return name && FtpSimple(hc, "DELE " + U(name)); }
BOOL WINAPI FtpDeleteFileA(HINTERNET hc, LPCSTR name) { return name && FtpSimple(hc, std::string("DELE ") + name); }

static BOOL Rename(HINTERNET hc, const std::string& from, const std::string& to) {
  Connection* c = As<Connection>(hc, INTERNET_HANDLE_TYPE_CONNECT_FTP);
  if (!c || FtpBusy(c)) return FALSE;
  int code = FtpCommand(c, "RNFR " + from);
  if (code != 350) {
    if (code >= 0) SetLastError(ERROR_INTERNET_EXTENDED_ERROR);
    return FALSE;
  }
  return FtpOk(c, "RNTO " + to);
}
BOOL WINAPI FtpRenameFileW(HINTERNET hc, LPCWSTR from, LPCWSTR to) {
  return from && to && Rename(hc, U(from), U(to));
}
BOOL WINAPI FtpRenameFileA(HINTERNET hc, LPCSTR from, LPCSTR to) { return from && to && Rename(hc, from, to); }

BOOL WINAPI FtpCreateDirectoryW(HINTERNET hc, LPCWSTR dir) { return dir && FtpSimple(hc, "MKD " + U(dir)); }
BOOL WINAPI FtpCreateDirectoryA(HINTERNET hc, LPCSTR dir) { return dir && FtpSimple(hc, std::string("MKD ") + dir); }
BOOL WINAPI FtpRemoveDirectoryW(HINTERNET hc, LPCWSTR dir) { return dir && FtpSimple(hc, "RMD " + U(dir)); }
BOOL WINAPI FtpRemoveDirectoryA(HINTERNET hc, LPCSTR dir) { return dir && FtpSimple(hc, std::string("RMD ") + dir); }
BOOL WINAPI FtpSetCurrentDirectoryW(HINTERNET hc, LPCWSTR dir) { return dir && FtpSimple(hc, "CWD " + U(dir)); }
BOOL WINAPI FtpSetCurrentDirectoryA(HINTERNET hc, LPCSTR dir) {
  return dir && FtpSimple(hc, std::string("CWD ") + dir);
}

BOOL WINAPI FtpGetCurrentDirectoryW(HINTERNET hc, LPWSTR buf, LPDWORD len) {
  std::string d;
  if (!len || !FtpGetDir(hc, &d)) return FALSE;
  std::wstring w = Widen(d.c_str());
  if (!buf || *len <= w.size()) {
    *len = (DWORD)w.size() + 1;
    SetLastError(ERROR_INSUFFICIENT_BUFFER);
    return FALSE;
  }
  wcscpy(buf, w.c_str());
  *len = (DWORD)w.size();
  return TRUE;
}
BOOL WINAPI FtpGetCurrentDirectoryA(HINTERNET hc, LPSTR buf, LPDWORD len) {
  std::string d;
  if (!len || !FtpGetDir(hc, &d)) return FALSE;
  if (!buf || *len <= d.size()) {
    *len = (DWORD)d.size() + 1;
    SetLastError(ERROR_INSUFFICIENT_BUFFER);
    return FALSE;
  }
  strcpy(buf, d.c_str());
  *len = (DWORD)d.size();
  return TRUE;
}

}  // extern "C"
