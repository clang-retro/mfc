// tls.cpp - TLS on a connected socket, for WinInet's HTTPS.
//
// On macOS the system's Secure Transport does it; elsewhere OpenSSL
// (libssl 3 or 1.1). Either is loaded when a program first asks for TLS,
// so a program runs without it, and its functions are declared here, so
// that building needs neither's headers. WINPORT_LIBSSL names OpenSSL's
// library when it is not one of the usual ones.
//
// The certificate is checked as WinInet checks it: whether it comes from
// an authority the system trusts, whether it is for the host asked for
// (its subjectAltName, or its common name without one), and whether it is
// in date. Each problem is an error of its own, which the caller may
// choose to ignore (SECURITY_FLAG_IGNORE_UNKNOWN_CA and the others).
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include "tls.h"

#include "winport.h"

#include <wininet.h>
#include <arpa/inet.h>
#include <dlfcn.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include <algorithm>
#include <map>
#include <mutex>

namespace winport {
namespace tls {
namespace {

void Fail(unsigned problems, unsigned ignore) {
  unsigned left = problems & ~ignore;
  if (left & kUnknownCa) SetLastError(ERROR_INTERNET_INVALID_CA);
  else if (left & kNameMismatch) SetLastError(ERROR_INTERNET_SEC_CERT_CN_INVALID);
  else if (left & kDateInvalid) SetLastError(ERROR_INTERNET_SEC_CERT_DATE_INVALID);
  else SetLastError(ERROR_INTERNET_SEC_INVALID_CERT);
}

template <class F>
bool Find(void* lib, const char* name, F* out) {
  *out = lib ? (F)dlsym(lib, name) : NULL;
  return *out != NULL;
}

#ifdef __APPLE__
// ---------------------------------------------------------------------
// Secure Transport

typedef int32_t OSStatus;
typedef unsigned char Boolean;
typedef struct OpaqueSSLContext* SSLContextRef;
typedef const void* SSLConnectionRef;
typedef struct OpaqueSecTrust* SecTrustRef;
typedef struct OpaqueSecPolicy* SecPolicyRef;
typedef struct OpaqueSecCertificate* SecCertificateRef;
typedef const void* CFTypeRef;
typedef const struct OpaqueCFString* CFStringRef;
typedef const struct OpaqueCFArray* CFArrayRef;
typedef long CFIndex;
typedef OSStatus (*SSLReadFunc)(SSLConnectionRef, void*, size_t*);
typedef OSStatus (*SSLWriteFunc)(SSLConnectionRef, const void*, size_t*);

const OSStatus kNoErr = 0;
const OSStatus kErrWouldBlock = -9803;
const OSStatus kErrClosedGraceful = -9805;
const OSStatus kErrClosedAbort = -9806;
const OSStatus kErrClosedNoNotify = -9816;
const OSStatus kErrServerAuthCompleted = -9841;
const int kBreakOnServerAuth = 0;
const uint32_t kResultProceed = 1, kResultUnspecified = 4;
const uint32_t kAllowExpired = 0x1, kAllowExpiredRoot = 0x8;
const uint32_t kUtf8 = 0x08000100;

struct Api {
  OSStatus (*NewContext)(Boolean, SSLContextRef*);
  OSStatus (*DisposeContext)(SSLContextRef);
  OSStatus (*SetIOFuncs)(SSLContextRef, SSLReadFunc, SSLWriteFunc);
  OSStatus (*SetConnection)(SSLContextRef, SSLConnectionRef);
  OSStatus (*SetPeerDomainName)(SSLContextRef, const char*, size_t);
  OSStatus (*SetSessionOption)(SSLContextRef, int, Boolean);
  OSStatus (*Handshake)(SSLContextRef);
  OSStatus (*CopyPeerTrust)(SSLContextRef, SecTrustRef*);
  OSStatus (*Read)(SSLContextRef, void*, size_t, size_t*);
  OSStatus (*Write)(SSLContextRef, const void*, size_t, size_t*);
  OSStatus (*Close)(SSLContextRef);
  OSStatus (*TrustEvaluate)(SecTrustRef, uint32_t*);
  OSStatus (*TrustSetPolicies)(SecTrustRef, CFTypeRef);
  OSStatus (*TrustSetOptions)(SecTrustRef, uint32_t);
  OSStatus (*TrustSetAnchors)(SecTrustRef, CFArrayRef);
  SecCertificateRef (*TrustCertificate)(SecTrustRef, CFIndex);
  SecPolicyRef (*PolicySsl)(Boolean, CFStringRef);
  void (*Release)(CFTypeRef);
  CFStringRef (*StringCreate)(const void*, const char*, uint32_t);
  CFArrayRef (*ArrayCreate)(const void*, const void**, CFIndex, const void*);
  const void* arrayCallBacks;
};

const Api* Load() {
  static Api api;
  static bool ok = false;
  static std::once_flag once;
  std::call_once(once, [] {
    void* sec = dlopen("/System/Library/Frameworks/Security.framework/Security", RTLD_NOW | RTLD_LOCAL);
    void* cf = dlopen("/System/Library/Frameworks/CoreFoundation.framework/CoreFoundation", RTLD_NOW | RTLD_LOCAL);
    ok = Find(sec, "SSLNewContext", &api.NewContext) && Find(sec, "SSLDisposeContext", &api.DisposeContext) &&
         Find(sec, "SSLSetIOFuncs", &api.SetIOFuncs) && Find(sec, "SSLSetConnection", &api.SetConnection) &&
         Find(sec, "SSLSetPeerDomainName", &api.SetPeerDomainName) &&
         Find(sec, "SSLSetSessionOption", &api.SetSessionOption) && Find(sec, "SSLHandshake", &api.Handshake) &&
         Find(sec, "SSLCopyPeerTrust", &api.CopyPeerTrust) && Find(sec, "SSLRead", &api.Read) &&
         Find(sec, "SSLWrite", &api.Write) && Find(sec, "SSLClose", &api.Close) &&
         Find(sec, "SecTrustEvaluate", &api.TrustEvaluate) && Find(sec, "SecTrustSetPolicies", &api.TrustSetPolicies) &&
         Find(sec, "SecTrustSetOptions", &api.TrustSetOptions) &&
         Find(sec, "SecTrustSetAnchorCertificates", &api.TrustSetAnchors) &&
         Find(sec, "SecTrustGetCertificateAtIndex", &api.TrustCertificate) &&
         Find(sec, "SecPolicyCreateSSL", &api.PolicySsl) && Find(cf, "CFRelease", &api.Release) &&
         Find(cf, "CFStringCreateWithCString", &api.StringCreate) && Find(cf, "CFArrayCreate", &api.ArrayCreate) &&
         Find(cf, "kCFTypeArrayCallBacks", &api.arrayCallBacks);
    if (!ok && getenv("WINPORT_DEBUG")) fprintf(stderr, "winport: Secure Transport not found; no HTTPS\n");
  });
  return ok ? &api : NULL;
}

OSStatus ReadSocket(SSLConnectionRef conn, void* data, size_t* len) {
  int fd = (int)(intptr_t)conn;
  size_t want = *len, got = 0;
  while (got < want) {
    ssize_t r = recv(fd, (char*)data + got, want - got, 0);
    if (r < 0 && errno == EINTR) continue;
    if (r < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
      *len = got;
      return kErrWouldBlock;
    }
    if (r <= 0) {
      *len = got;
      return r == 0 ? kErrClosedGraceful : kErrClosedAbort;
    }
    got += (size_t)r;
  }
  *len = got;
  return kNoErr;
}

OSStatus WriteSocket(SSLConnectionRef conn, const void* data, size_t* len) {
  int fd = (int)(intptr_t)conn;
  size_t want = *len, put = 0;
  while (put < want) {
    ssize_t r = send(fd, (const char*)data + put, want - put, 0);
    if (r < 0 && errno == EINTR) continue;
    if (r <= 0) {
      *len = put;
      return kErrClosedAbort;
    }
    put += (size_t)r;
  }
  *len = put;
  return kNoErr;
}

bool Trusted(const Api* a, SecTrustRef trust) {
  uint32_t result = 0;
  return a->TrustEvaluate(trust, &result) == kNoErr && (result == kResultProceed || result == kResultUnspecified);
}

// What is wrong with the server's certificate: each problem found by
// asking again with the others let go.
unsigned Problems(const Api* a, SecTrustRef trust, const std::string& host) {
  if (Trusted(a, trust)) return 0;
  unsigned p = 0;
  SecPolicyRef anyHost = a->PolicySsl(1, NULL);
  CFStringRef name = a->StringCreate(NULL, host.c_str(), kUtf8);
  SecPolicyRef thisHost = a->PolicySsl(1, name);
  a->TrustSetPolicies(trust, anyHost);
  a->TrustSetOptions(trust, kAllowExpired | kAllowExpiredRoot);
  if (!Trusted(a, trust)) {
    // Its authority: trusted for the moment, to see what else there is.
    p |= kUnknownCa;
    const void* leaf = a->TrustCertificate(trust, 0);
    CFArrayRef anchors = leaf ? a->ArrayCreate(NULL, &leaf, 1, a->arrayCallBacks) : NULL;
    if (anchors) {
      a->TrustSetAnchors(trust, anchors);
      a->Release(anchors);
    }
    if (!Trusted(a, trust)) p |= kOther;
  }
  if (!(p & kOther)) {
    a->TrustSetPolicies(trust, thisHost);
    if (!Trusted(a, trust)) p |= kNameMismatch;
    a->TrustSetPolicies(trust, anyHost);
    a->TrustSetOptions(trust, 0);
    if (!Trusted(a, trust)) p |= kDateInvalid;
  }
  if (anyHost) a->Release(anyHost);
  if (thisHost) a->Release(thisHost);
  if (name) a->Release(name);
  return p ? p : kOther;
}

typedef SSLContextRef Session;

Session Handshake(int fd, const std::string& host, unsigned ignore) {
  const Api* a = Load();
  if (!a) {
    SetLastError(ERROR_INTERNET_SECURITY_CHANNEL_ERROR);
    return NULL;
  }
  SSLContextRef ctx = NULL;
  if (a->NewContext(0, &ctx) != kNoErr || !ctx) {
    SetLastError(ERROR_INTERNET_SECURITY_CHANNEL_ERROR);
    return NULL;
  }
  a->SetIOFuncs(ctx, ReadSocket, WriteSocket);
  a->SetConnection(ctx, (SSLConnectionRef)(intptr_t)fd);
  a->SetPeerDomainName(ctx, host.c_str(), host.size());
  a->SetSessionOption(ctx, kBreakOnServerAuth, 1);
  for (;;) {
    OSStatus st = a->Handshake(ctx);
    if (st == kNoErr) return ctx;
    if (st == kErrServerAuthCompleted) {
      SecTrustRef trust = NULL;
      unsigned p = kOther;
      if (a->CopyPeerTrust(ctx, &trust) == kNoErr && trust) {
        p = Problems(a, trust, host);
        a->Release(trust);
      }
      if (p & ~ignore) {
        Fail(p, ignore);
        a->Close(ctx);
        a->DisposeContext(ctx);
        return NULL;
      }
      continue;
    }
    if (st == kErrWouldBlock) continue;
    a->DisposeContext(ctx);
    SetLastError(ERROR_INTERNET_SECURITY_CHANNEL_ERROR);
    return NULL;
  }
}

ssize_t SessionSend(Session s, const void* p, size_t n) {
  size_t done = 0;
  OSStatus st = Load()->Write(s, p, n, &done);
  if (st != kNoErr && done == 0) {
    errno = EPIPE;
    return -1;
  }
  return (ssize_t)done;
}

ssize_t SessionRecv(Session s, void* p, size_t n) {
  for (;;) {
    size_t done = 0;
    OSStatus st = Load()->Read(s, p, n, &done);
    if (done > 0) return (ssize_t)done;
    if (st == kErrClosedGraceful || st == kErrClosedNoNotify) return 0;
    if (st == kErrWouldBlock) {
      errno = EAGAIN;
      return -1;
    }
    if (st == kNoErr) continue;
    errno = ECONNRESET;
    return -1;
  }
}

void SessionEnd(Session s) {
  Load()->Close(s);
  Load()->DisposeContext(s);
}

#else
// ---------------------------------------------------------------------
// OpenSSL

bool IsAddress(const std::string& host) {
  unsigned char buf[16];
  return inet_pton(AF_INET, host.c_str(), buf) == 1 || inet_pton(AF_INET6, host.c_str(), buf) == 1;
}

struct SSL_CTX;
struct SSL;
struct SSL_METHOD;
struct X509;
struct X509_STORE_CTX;
typedef int (*VerifyCallback)(int, X509_STORE_CTX*);

const int kVerifyPeer = 1;
const int kErrorWantRead = 2, kErrorWantWrite = 3, kErrorSyscall = 5, kErrorZeroReturn = 6;
const long kCtrlSetHostName = 55;

struct Api {
  int (*Init)(uint64_t, const void*);
  const SSL_METHOD* (*ClientMethod)();
  SSL_CTX* (*CtxNew)(const SSL_METHOD*);
  int (*CtxDefaultPaths)(SSL_CTX*);
  void (*CtxSetVerify)(SSL_CTX*, int, VerifyCallback);
  SSL* (*New)(SSL_CTX*);
  void (*Free)(SSL*);
  int (*SetFd)(SSL*, int);
  long (*Ctrl)(SSL*, int, long, void*);
  int (*Connect)(SSL*);
  int (*Read)(SSL*, void*, int);
  int (*Write)(SSL*, const void*, int);
  int (*Shutdown)(SSL*);
  int (*GetError)(const SSL*, int);
  X509* (*PeerCertificate)(const SSL*);
  void (*X509Free)(X509*);
  int (*CheckHost)(X509*, const char*, size_t, unsigned, char**);
  int (*CheckIp)(X509*, const char*, unsigned);
  int (*StoreError)(X509_STORE_CTX*);
  SSL_CTX* ctx;
};

thread_local unsigned t_problems;
const Api* g_api;

unsigned ProblemOf(int err) {
  switch (err) {
    case 0: return 0;
    case 9:   // not yet valid
    case 10:  // expired
    case 13:  // a bad notBefore
    case 14:  // a bad notAfter
      return kDateInvalid;
    case 2:   // the issuer's certificate is not there
    case 18:  // self-signed
    case 19:  // a self-signed certificate in the chain
    case 20:  // the issuer is not known here
    case 21:  // the leaf cannot be checked
    case 24:  // not a CA
    case 27:  // not trusted
      return kUnknownCa;
    case 62:  // the host's name
    case 64:  // the IP address
      return kNameMismatch;
    default: return kOther;
  }
}

// Every problem in the chain is noted, and none stops the handshake here:
// the caller decides afterwards which of them matter.
int NoteProblem(int, X509_STORE_CTX* store) {
  t_problems |= ProblemOf(g_api->StoreError(store));
  return 1;
}

const Api* Load() {
  static Api api;
  static bool ok = false;
  static std::once_flag once;
  std::call_once(once, [] {
    void* lib = NULL;
    const char* env = getenv("WINPORT_LIBSSL");
    if (env && *env) {
      lib = dlopen(env, RTLD_NOW | RTLD_LOCAL);
    } else {
      static const char* const names[] = {"libssl.so.3", "libssl.so.1.1", "libssl.so", "libssl.3.dylib"};
      for (const char* n : names)
        if ((lib = dlopen(n, RTLD_NOW | RTLD_LOCAL)) != NULL) break;
    }
    ok = Find(lib, "OPENSSL_init_ssl", &api.Init) && Find(lib, "TLS_client_method", &api.ClientMethod) &&
         Find(lib, "SSL_CTX_new", &api.CtxNew) && Find(lib, "SSL_CTX_set_default_verify_paths", &api.CtxDefaultPaths) &&
         Find(lib, "SSL_CTX_set_verify", &api.CtxSetVerify) && Find(lib, "SSL_new", &api.New) &&
         Find(lib, "SSL_free", &api.Free) && Find(lib, "SSL_set_fd", &api.SetFd) && Find(lib, "SSL_ctrl", &api.Ctrl) &&
         Find(lib, "SSL_connect", &api.Connect) && Find(lib, "SSL_read", &api.Read) &&
         Find(lib, "SSL_write", &api.Write) && Find(lib, "SSL_shutdown", &api.Shutdown) &&
         Find(lib, "SSL_get_error", &api.GetError) &&
         (Find(lib, "SSL_get1_peer_certificate", &api.PeerCertificate) ||
          Find(lib, "SSL_get_peer_certificate", &api.PeerCertificate)) &&
         Find(lib, "X509_free", &api.X509Free) && Find(lib, "X509_check_host", &api.CheckHost) &&
         Find(lib, "X509_check_ip_asc", &api.CheckIp) && Find(lib, "X509_STORE_CTX_get_error", &api.StoreError);
    if (ok) {
      api.Init(0, NULL);
      api.ctx = api.CtxNew(api.ClientMethod());
      ok = api.ctx != NULL;
      if (ok) {
        api.CtxDefaultPaths(api.ctx);
        api.CtxSetVerify(api.ctx, kVerifyPeer, NoteProblem);
        g_api = &api;
      }
    }
    if (!ok && getenv("WINPORT_DEBUG")) fprintf(stderr, "winport: no OpenSSL (libssl) found; no HTTPS\n");
  });
  return ok ? &api : NULL;
}

// OpenSSL writes to the socket itself: a peer that has gone would raise
// SIGPIPE, so it is held back while it writes, and taken back if it came.
#if defined(macintosh)
// The classic Mac OS sends no SIGPIPE: a write to a closed socket fails
// with EPIPE only.
struct NoSigpipe {
  NoSigpipe() {}
};
#else
struct NoSigpipe {
  sigset_t old, pipe;
  NoSigpipe() {
    sigemptyset(&pipe);
    sigaddset(&pipe, SIGPIPE);
    pthread_sigmask(SIG_BLOCK, &pipe, &old);
  }
  ~NoSigpipe() {
    sigset_t pending;
    sigpending(&pending);
    if (sigismember(&pending, SIGPIPE) && !sigismember(&old, SIGPIPE)) {
      timespec zero = {0, 0};
      sigtimedwait(&pipe, NULL, &zero);
    }
    pthread_sigmask(SIG_SETMASK, &old, NULL);
  }
};
#endif

typedef SSL* Session;

Session Handshake(int fd, const std::string& host, unsigned ignore) {
  const Api* a = Load();
  if (!a) {
    SetLastError(ERROR_INTERNET_SECURITY_CHANNEL_ERROR);
    return NULL;
  }
  SSL* ssl = a->New(a->ctx);
  if (!ssl) {
    SetLastError(ERROR_INTERNET_SECURITY_CHANNEL_ERROR);
    return NULL;
  }
  a->SetFd(ssl, fd);
  bool address = IsAddress(host);
  if (!address) a->Ctrl(ssl, kCtrlSetHostName, 0, (void*)host.c_str());
  t_problems = 0;
  int r;
  {
    NoSigpipe guard;
    r = a->Connect(ssl);
  }
  if (r != 1) {
    a->Free(ssl);
    SetLastError(ERROR_INTERNET_SECURITY_CHANNEL_ERROR);
    return NULL;
  }
  unsigned p = t_problems;
  X509* cert = a->PeerCertificate(ssl);
  if (!cert) {
    p |= kOther;
  } else {
    int match = address ? a->CheckIp(cert, host.c_str(), 0) : a->CheckHost(cert, host.c_str(), host.size(), 0, NULL);
    if (match != 1) p |= kNameMismatch;
    a->X509Free(cert);
  }
  if (p & ~ignore) {
    Fail(p, ignore);
    {
      NoSigpipe guard;
      a->Shutdown(ssl);
    }
    a->Free(ssl);
    return NULL;
  }
  return ssl;
}

ssize_t SessionSend(Session s, const void* p, size_t n) {
  NoSigpipe guard;
  int r = g_api->Write(s, p, (int)std::min(n, (size_t)0x7fffffff));
  if (r > 0) return r;
  errno = EPIPE;
  return -1;
}

ssize_t SessionRecv(Session s, void* p, size_t n) {
  int r = g_api->Read(s, p, (int)std::min(n, (size_t)0x7fffffff));
  if (r > 0) return r;
  int e = g_api->GetError(s, r);
  if (e == kErrorZeroReturn) return 0;
  if (e == kErrorSyscall && r == 0) return 0;  // closed without a close_notify
  if (e == kErrorWantRead || e == kErrorWantWrite) {
    errno = EAGAIN;
    return -1;
  }
  if (e != kErrorSyscall) errno = ECONNRESET;
  return -1;
}

void SessionEnd(Session s) {
  {
    NoSigpipe guard;
    g_api->Shutdown(s);
  }
  g_api->Free(s);
}
#endif

std::mutex g_lock;
std::map<int, Session>& Sessions() {
  static auto* m = new std::map<int, Session>;
  return *m;
}

Session SessionOf(int fd) {
  std::lock_guard<std::mutex> g(g_lock);
  auto it = Sessions().find(fd);
  return it == Sessions().end() ? NULL : it->second;
}

}  // namespace

bool Start(int fd, const std::string& host, unsigned ignore) {
  Session s = Handshake(fd, host, ignore);
  if (!s) return false;
  std::lock_guard<std::mutex> g(g_lock);
  Sessions()[fd] = s;
  return true;
}

bool Active(int fd) { return SessionOf(fd) != NULL; }

ssize_t Send(int fd, const void* p, size_t n) {
  if (Session s = SessionOf(fd)) return SessionSend(s, p, n);
#ifdef MSG_NOSIGNAL
  return send(fd, p, n, MSG_NOSIGNAL);
#else
  return send(fd, p, n, 0);
#endif
}

ssize_t Recv(int fd, void* p, size_t n) {
  if (Session s = SessionOf(fd)) return SessionRecv(s, p, n);
  return recv(fd, p, n, 0);
}

void Close(int fd) {
  if (fd < 0) return;
  Session s = NULL;
  {
    std::lock_guard<std::mutex> g(g_lock);
    auto it = Sessions().find(fd);
    if (it != Sessions().end()) {
      s = it->second;
      Sessions().erase(it);
    }
  }
  if (s) SessionEnd(s);
  close(fd);
}

}  // namespace tls
}  // namespace winport
