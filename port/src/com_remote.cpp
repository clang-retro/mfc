// com_remote.cpp - COM between processes: a program's objects called
// from another program through proxies, over Unix domain sockets.
//
// Each program that hands out an object listens on a socket of its own
// (ep-<pid> in the COM directory). Another program connects to it, says
// who it is, and sends calls: an object, an interface, a method and the
// arguments in the form each interface's proxy and stub agree on. The
// calls run on the thread that owns the objects (the apartment): a
// message-only window there is woken for them, and a thread waiting for
// its own call's reply runs them too, so that a call back into the
// caller (an advise sink, a client site) gets through.
//
// A class object registered for CLSCTX_LOCAL_SERVER is published as a
// file naming the program's process; CoGetClassObject finds it there or
// starts the class's LocalServer32 command with -Embedding and waits for
// it. The class files are kept apart for each registry file
// (WINPORT_REGISTRY), as the registrations they stand for are.
//
// References: each one handed to another program holds one reference to
// the object, given back when that program's proxy goes or the program
// ends.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include "com.h"
#include "com_wire.h"

#include <oaidl.h>
#include <ole2.h>
#include <oleauto.h>

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using namespace winport;
using namespace winport::com;

namespace winport {
namespace com {

// ---------------------------------------------------------------------
// The state

namespace {

enum : uint32_t { kHello = 1, kCall = 2, kOneWay = 3, kReply = 4, kGone = 5 };

// The process's own object: oid 0, whose methods these are.
enum : uint32_t { kGetClassObject = 0, kRemRelease = 1, kRemAddRef = 2, kRemQueryInterface = 3 };

struct Conn {
  int fd = -1;
  DWORD peer = 0;
  bool outgoing = false;
  std::mutex send;
  bool dead = false;  // guarded by g_mu
};

struct Incoming {
  std::shared_ptr<Conn> conn;
  uint32_t kind = 0, id = 0;
  DWORD peer = 0;
  std::vector<BYTE> body;
};

struct Pending {
  Conn* conn;
  bool done = false;
  std::vector<BYTE> body;
};

struct Export {
  IUnknown* unk = NULL;  // the identity
  std::map<DWORD, LONG> refs;  // the references each program holds
};

std::mutex g_mu;
std::condition_variable g_cv;
std::deque<Incoming> g_incoming;
std::map<uint32_t, Pending*> g_pending;
uint32_t g_nextId = 1;
std::map<DWORD, std::shared_ptr<Conn>> g_out;  // to each program
std::map<uint64_t, Export> g_exports;
std::map<IUnknown*, uint64_t> g_exportOf;
uint64_t g_nextOid = 1;
std::map<std::pair<DWORD, uint64_t>, ProxyManager*> g_proxies;
std::vector<GUID> g_published;

DWORD g_aptTid = 0;
HWND g_wnd = NULL;
bool g_wakePosted = false;
int g_listen = -1;
const UINT kWakeMsg = WM_USER + 0x4f43;  // "OC"

const HRESULT kDisconnected = (HRESULT)0x80010108L;  // RPC_E_DISCONNECTED
const HRESULT kServerFault = (HRESULT)0x80010105L;   // RPC_E_SERVERFAULT

DWORD Self() { return (DWORD)getpid(); }

// ---------------------------------------------------------------------
// Places

std::string Dir() {
  static std::string dir = [] {
    const char* run = getenv("XDG_RUNTIME_DIR");
    std::string d = run && *run ? std::string(run) + "/winport-com" : "/tmp/winport-com-" + std::to_string(getuid());
    mkdir(d.c_str(), 0700);
    return d;
  }();
  return dir;
}

std::string EndpointPath(DWORD pid) { return Dir() + "/ep-" + std::to_string(pid); }

std::string ClassPath(REFCLSID clsid) {
  // The registry file the class's registration is in (FNV-1a of its name).
  const char* reg = getenv("WINPORT_REGISTRY");
  uint32_t h = 2166136261u;
  for (const char* p = reg ? reg : ""; *p; p++) h = (h ^ (BYTE)*p) * 16777619u;
  WCHAR guid[40];
  StringFromGUID2(clsid, guid, 40);
  char name[80];
  snprintf(name, sizeof name, "/cls-%08x-", h);
  std::string s = Dir() + name;
  for (int i = 0; guid[i]; i++) s += (char)guid[i];
  return s;
}

bool Alive(DWORD pid) { return pid != 0 && (kill((pid_t)pid, 0) == 0 || errno == EPERM); }

DWORD ReadClassFile(REFCLSID clsid) {
  FILE* f = fopen(ClassPath(clsid).c_str(), "r");
  if (f == NULL) return 0;
  unsigned long pid = 0;
  if (fscanf(f, "%lu", &pid) != 1) pid = 0;
  fclose(f);
  return (DWORD)pid;
}

void Cleanup() {
  if (g_listen >= 0) unlink(EndpointPath(Self()).c_str());
  for (const GUID& g : g_published)
    if (ReadClassFile(g) == Self()) unlink(ClassPath(g).c_str());
}

// ---------------------------------------------------------------------
// Sockets

bool WriteAll(int fd, const void* p, size_t n) {
  const char* c = (const char*)p;
  while (n > 0) {
#ifdef MSG_NOSIGNAL
    ssize_t r = send(fd, c, n, MSG_NOSIGNAL);
#else
    ssize_t r = send(fd, c, n, 0);
#endif
    if (r < 0 && errno == EINTR) continue;
    if (r <= 0) return false;
    c += r;
    n -= (size_t)r;
  }
  return true;
}

bool ReadAll(int fd, void* p, size_t n) {
  char* c = (char*)p;
  while (n > 0) {
    ssize_t r = recv(fd, c, n, 0);
    if (r < 0 && errno == EINTR) continue;
    if (r <= 0) return false;
    c += r;
    n -= (size_t)r;
  }
  return true;
}

void NoInherit(int fd) {
  fcntl(fd, F_SETFD, FD_CLOEXEC);
#ifdef SO_NOSIGPIPE
  int one = 1;
  setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
#endif
}

bool SendMsg(Conn* c, uint32_t kind, uint32_t id, const std::vector<BYTE>& body) {
  uint32_t head[3] = {(uint32_t)body.size(), kind, id};
  std::lock_guard<std::mutex> g(c->send);
  return WriteAll(c->fd, head, sizeof head) && (body.empty() || WriteAll(c->fd, body.data(), body.size()));
}

void Wake() {  // with g_mu held
  g_cv.notify_all();
  if (g_wnd != NULL && !g_wakePosted) {
    g_wakePosted = true;
    PostMessageW(g_wnd, kWakeMsg, 0, 0);
  }
}

void ReaderLoop(std::shared_ptr<Conn> c) {
  for (;;) {
    uint32_t head[3];
    if (!ReadAll(c->fd, head, sizeof head)) break;
    std::vector<BYTE> body(head[0]);
    if (head[0] && !ReadAll(c->fd, body.data(), body.size())) break;
    std::lock_guard<std::mutex> g(g_mu);
    if (head[1] == kHello) {
      Reader r(body);
      c->peer = r.U32();
    } else if (head[1] == kReply) {
      auto it = g_pending.find(head[2]);
      if (it != g_pending.end()) {
        it->second->body.swap(body);
        it->second->done = true;
        g_cv.notify_all();
      }
    } else {
      Incoming in;
      in.conn = c;
      in.kind = head[1];
      in.id = head[2];
      in.peer = c->peer;
      in.body.swap(body);
      g_incoming.push_back(std::move(in));
      Wake();
    }
  }
  std::lock_guard<std::mutex> g(g_mu);
  c->dead = true;
  for (auto& p : g_pending)
    if (p.second->conn == c.get()) p.second->done = true;  // with no body: disconnected
  if (c->outgoing) {
    auto it = g_out.find(c->peer);
    if (it != g_out.end() && it->second == c) g_out.erase(it);
  }
  // The program is gone (its socket, or its connection to this one, is
  // closed only when it ends): what it held goes.
  Incoming in;
  in.kind = kGone;
  in.peer = c->peer;
  g_incoming.push_back(std::move(in));
  Wake();
}

std::shared_ptr<Conn> Connect(DWORD pid) {
  {
    std::lock_guard<std::mutex> g(g_mu);
    auto it = g_out.find(pid);
    if (it != g_out.end()) return it->second;
  }
  int fd = socket(AF_UNIX, SOCK_STREAM, 0);
  if (fd < 0) return NULL;
  NoInherit(fd);
  sockaddr_un sa = {};
  sa.sun_family = AF_UNIX;
  std::string path = EndpointPath(pid);
  snprintf(sa.sun_path, sizeof sa.sun_path, "%s", path.c_str());
  if (connect(fd, (sockaddr*)&sa, sizeof sa) != 0) {
    close(fd);
    return NULL;
  }
  auto c = std::make_shared<Conn>();
  c->fd = fd;
  c->peer = pid;
  c->outgoing = true;
  Writer w;
  w.U32(Self());
  if (!SendMsg(c.get(), kHello, 0, w.b)) {
    close(fd);
    return NULL;
  }
  {
    std::lock_guard<std::mutex> g(g_mu);
    auto it = g_out.find(pid);
    if (it != g_out.end()) {  // another thread was quicker
      close(fd);
      return it->second;
    }
    g_out[pid] = c;
  }
  std::thread(ReaderLoop, c).detach();
  return c;
}

void ListenLoop(int fd) {
  for (;;) {
    int s = accept(fd, NULL, NULL);
    if (s < 0) {
      if (errno == EINTR || errno == ECONNABORTED) continue;
      return;
    }
    NoInherit(s);
    auto c = std::make_shared<Conn>();
    c->fd = s;
    std::thread(ReaderLoop, c).detach();
  }
}

LRESULT CALLBACK ApartmentProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
  if (msg == kWakeMsg) {
    {
      std::lock_guard<std::mutex> g(g_mu);
      g_wakePosted = false;
    }
    Drain();
    return 0;
  }
  return DefWindowProcW(h, msg, wp, lp);
}

// The apartment: the thread that first hands out an object, with a
// window that wakes it for calls.
void EnsureApartment() {
  if (g_wnd != NULL) return;
  g_aptTid = GetCurrentThreadId();
  WNDCLASSW wc = {};
  wc.lpfnWndProc = ApartmentProc;
  wc.lpszClassName = L"OleMainThreadWndClass";
  RegisterClassW(&wc);
  HWND h = CreateWindowExW(0, wc.lpszClassName, L"OleMainThreadWndName", 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, NULL,
                           NULL);
  std::lock_guard<std::mutex> g(g_mu);
  g_wnd = h;
  if (!g_incoming.empty()) Wake();
}

bool StartEndpoint() {
  EnsureApartment();
  if (g_listen >= 0) return true;
  int fd = socket(AF_UNIX, SOCK_STREAM, 0);
  if (fd < 0) return false;
  NoInherit(fd);
  sockaddr_un sa = {};
  sa.sun_family = AF_UNIX;
  std::string path = EndpointPath(Self());
  snprintf(sa.sun_path, sizeof sa.sun_path, "%s", path.c_str());
  unlink(path.c_str());
  if (bind(fd, (sockaddr*)&sa, sizeof sa) != 0 || listen(fd, 16) != 0) {
    close(fd);
    return false;
  }
  g_listen = fd;
  atexit(Cleanup);
  std::thread(ListenLoop, fd).detach();
  return true;
}

// ---------------------------------------------------------------------
// The objects handed out

uint64_t ExportObject(IUnknown* identity, DWORD to) {  // apartment thread
  std::lock_guard<std::mutex> g(g_mu);
  uint64_t oid;
  auto it = g_exportOf.find(identity);
  if (it != g_exportOf.end()) {
    oid = it->second;
  } else {
    oid = g_nextOid++;
    g_exportOf[identity] = oid;
    g_exports[oid].unk = identity;
  }
  g_exports[oid].refs[to]++;
  identity->AddRef();
  return oid;
}

// Gives back references a program held; the objects are released outside
// the lock.
void ReleaseExport(uint64_t oid, DWORD from, LONG n, std::vector<IUnknown*>* release) {
  auto it = g_exports.find(oid);
  if (it == g_exports.end()) return;
  auto r = it->second.refs.find(from);
  if (r == it->second.refs.end()) return;
  if (n > r->second) n = r->second;
  r->second -= n;
  for (LONG i = 0; i < n; i++) release->push_back(it->second.unk);
  if (r->second == 0) it->second.refs.erase(r);
  if (it->second.refs.empty()) {
    g_exportOf.erase(it->second.unk);
    g_exports.erase(it);
  }
}

void PeerGone(DWORD peer) {
  std::vector<IUnknown*> release;
  {
    std::lock_guard<std::mutex> g(g_mu);
    if (Alive(peer) && g_out.count(peer)) return;  // only one of its connections closed
    std::vector<uint64_t> oids;
    for (auto& e : g_exports)
      if (e.second.refs.count(peer)) oids.push_back(e.first);
    for (uint64_t oid : oids) ReleaseExport(oid, peer, 0x7fffffff, &release);
  }
  for (IUnknown* u : release) u->Release();
}

IUnknown* FindExport(uint64_t oid) {
  std::lock_guard<std::mutex> g(g_mu);
  auto it = g_exports.find(oid);
  return it == g_exports.end() ? NULL : it->second.unk;
}

}  // namespace

// ---------------------------------------------------------------------
// Proxies

ProxyManager::ProxyManager(DWORD owner_, uint64_t oid_) : owner(owner_), oid(oid_) {}

ProxyManager::~ProxyManager() {
  for (auto& i : itfs) delete i.proxy;
}

HRESULT STDMETHODCALLTYPE ProxyManager::QueryInterface(REFIID riid, void** out) {
  if (out == NULL) return E_POINTER;
  *out = NULL;
  if (IsEqualIID(riid, IID_IUnknown)) {
    *out = static_cast<IUnknown*>(this);
    AddRef();
    return S_OK;
  }
  {
    std::lock_guard<std::mutex> g(lock);
    for (auto& i : itfs)
      if (IsEqualIID(i.iid, riid)) {
        *out = i.itf;
        AddRef();
        return S_OK;
      }
  }
  const ProxyInfo* info = FindProxyInfo(riid);
  if (info == NULL || info->make == NULL) return E_NOINTERFACE;
  Writer w;
  w.U64(oid);
  w.Guid(riid);
  std::vector<BYTE> reply;
  HRESULT hr = CallObject(owner, 0, IID_NULL, kRemQueryInterface, w, &reply);
  if (FAILED(hr)) return hr;
  if (hr != S_OK) return E_NOINTERFACE;
  return AddInterface(riid, out);
}

HRESULT ProxyManager::AddInterface(REFIID riid, void** out) {
  std::lock_guard<std::mutex> g(lock);
  for (auto& i : itfs)
    if (IsEqualIID(i.iid, riid)) {
      *out = i.itf;
      AddRef();
      return S_OK;
    }
  const ProxyInfo* info = FindProxyInfo(riid);
  if (info == NULL || info->make == NULL) return E_NOINTERFACE;
  Itf i;
  i.iid = riid;
  i.proxy = info->make(this, &i.itf);
  itfs.push_back(i);
  *out = i.itf;
  AddRef();
  return S_OK;
}

ULONG STDMETHODCALLTYPE ProxyManager::AddRef() { return (ULONG)++refs; }

ULONG STDMETHODCALLTYPE ProxyManager::Release() {
  LONG n;
  {
    std::lock_guard<std::mutex> g(g_mu);
    n = --refs;
    if (n != 0) return (ULONG)n;
    g_proxies.erase(std::make_pair(owner, oid));
  }
  // The references this program held go back.
  if (remoteRefs > 0) {
    if (auto c = Connect(owner)) {
      Writer m;
      m.U64(0);
      m.Guid(IID_NULL);
      m.U32(kRemRelease);
      m.U64(oid);
      m.U32((uint32_t)remoteRefs);
      SendMsg(c.get(), kOneWay, 0, m.b);
    }
  }
  delete this;
  return 0;
}

HRESULT ProxyManager::Invoke(REFIID iid, uint32_t method, const Writer& args, std::vector<BYTE>* reply) {
  return CallObject(owner, oid, iid, method, args, reply);
}

HRESULT CallObject(DWORD owner, uint64_t oid, REFIID iid, uint32_t method, const Writer& args,
                   std::vector<BYTE>* reply) {
  std::shared_ptr<Conn> c = Connect(owner);
  if (!c) return kDisconnected;
  Writer m;
  m.U64(oid);
  m.Guid(iid);
  m.U32(method);
  m.Put(args.b.data(), args.b.size());
  Pending p;
  p.conn = c.get();
  uint32_t id;
  {
    std::lock_guard<std::mutex> g(g_mu);
    if (c->dead) return kDisconnected;
    id = g_nextId++;
    g_pending[id] = &p;
  }
  if (!SendMsg(c.get(), kCall, id, m.b)) {
    std::lock_guard<std::mutex> g(g_mu);
    g_pending.erase(id);
    return kDisconnected;
  }
  bool apartment = GetCurrentThreadId() == g_aptTid;
  std::unique_lock<std::mutex> g(g_mu);
  while (!p.done) {
    if (apartment && !g_incoming.empty()) {
      g.unlock();
      Drain();
      g.lock();
      continue;
    }
    g_cv.wait_for(g, std::chrono::milliseconds(200));
  }
  g_pending.erase(id);
  g.unlock();
  if (p.body.size() < 4) return kDisconnected;
  Reader r(p.body);
  HRESULT hr = (HRESULT)r.U32();
  if (reply) reply->assign(p.body.begin() + 4, p.body.end());
  return hr;
}

// ---------------------------------------------------------------------
// Interface pointers on the wire: the owning process (0 for NULL), the
// object, the interface, and whether the reference holds one of the
// owner's references for the receiver.

void PutInterface(Writer& w, IUnknown* p, REFIID iid, DWORD to) {
  IUnknown* id = NULL;
  if (p == NULL || FAILED(p->QueryInterface(IID_IUnknown, (void**)&id)) || id == NULL) {
    w.U32(0);
    return;
  }
  if (IsProxy(id)) {
    ProxyManager* pm = static_cast<ProxyManager*>(id);
    w.U32(pm->owner);
    w.U64(pm->oid);
    w.Guid(iid);
    if (pm->owner == to) {
      w.U32(0);  // going home: the owner has it
    } else {
      // The owner holds a reference for the receiver as well.
      Writer a;
      a.U64(pm->oid);
      a.U32(1);
      a.U32(to);
      CallObject(pm->owner, 0, IID_NULL, kRemAddRef, a, NULL);
      w.U32(1);
    }
  } else {
    StartEndpoint();
    uint64_t oid = ExportObject(id, to);
    w.U32(Self());
    w.U64(oid);
    w.Guid(iid);
    w.U32(1);
  }
  id->Release();
}

HRESULT GetInterface(Reader& r, void** out) {
  *out = NULL;
  DWORD owner = r.U32();
  if (owner == 0) return r.bad ? kServerFault : S_OK;
  uint64_t oid = r.U64();
  GUID iid = r.Guid();
  uint32_t held = r.U32();
  if (r.bad) return kServerFault;
  if (owner == Self()) {
    IUnknown* unk = FindExport(oid);
    if (unk == NULL) return CO_E_OBJNOTCONNECTED;
    return unk->QueryInterface(iid, out);
  }
  ProxyManager* pm;
  {
    std::lock_guard<std::mutex> g(g_mu);
    auto it = g_proxies.find(std::make_pair(owner, oid));
    if (it != g_proxies.end()) {
      pm = it->second;
    } else {
      pm = new ProxyManager(owner, oid);
      g_proxies[std::make_pair(owner, oid)] = pm;
    }
    pm->refs++;  // kept while the interface is found
    pm->remoteRefs += (LONG)held;
  }
  HRESULT hr = pm->AddInterface(iid, out);
  pm->Release();
  return hr;
}

void PutInterfaceOut(Writer& w, HRESULT hr, IUnknown* p, REFIID iid, DWORD to) {
  PutInterface(w, SUCCEEDED(hr) ? p : NULL, iid, to);
  if (p) p->Release();
}

bool IsProxy(IUnknown* unk) {
  std::lock_guard<std::mutex> g(g_mu);
  for (auto& p : g_proxies)
    if (static_cast<IUnknown*>(p.second) == unk) return true;
  return false;
}

// ---------------------------------------------------------------------
// Running the calls

namespace {

HRESULT RunProcessMethod(uint32_t method, Reader& in, Writer& out, DWORD peer) {
  switch (method) {
    case kGetClassObject: {
      CLSID clsid = in.Guid();
      IID iid = in.Guid();
      IUnknown* unk = NULL;
      bool last = false;
      HRESULT hr = TakeClassObject(clsid, &unk, &last);
      if (last) UnpublishClass(clsid);
      void* itf = NULL;
      if (SUCCEEDED(hr)) {
        hr = unk->QueryInterface(iid, &itf);
        unk->Release();
      }
      PutInterfaceOut(out, hr, (IUnknown*)itf, iid, peer);
      return hr;
    }
    case kRemRelease: {
      uint64_t oid = in.U64();
      LONG n = (LONG)in.U32();
      std::vector<IUnknown*> release;
      {
        std::lock_guard<std::mutex> g(g_mu);
        ReleaseExport(oid, peer, n, &release);
      }
      for (IUnknown* u : release) u->Release();
      return S_OK;
    }
    case kRemAddRef: {
      uint64_t oid = in.U64();
      LONG n = (LONG)in.U32();
      DWORD forPid = in.U32();
      std::lock_guard<std::mutex> g(g_mu);
      auto it = g_exports.find(oid);
      if (it == g_exports.end()) return CO_E_OBJNOTCONNECTED;
      it->second.refs[forPid] += n;
      for (LONG i = 0; i < n; i++) it->second.unk->AddRef();
      return S_OK;
    }
    case kRemQueryInterface: {
      uint64_t oid = in.U64();
      IID iid = in.Guid();
      IUnknown* unk = FindExport(oid);
      if (unk == NULL) return CO_E_OBJNOTCONNECTED;
      void* p = NULL;
      HRESULT hr = unk->QueryInterface(iid, &p);
      if (SUCCEEDED(hr) && p) ((IUnknown*)p)->Release();
      return hr == S_OK ? S_OK : S_FALSE;
    }
  }
  return E_NOTIMPL;
}

void Execute(Incoming& in) {
  if (in.kind == kGone) {
    PeerGone(in.peer);
    return;
  }
  Reader r(in.body);
  uint64_t oid = r.U64();
  IID iid = r.Guid();
  uint32_t method = r.U32();
  Writer out;
  HRESULT hr;
  if (r.bad) {
    hr = kServerFault;
  } else if (oid == 0) {
    hr = RunProcessMethod(method, r, out, in.peer);
  } else {
    IUnknown* unk = FindExport(oid);
    const ProxyInfo* info = FindProxyInfo(iid);
    if (unk == NULL) {
      hr = CO_E_OBJNOTCONNECTED;
    } else if (info == NULL || info->stub == NULL) {
      hr = E_NOINTERFACE;
    } else {
      unk->AddRef();  // through the call, whatever it releases
      void* itf = NULL;
      hr = unk->QueryInterface(iid, &itf);
      if (SUCCEEDED(hr)) {
        StubCall call = {(IUnknown*)itf, method, &r, &out, in.peer};
        hr = info->stub(call);
        ((IUnknown*)itf)->Release();
      }
      unk->Release();
    }
  }
  if (in.kind == kCall && in.conn) {
    Writer reply;
    reply.U32((uint32_t)hr);
    reply.Put(out.b.data(), out.b.size());
    SendMsg(in.conn.get(), kReply, in.id, reply.b);
  }
}

}  // namespace

void Drain() {
  if (g_aptTid != 0 && GetCurrentThreadId() != g_aptTid) return;
  for (;;) {
    Incoming in;
    {
      std::lock_guard<std::mutex> g(g_mu);
      if (g_incoming.empty()) return;
      in = std::move(g_incoming.front());
      g_incoming.pop_front();
    }
    Execute(in);
  }
}

// ---------------------------------------------------------------------
// Classes

void PublishClass(REFCLSID clsid) {
  if (!StartEndpoint()) return;
  std::string path = ClassPath(clsid);
  std::string tmp = path + ".tmp" + std::to_string(Self());
  FILE* f = fopen(tmp.c_str(), "w");
  if (f == NULL) return;
  fprintf(f, "%lu\n", (unsigned long)Self());
  fclose(f);
  rename(tmp.c_str(), path.c_str());
  std::lock_guard<std::mutex> g(g_mu);
  for (const GUID& p : g_published)
    if (IsEqualGUID(p, clsid)) return;
  g_published.push_back(clsid);
}

void UnpublishClass(REFCLSID clsid) {
  if (ReadClassFile(clsid) == Self()) unlink(ClassPath(clsid).c_str());
}

namespace {

HRESULT Activate(DWORD pid, REFCLSID clsid, REFIID riid, void** out) {
  Writer w;
  w.Guid(clsid);
  w.Guid(riid);
  std::vector<BYTE> reply;
  HRESULT hr = CallObject(pid, 0, IID_NULL, kGetClassObject, w, &reply);
  Reader r(reply);
  void* p = NULL;
  HRESULT hr2 = GetInterface(r, &p);
  if (FAILED(hr) || FAILED(hr2) || p == NULL) {
    if (p) ((IUnknown*)p)->Release();
    return FAILED(hr) ? hr : FAILED(hr2) ? hr2 : E_NOINTERFACE;
  }
  *out = p;
  return S_OK;
}

}  // namespace

HRESULT GetRemoteClassObject(REFCLSID clsid, REFIID riid, void** out) {
  *out = NULL;
  DWORD pid = ReadClassFile(clsid);
  if (pid == Self()) return REGDB_E_CLASSNOTREG;  // taken already, or revoked
  if (Alive(pid) && SUCCEEDED(Activate(pid, clsid, riid, out))) return S_OK;
  if (pid != 0 && !Alive(pid)) unlink(ClassPath(clsid).c_str());

  WCHAR guid[40], cmd[1024];
  StringFromGUID2(clsid, guid, 40);
  std::wstring key = std::wstring(L"CLSID\\") + guid + L"\\LocalServer32";
  LONG cb = sizeof(cmd);
  if (RegQueryValueW(HKEY_CLASSES_ROOT, key.c_str(), cmd, &cb) != ERROR_SUCCESS || !cmd[0])
    return REGDB_E_CLASSNOTREG;
  std::wstring line = std::wstring(cmd) + L" -Embedding";
  STARTUPINFOW si = {};
  si.cb = sizeof si;
  PROCESS_INFORMATION pi;
  if (!CreateProcessW(NULL, (LPWSTR)line.c_str(), NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi))
    return CO_E_SERVER_EXEC_FAILURE;
  CloseHandle(pi.hThread);
  HRESULT hr = CO_E_SERVER_EXEC_FAILURE;
  for (int i = 0; i < 1500; i++) {  // 30 seconds
    DWORD now = ReadClassFile(clsid);
    if (now != 0 && now != pid && now != Self() && Alive(now)) {
      hr = Activate(now, clsid, riid, out);
      if (SUCCEEDED(hr)) break;
    }
    if (WaitForSingleObject(pi.hProcess, 20) == WAIT_OBJECT_0) break;  // it ended
  }
  CloseHandle(pi.hProcess);
  return hr;
}

void DisconnectObject(IUnknown* unk) {
  IUnknown* id = NULL;
  if (unk == NULL || FAILED(unk->QueryInterface(IID_IUnknown, (void**)&id)) || id == NULL) return;
  std::vector<IUnknown*> release;
  {
    std::lock_guard<std::mutex> g(g_mu);
    auto it = g_exportOf.find(id);
    if (it != g_exportOf.end()) {
      uint64_t oid = it->second;
      std::vector<DWORD> peers;
      for (auto& r : g_exports[oid].refs) peers.push_back(r.first);
      for (DWORD p : peers) ReleaseExport(oid, p, 0x7fffffff, &release);
    }
  }
  id->Release();
  for (IUnknown* u : release) u->Release();
}

}  // namespace com
}  // namespace winport
