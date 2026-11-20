// service.cpp - services: the service control manager's API and a
// service's own (the dispatcher, its control handler and status), and the
// event log.
//
// A service is configured where Windows keeps it, under
// HKLM\SYSTEM\CurrentControlSet\Services, in the shared registry.
// StartService runs its program as a process of its own, telling it which
// service it is in the environment. That process's dispatcher listens on
// a local socket for the controls ControlService sends, and keeps the
// service's status in a file beside the socket, which QueryServiceStatus
// reads; a status left behind by a process that has gone reads as
// stopped.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include "winport.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <syslog.h>
#include <unistd.h>
#include <wctype.h>
#include <string>
#include <vector>

extern char** environ;

using namespace winport;

namespace {

const wchar_t kServicesKey[] = L"SYSTEM\\CurrentControlSet\\Services";
const char kServiceEnv[] = "WINPORT_SERVICE";
const char kArgsEnv[] = "WINPORT_SERVICE_ARGS";

// A handle: the manager, or a service by name.
struct ScHandle {
  static const DWORD kMagic = 0x53635363;  // "cScS"
  DWORD magic = kMagic;
  bool manager;
  std::wstring name;
};

ScHandle* GetSc(SC_HANDLE h, bool manager) {
  ScHandle* s = (ScHandle*)h;
  if (s == NULL || s->magic != ScHandle::kMagic || s->manager != manager) {
    SetLastError(ERROR_INVALID_HANDLE);
    return NULL;
  }
  return s;
}

std::wstring Lower(const std::wstring& s) {
  std::wstring r = s;
  for (wchar_t& c : r) c = (wchar_t)towlower(c);
  return r;
}

// Where a service's socket and status live: a directory of the user's
// own, with a short name, as socket paths are short.
std::string RuntimeBase(const std::wstring& name) {
  const char* run = getenv("XDG_RUNTIME_DIR");
  std::string dir = run && *run ? std::string(run) : std::string("/tmp");
  dir += "/winport-svc-" + std::to_string((unsigned)getuid());
  mkdir(dir.c_str(), 0700);
  unsigned long long h = 1469598103934665603ULL;
  for (wchar_t c : Lower(name)) {
    h ^= (unsigned)c;
    h *= 1099511628211ULL;
  }
  char buf[24];
  snprintf(buf, sizeof buf, "/%016llx", h);
  return dir + buf;
}

// The status file: the status, and the process that wrote it.
struct StatusRecord {
  SERVICE_STATUS status;
  DWORD pid;
};

void WriteStatus(const std::wstring& name, const SERVICE_STATUS& st, DWORD pid) {
  std::string path = RuntimeBase(name) + ".status";
  std::string tmp = path + ".tmp" + std::to_string((unsigned)getpid());
  StatusRecord r = {st, pid};
  int fd = open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
  if (fd < 0) return;
  bool ok = write(fd, &r, sizeof r) == (ssize_t)sizeof r;
  close(fd);
  if (!ok || rename(tmp.c_str(), path.c_str()) != 0) unlink(tmp.c_str());
}

bool Alive(DWORD pid) { return pid != 0 && (kill((pid_t)pid, 0) == 0 || errno == EPERM); }

// The status, or stopped when there is none or its process is gone.
SERVICE_STATUS ReadStatus(const std::wstring& name, DWORD type, DWORD* pid) {
  StatusRecord r;
  memset(&r, 0, sizeof r);
  std::string path = RuntimeBase(name) + ".status";
  int fd = open(path.c_str(), O_RDONLY | O_CLOEXEC);
  bool got = false;
  if (fd >= 0) {
    got = read(fd, &r, sizeof r) == (ssize_t)sizeof r;
    close(fd);
  }
  if (!got || (r.status.dwCurrentState != SERVICE_STOPPED && !Alive(r.pid))) {
    DWORD exitCode = got && r.status.dwCurrentState != SERVICE_STOPPED ? ERROR_PROCESS_ABORTED : r.status.dwWin32ExitCode;
    memset(&r, 0, sizeof r);
    r.status.dwServiceType = type;
    r.status.dwCurrentState = SERVICE_STOPPED;
    r.status.dwWin32ExitCode = got ? exitCode : 0;
  }
  if (pid) *pid = r.status.dwCurrentState == SERVICE_STOPPED ? 0 : r.pid;
  return r.status;
}

// The service's registry key, opened.
HKEY OpenServiceKey(const std::wstring& name, bool write) {
  HKEY services, key = NULL;
  if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, kServicesKey, 0, KEY_READ, &services) != ERROR_SUCCESS) return NULL;
  if (RegOpenKeyExW(services, name.c_str(), 0, write ? KEY_ALL_ACCESS : KEY_READ, &key) != ERROR_SUCCESS) key = NULL;
  RegCloseKey(services);
  return key;
}

DWORD GetDword(HKEY k, const wchar_t* name, DWORD def) {
  DWORD v = 0, cb = sizeof v, type = 0;
  if (RegQueryValueExW(k, name, NULL, &type, (LPBYTE)&v, &cb) != ERROR_SUCCESS || type != REG_DWORD) return def;
  return v;
}
// A string value; a multi-string keeps its zeros and gets a final one.
std::wstring GetString(HKEY k, const wchar_t* name) {
  DWORD cb = 0, type = 0;
  if (RegQueryValueExW(k, name, NULL, &type, NULL, &cb) != ERROR_SUCCESS) return std::wstring();
  std::vector<wchar_t> buf(cb / sizeof(wchar_t) + 2, 0);
  if (RegQueryValueExW(k, name, NULL, &type, (LPBYTE)buf.data(), &cb) != ERROR_SUCCESS) return std::wstring();
  size_t n = cb / sizeof(wchar_t);
  if (type == REG_MULTI_SZ) {
    while (n > 0 && buf[n - 1] == 0) n--;
    return std::wstring(buf.data(), n) + L'\0';
  }
  while (n > 0 && buf[n - 1] == 0) n--;
  return std::wstring(buf.data(), n);
}
void SetDword(HKEY k, const wchar_t* name, DWORD v) { RegSetValueExW(k, name, 0, REG_DWORD, (const BYTE*)&v, sizeof v); }
void SetString(HKEY k, const wchar_t* name, const std::wstring& v, DWORD type = REG_SZ) {
  RegSetValueExW(k, name, 0, type, (const BYTE*)v.c_str(), (DWORD)((v.size() + 1) * sizeof(wchar_t)));
}
// A list of strings each ending in a zero, then a zero, as one string.
std::wstring MultiString(const wchar_t* p) {
  std::wstring r;
  if (p == NULL) return r;
  while (*p) {
    size_t n = wcslen(p);
    r.append(p, n + 1);
    p += n + 1;
  }
  return r;
}

struct Config {
  DWORD type, start, errorControl;
  std::wstring path, group, dependencies, startName, displayName;
};

bool ReadConfig(const std::wstring& name, Config* c) {
  HKEY k = OpenServiceKey(name, false);
  if (k == NULL) {
    SetLastError(ERROR_SERVICE_DOES_NOT_EXIST);
    return false;
  }
  c->type = GetDword(k, L"Type", SERVICE_WIN32_OWN_PROCESS);
  c->start = GetDword(k, L"Start", SERVICE_DEMAND_START);
  c->errorControl = GetDword(k, L"ErrorControl", SERVICE_ERROR_NORMAL);
  c->path = GetString(k, L"ImagePath");
  c->group = GetString(k, L"Group");
  c->dependencies = GetString(k, L"DependOnService");
  c->startName = GetString(k, L"ObjectName");
  if (c->startName.empty()) c->startName = L"LocalSystem";
  c->displayName = GetString(k, L"DisplayName");
  if (c->displayName.empty()) c->displayName = name;
  RegCloseKey(k);
  return true;
}

SC_HANDLE NewSc(bool manager, const std::wstring& name) {
  ScHandle* s = new ScHandle;
  s->manager = manager;
  s->name = name;
  return (SC_HANDLE)s;
}

std::wstring W(LPCSTR s) { return s ? Widen(s) : std::wstring(); }
std::wstring W(LPCWSTR s) { return s ? std::wstring(s) : std::wstring(); }
// A multi-string from ANSI.
std::wstring MultiW(LPCSTR p) {
  std::wstring r;
  if (p == NULL) return r;
  while (*p) {
    size_t n = strlen(p);
    r += Widen(p, (int)n);
    r += L'\0';
    p += n + 1;
  }
  return r;
}

// Packs the strings of a configuration after the structure, in wide or
// ANSI characters.
template <class Q, class C>
BOOL PackConfig(const Config& c, Q* q, DWORD cb, LPDWORD needed) {
  std::basic_string<C> s[5];
  const std::wstring* src[5] = {&c.path, &c.group, &c.dependencies, &c.startName, &c.displayName};
  for (int i = 0; i < 5; i++) {
    if (sizeof(C) == sizeof(wchar_t)) {
      s[i].assign((const C*)src[i]->c_str(), src[i]->size());
    } else {
      // Each piece of a multi-string on its own, keeping the zeros.
      std::string a;
      const std::wstring& w = *src[i];
      size_t start = 0;
      while (start < w.size()) {
        size_t z = w.find(L'\0', start);
        if (z == std::wstring::npos) z = w.size();
        a += Narrow(w.c_str() + start, (int)(z - start));
        if (z < w.size()) a += '\0';
        start = z + 1;
      }
      s[i].assign((const C*)a.c_str(), a.size());
    }
  }
  if (s[2].empty() || s[2].back() != 0) s[2] += (C)0;  // the list's final zero
  size_t total = sizeof(Q);
  for (auto& x : s) total += (x.size() + 1) * sizeof(C);
  if (needed) *needed = (DWORD)total;
  if (q == NULL || cb < total) {
    SetLastError(ERROR_INSUFFICIENT_BUFFER);
    return FALSE;
  }
  q->dwServiceType = c.type;
  q->dwStartType = c.start;
  q->dwErrorControl = c.errorControl;
  q->dwTagId = 0;
  C* p = (C*)(q + 1);
  C** dst[5] = {(C**)&q->lpBinaryPathName, (C**)&q->lpLoadOrderGroup, (C**)&q->lpDependencies,
                (C**)&q->lpServiceStartName, (C**)&q->lpDisplayName};
  for (int i = 0; i < 5; i++) {
    *dst[i] = p;
    memcpy(p, s[i].data(), s[i].size() * sizeof(C));
    p += s[i].size();
    *p++ = 0;
  }
  return TRUE;
}

// Sends a control to the service's dispatcher; the reply is the error.
DWORD SendControl(const std::wstring& name, DWORD code) {
  std::string path = RuntimeBase(name) + ".sock";
  int fd = socket(AF_UNIX, SOCK_STREAM, 0);
  if (fd < 0) return ERROR_SERVICE_NOT_ACTIVE;
  fcntl(fd, F_SETFD, FD_CLOEXEC);
  struct sockaddr_un a;
  memset(&a, 0, sizeof a);
  a.sun_family = AF_UNIX;
  strncpy(a.sun_path, path.c_str(), sizeof a.sun_path - 1);
  if (connect(fd, (struct sockaddr*)&a, sizeof a) != 0) {
    close(fd);
    return ERROR_SERVICE_NOT_ACTIVE;
  }
  DWORD reply = ERROR_SERVICE_REQUEST_TIMEOUT;
  if (write(fd, &code, sizeof code) == (ssize_t)sizeof code) {
    // As on Windows, a handler has 30 seconds.
    struct pollfd p = {fd, POLLIN, 0};
    if (poll(&p, 1, 30000) == 1 && read(fd, &reply, sizeof reply) != (ssize_t)sizeof reply)
      reply = ERROR_SERVICE_NOT_ACTIVE;
  }
  close(fd);
  return reply;
}

// ---------------------------------------------------------------------
// The service's own process.

struct Running {
  std::wstring name;
  LPHANDLER_FUNCTION handler = NULL;
  LPHANDLER_FUNCTION_EX handlerEx = NULL;
  LPVOID context = NULL;
  SERVICE_STATUS status;
  bool registered = false;
  bool mainDone = false;
  LPSERVICE_MAIN_FUNCTIONW mainW = NULL;
  LPSERVICE_MAIN_FUNCTIONA mainA = NULL;
  std::vector<std::wstring> args;
};
Running* g_running;  // while the dispatcher runs

DWORD WINAPI ServiceThread(LPVOID) {
  Running* r = g_running;
  if (r->mainW) {
    std::vector<LPWSTR> argv;
    for (auto& a : r->args) argv.push_back((LPWSTR)a.c_str());
    argv.push_back(NULL);
    r->mainW((DWORD)r->args.size(), argv.data());
  } else {
    std::vector<std::string> narrow;
    for (auto& a : r->args) narrow.push_back(Narrow(a.c_str()));
    std::vector<LPSTR> argv;
    for (auto& a : narrow) argv.push_back((LPSTR)a.c_str());
    argv.push_back(NULL);
    r->mainA((DWORD)narrow.size(), argv.data());
  }
  pthread_mutex_lock(WaitLock());
  r->mainDone = true;
  pthread_mutex_unlock(WaitLock());
  return 0;
}

// Which control a status accepts.
bool Accepts(const SERVICE_STATUS& st, DWORD code) {
  switch (code) {
  case SERVICE_CONTROL_STOP: return (st.dwControlsAccepted & SERVICE_ACCEPT_STOP) != 0;
  case SERVICE_CONTROL_PAUSE:
  case SERVICE_CONTROL_CONTINUE: return (st.dwControlsAccepted & SERVICE_ACCEPT_PAUSE_CONTINUE) != 0;
  case SERVICE_CONTROL_SHUTDOWN: return (st.dwControlsAccepted & SERVICE_ACCEPT_SHUTDOWN) != 0;
  case SERVICE_CONTROL_PARAMCHANGE: return (st.dwControlsAccepted & SERVICE_ACCEPT_PARAMCHANGE) != 0;
  case SERVICE_CONTROL_INTERROGATE: return true;
  default: return code >= 128 && code <= 255;  // the service's own
  }
}

DWORD Dispatch(const std::wstring& name, LPSERVICE_MAIN_FUNCTIONW mainW, LPSERVICE_MAIN_FUNCTIONA mainA) {
  std::string path = RuntimeBase(name) + ".sock";
  int lfd = socket(AF_UNIX, SOCK_STREAM, 0);
  if (lfd < 0) return ERROR_FAILED_SERVICE_CONTROLLER_CONNECT;
  fcntl(lfd, F_SETFD, FD_CLOEXEC);
  struct sockaddr_un a;
  memset(&a, 0, sizeof a);
  a.sun_family = AF_UNIX;
  strncpy(a.sun_path, path.c_str(), sizeof a.sun_path - 1);
  unlink(path.c_str());
  if (bind(lfd, (struct sockaddr*)&a, sizeof a) != 0 || listen(lfd, 8) != 0) {
    close(lfd);
    return ERROR_FAILED_SERVICE_CONTROLLER_CONNECT;
  }
  Running* r = new Running;
  r->name = name;
  r->mainW = mainW;
  r->mainA = mainA;
  memset(&r->status, 0, sizeof r->status);
  r->status.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
  r->status.dwCurrentState = SERVICE_START_PENDING;
  r->status.dwWaitHint = 2000;
  // argv[0] is the service's name; the rest are StartService's.
  r->args.push_back(name);
  const char* packed = getenv(kArgsEnv);
  if (packed && *packed) {
    std::string all = packed, cur;
    for (size_t i = 0; i <= all.size(); i++) {
      if (i == all.size() || all[i] == '\x1f') {
        r->args.push_back(Widen(cur.c_str()));
        cur.clear();
      } else {
        cur += all[i];
      }
    }
  }
  g_running = r;
  WriteStatus(name, r->status, (DWORD)getpid());
  HANDLE t = CreateThread(NULL, 0, ServiceThread, NULL, 0, NULL);
  if (t == NULL) {
    close(lfd);
    unlink(path.c_str());
    return GetLastError();
  }
  // Controls until the service has stopped and its main has returned (or
  // a moment after it stopped, when its main goes on).
  int afterStop = 0;
  for (;;) {
    pthread_mutex_lock(WaitLock());
    bool stopped = r->status.dwCurrentState == SERVICE_STOPPED;
    bool done = r->mainDone;
    pthread_mutex_unlock(WaitLock());
    if (done && !r->registered) {
      // Its main returned without ever registering a handler.
      r->status.dwCurrentState = SERVICE_STOPPED;
      WriteStatus(name, r->status, (DWORD)getpid());
      break;
    }
    if (stopped && (done || ++afterStop > 20)) break;
    struct pollfd p = {lfd, POLLIN, 0};
    if (poll(&p, 1, 50) != 1) continue;
    int cfd = accept(lfd, NULL, NULL);
    if (cfd < 0) continue;
    fcntl(cfd, F_SETFD, FD_CLOEXEC);
    DWORD code = 0, reply;
    if (read(cfd, &code, sizeof code) != (ssize_t)sizeof code) {
      close(cfd);
      continue;
    }
    pthread_mutex_lock(WaitLock());
    SERVICE_STATUS st = r->status;
    pthread_mutex_unlock(WaitLock());
    if (!r->registered || st.dwCurrentState == SERVICE_STOPPED) {
      reply = ERROR_SERVICE_NOT_ACTIVE;
    } else if (!Accepts(st, code)) {
      reply = ERROR_INVALID_SERVICE_CONTROL;
    } else if (r->handlerEx) {
      reply = r->handlerEx(code, 0, NULL, r->context);
      if (code == SERVICE_CONTROL_INTERROGATE) reply = 0;
    } else {
      r->handler(code);
      reply = 0;
    }
    if (write(cfd, &reply, sizeof reply) < 0) {
    }
    close(cfd);
  }
  WaitForSingleObject(t, 1000);
  CloseHandle(t);
  close(lfd);
  unlink(path.c_str());
  g_running = NULL;
  return 0;
}

std::wstring DispatcherName() {
  const char* n = getenv(kServiceEnv);
  return n && *n ? Widen(n) : std::wstring();
}

SERVICE_STATUS_HANDLE Register(const std::wstring& name, LPHANDLER_FUNCTION h, LPHANDLER_FUNCTION_EX hx, LPVOID ctx) {
  Running* r = g_running;
  // A service of its own process takes whatever name it is given.
  if (r == NULL || (h == NULL && hx == NULL)) {
    SetLastError(r == NULL ? ERROR_SERVICE_NOT_IN_EXE : ERROR_INVALID_PARAMETER);
    return NULL;
  }
  (void)name;
  r->handler = h;
  r->handlerEx = hx;
  r->context = ctx;
  r->registered = true;
  return (SERVICE_STATUS_HANDLE)r;
}

// The event log's sources.
struct EventSource {
  static const DWORD kMagic = 0x45764c67;  // "gLvE"
  DWORD magic = kMagic;
  std::string name;
};

BOOL Report(HANDLE h, WORD type, DWORD id, const std::vector<std::string>& strings) {
  EventSource* s = (EventSource*)h;
  if (s == NULL || s->magic != EventSource::kMagic) {
    SetLastError(ERROR_INVALID_HANDLE);
    return FALSE;
  }
  int prio = type == EVENTLOG_ERROR_TYPE || type == EVENTLOG_AUDIT_FAILURE ? LOG_ERR
             : type == EVENTLOG_WARNING_TYPE                              ? LOG_WARNING
                                                                          : LOG_INFO;
  std::string msg;
  for (auto& x : strings) {
    if (!msg.empty()) msg += ' ';
    msg += x;
  }
  if (msg.empty()) msg = "event " + std::to_string((unsigned)(id & 0xFFFF));
  syslog(LOG_USER | prio, "%s: %s", s->name.c_str(), msg.c_str());
  return TRUE;
}

// The services of the types and states asked for, from *resume on.
template <class E, class C>
BOOL Enum(SC_HANDLE scm, DWORD types, DWORD states, E* out, DWORD cb, LPDWORD needed, LPDWORD returned,
                 LPDWORD resume) {
  if (GetSc(scm, true) == NULL) return FALSE;
  if (needed == NULL || returned == NULL) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  struct Item {
    std::basic_string<C> name, display;
    SERVICE_STATUS status;
  };
  std::vector<Item> items;
  HKEY services;
  if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, kServicesKey, 0, KEY_READ, &services) == ERROR_SUCCESS) {
    WCHAR name[256];
    for (DWORD i = 0; RegEnumKeyW(services, i, name, 256) == ERROR_SUCCESS; i++) {
      Config c;
      if (!ReadConfig(name, &c) || !(c.type & types)) continue;
      SERVICE_STATUS st = ReadStatus(name, c.type, NULL);
      bool active = st.dwCurrentState != SERVICE_STOPPED;
      if (!((states & SERVICE_ACTIVE) && active) && !((states & SERVICE_INACTIVE) && !active)) continue;
      Item it;
      if (sizeof(C) == sizeof(wchar_t)) {
        it.name.assign((const C*)name, wcslen(name));
        it.display.assign((const C*)c.displayName.c_str(), c.displayName.size());
      } else {
        std::string n = Narrow(name), d = Narrow(c.displayName.c_str());
        it.name.assign((const C*)n.c_str(), n.size());
        it.display.assign((const C*)d.c_str(), d.size());
      }
      it.status = st;
      items.push_back(it);
    }
    RegCloseKey(services);
  }
  DWORD start = resume ? *resume : 0;
  // As many as fit, the entries first and their strings packed at the end.
  DWORD used = 0, n = 0, i = start;
  for (; i < items.size(); i++) {
    DWORD size = (DWORD)(sizeof(E) + (items[i].name.size() + items[i].display.size() + 2) * sizeof(C));
    if (used + size > cb || out == NULL) break;
    used += size;
    n++;
  }
  DWORD rest = 0;
  for (DWORD j = i; j < items.size(); j++)
    rest += (DWORD)(sizeof(E) + (items[j].name.size() + items[j].display.size() + 2) * sizeof(C));
  C* strings = (C*)((BYTE*)out + cb);
  for (DWORD j = 0; j < n; j++) {
    Item& it = items[start + j];
    strings -= it.display.size() + 1;
    memcpy(strings, it.display.c_str(), (it.display.size() + 1) * sizeof(C));
    out[j].lpDisplayName = (decltype(out[j].lpDisplayName))strings;
    strings -= it.name.size() + 1;
    memcpy(strings, it.name.c_str(), (it.name.size() + 1) * sizeof(C));
    out[j].lpServiceName = (decltype(out[j].lpServiceName))strings;
    out[j].ServiceStatus = it.status;
  }
  *returned = n;
  *needed = rest;
  if (i < items.size()) {
    if (resume) *resume = i;
    SetLastError(ERROR_MORE_DATA);
    return FALSE;
  }
  if (resume) *resume = 0;
  return TRUE;
}
}  // namespace

extern "C" {

SC_HANDLE WINAPI OpenSCManagerW(LPCWSTR machine, LPCWSTR database, DWORD) {
  if (machine && *machine) {
    SetLastError(1722);  // RPC_S_SERVER_UNAVAILABLE: no other machines
    return NULL;
  }
  if (database && *database && wcscmp(database, SERVICES_ACTIVE_DATABASEW) != 0) {
    SetLastError(ERROR_INVALID_NAME);
    return NULL;
  }
  return NewSc(true, L"");
}
SC_HANDLE WINAPI OpenSCManagerA(LPCSTR machine, LPCSTR database, DWORD access) {
  std::wstring m = W(machine), d = W(database);
  return OpenSCManagerW(machine ? m.c_str() : NULL, database ? d.c_str() : NULL, access);
}

SC_HANDLE WINAPI OpenServiceW(SC_HANDLE scm, LPCWSTR name, DWORD) {
  if (GetSc(scm, true) == NULL) return NULL;
  if (name == NULL || !*name) {
    SetLastError(ERROR_INVALID_NAME);
    return NULL;
  }
  HKEY k = OpenServiceKey(name, false);
  if (k == NULL) {
    SetLastError(ERROR_SERVICE_DOES_NOT_EXIST);
    return NULL;
  }
  RegCloseKey(k);
  return NewSc(false, name);
}
SC_HANDLE WINAPI OpenServiceA(SC_HANDLE scm, LPCSTR name, DWORD access) {
  std::wstring w = W(name);
  return OpenServiceW(scm, name ? w.c_str() : NULL, access);
}

static SC_HANDLE Create(SC_HANDLE scm, const std::wstring& name, const std::wstring& display, DWORD type, DWORD start,
                        DWORD errorControl, const std::wstring& path, const std::wstring& group,
                        const std::wstring& deps, const std::wstring& startName) {
  if (GetSc(scm, true) == NULL) return NULL;
  if (name.empty() || name.find_first_of(L"/\\") != std::wstring::npos) {
    SetLastError(ERROR_INVALID_NAME);
    return NULL;
  }
  if (path.empty()) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return NULL;
  }
  HKEY existing = OpenServiceKey(name, false);
  if (existing != NULL) {
    RegCloseKey(existing);
    SetLastError(ERROR_SERVICE_EXISTS);
    return NULL;
  }
  HKEY services, k;
  if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, kServicesKey, 0, NULL, 0, KEY_ALL_ACCESS, NULL, &services, NULL) !=
      ERROR_SUCCESS)
    return NULL;
  LONG l = RegCreateKeyExW(services, name.c_str(), 0, NULL, 0, KEY_ALL_ACCESS, NULL, &k, NULL);
  RegCloseKey(services);
  if (l != ERROR_SUCCESS) {
    SetLastError((DWORD)l);
    return NULL;
  }
  SetDword(k, L"Type", type);
  SetDword(k, L"Start", start);
  SetDword(k, L"ErrorControl", errorControl);
  SetString(k, L"ImagePath", path, REG_EXPAND_SZ);
  if (!display.empty()) SetString(k, L"DisplayName", display);
  if (!group.empty()) SetString(k, L"Group", group);
  if (!deps.empty()) {
    std::wstring d = deps + L'\0';
    RegSetValueExW(k, L"DependOnService", 0, REG_MULTI_SZ, (const BYTE*)d.data(),
                   (DWORD)(d.size() * sizeof(wchar_t)));
  }
  SetString(k, L"ObjectName", startName.empty() ? std::wstring(L"LocalSystem") : startName);
  RegCloseKey(k);
  SetLastError(0);
  return NewSc(false, name);
}

SC_HANDLE WINAPI CreateServiceW(SC_HANDLE scm, LPCWSTR name, LPCWSTR display, DWORD, DWORD type, DWORD start,
                                DWORD errorControl, LPCWSTR path, LPCWSTR group, LPDWORD tag, LPCWSTR deps,
                                LPCWSTR startName, LPCWSTR) {
  if (tag) *tag = 0;
  return Create(scm, W(name), W(display), type, start, errorControl, W(path), W(group), MultiString(deps),
                W(startName));
}
SC_HANDLE WINAPI CreateServiceA(SC_HANDLE scm, LPCSTR name, LPCSTR display, DWORD, DWORD type, DWORD start,
                                DWORD errorControl, LPCSTR path, LPCSTR group, LPDWORD tag, LPCSTR deps,
                                LPCSTR startName, LPCSTR) {
  if (tag) *tag = 0;
  return Create(scm, W(name), W(display), type, start, errorControl, W(path), W(group), MultiW(deps), W(startName));
}

static BOOL Change(SC_HANDLE h, DWORD type, DWORD start, DWORD errorControl, const std::wstring* path,
                   const std::wstring* group, const std::wstring* deps, const std::wstring* startName,
                   const std::wstring* display) {
  ScHandle* s = GetSc(h, false);
  if (s == NULL) return FALSE;
  HKEY k = OpenServiceKey(s->name, true);
  if (k == NULL) {
    SetLastError(ERROR_SERVICE_MARKED_FOR_DELETE);
    return FALSE;
  }
  if (type != SERVICE_NO_CHANGE) SetDword(k, L"Type", type);
  if (start != SERVICE_NO_CHANGE) SetDword(k, L"Start", start);
  if (errorControl != SERVICE_NO_CHANGE) SetDword(k, L"ErrorControl", errorControl);
  if (path) SetString(k, L"ImagePath", *path, REG_EXPAND_SZ);
  if (group) SetString(k, L"Group", *group);
  if (deps) {
    std::wstring d = *deps + L'\0';
    RegSetValueExW(k, L"DependOnService", 0, REG_MULTI_SZ, (const BYTE*)d.data(),
                   (DWORD)(d.size() * sizeof(wchar_t)));
  }
  if (startName) SetString(k, L"ObjectName", *startName);
  if (display) SetString(k, L"DisplayName", *display);
  RegCloseKey(k);
  return TRUE;
}

BOOL WINAPI ChangeServiceConfigW(SC_HANDLE h, DWORD type, DWORD start, DWORD errorControl, LPCWSTR path,
                                 LPCWSTR group, LPDWORD tag, LPCWSTR deps, LPCWSTR startName, LPCWSTR,
                                 LPCWSTR display) {
  if (tag) *tag = 0;
  std::wstring p = W(path), g = W(group), d = MultiString(deps), n = W(startName), dn = W(display);
  return Change(h, type, start, errorControl, path ? &p : NULL, group ? &g : NULL, deps ? &d : NULL,
                startName ? &n : NULL, display ? &dn : NULL);
}
BOOL WINAPI ChangeServiceConfigA(SC_HANDLE h, DWORD type, DWORD start, DWORD errorControl, LPCSTR path, LPCSTR group,
                                 LPDWORD tag, LPCSTR deps, LPCSTR startName, LPCSTR, LPCSTR display) {
  if (tag) *tag = 0;
  std::wstring p = W(path), g = W(group), d = MultiW(deps), n = W(startName), dn = W(display);
  return Change(h, type, start, errorControl, path ? &p : NULL, group ? &g : NULL, deps ? &d : NULL,
                startName ? &n : NULL, display ? &dn : NULL);
}

BOOL WINAPI ChangeServiceConfig2W(SC_HANDLE h, DWORD level, LPVOID info) {
  ScHandle* s = GetSc(h, false);
  if (s == NULL) return FALSE;
  if (level != SERVICE_CONFIG_DESCRIPTION) {
    SetLastError(ERROR_INVALID_LEVEL);
    return FALSE;
  }
  LPSERVICE_DESCRIPTIONW d = (LPSERVICE_DESCRIPTIONW)info;
  if (d == NULL || d->lpDescription == NULL) return TRUE;  // unchanged
  HKEY k = OpenServiceKey(s->name, true);
  if (k == NULL) {
    SetLastError(ERROR_SERVICE_MARKED_FOR_DELETE);
    return FALSE;
  }
  if (*d->lpDescription)
    SetString(k, L"Description", d->lpDescription);
  else
    RegDeleteValueW(k, L"Description");
  RegCloseKey(k);
  return TRUE;
}
BOOL WINAPI ChangeServiceConfig2A(SC_HANDLE h, DWORD level, LPVOID info) {
  if (level != SERVICE_CONFIG_DESCRIPTION) {
    SetLastError(ERROR_INVALID_LEVEL);
    return FALSE;
  }
  LPSERVICE_DESCRIPTIONA d = (LPSERVICE_DESCRIPTIONA)info;
  std::wstring w = d && d->lpDescription ? Widen(d->lpDescription) : std::wstring();
  SERVICE_DESCRIPTIONW dw = {d && d->lpDescription ? (LPWSTR)w.c_str() : NULL};
  return ChangeServiceConfig2W(h, level, &dw);
}

BOOL WINAPI QueryServiceConfigW(SC_HANDLE h, LPQUERY_SERVICE_CONFIGW q, DWORD cb, LPDWORD needed) {
  ScHandle* s = GetSc(h, false);
  Config c;
  if (s == NULL || !ReadConfig(s->name, &c)) return FALSE;
  return PackConfig<QUERY_SERVICE_CONFIGW, wchar_t>(c, q, cb, needed);
}
BOOL WINAPI QueryServiceConfigA(SC_HANDLE h, LPQUERY_SERVICE_CONFIGA q, DWORD cb, LPDWORD needed) {
  ScHandle* s = GetSc(h, false);
  Config c;
  if (s == NULL || !ReadConfig(s->name, &c)) return FALSE;
  return PackConfig<QUERY_SERVICE_CONFIGA, char>(c, q, cb, needed);
}

BOOL WINAPI QueryServiceConfig2W(SC_HANDLE h, DWORD level, LPBYTE buf, DWORD cb, LPDWORD needed) {
  ScHandle* s = GetSc(h, false);
  if (s == NULL) return FALSE;
  if (level != SERVICE_CONFIG_DESCRIPTION) {
    SetLastError(ERROR_INVALID_LEVEL);
    return FALSE;
  }
  HKEY k = OpenServiceKey(s->name, false);
  if (k == NULL) {
    SetLastError(ERROR_SERVICE_DOES_NOT_EXIST);
    return FALSE;
  }
  std::wstring d = GetString(k, L"Description");
  RegCloseKey(k);
  DWORD total = (DWORD)(sizeof(SERVICE_DESCRIPTIONW) + (d.empty() ? 0 : (d.size() + 1) * sizeof(wchar_t)));
  if (needed) *needed = total;
  if (buf == NULL || cb < total) {
    SetLastError(ERROR_INSUFFICIENT_BUFFER);
    return FALSE;
  }
  LPSERVICE_DESCRIPTIONW out = (LPSERVICE_DESCRIPTIONW)buf;
  if (d.empty()) {
    out->lpDescription = NULL;
  } else {
    out->lpDescription = (LPWSTR)(out + 1);
    wmemcpy(out->lpDescription, d.c_str(), d.size() + 1);
  }
  return TRUE;
}
BOOL WINAPI QueryServiceConfig2A(SC_HANDLE h, DWORD level, LPBYTE buf, DWORD cb, LPDWORD needed) {
  BYTE wbuf[8192];
  DWORD n = 0;
  if (!QueryServiceConfig2W(h, level, wbuf, sizeof wbuf, &n)) {
    if (needed) *needed = n;
    return FALSE;
  }
  LPSERVICE_DESCRIPTIONW w = (LPSERVICE_DESCRIPTIONW)wbuf;
  std::string a = w->lpDescription ? Narrow(w->lpDescription) : std::string();
  DWORD total = (DWORD)(sizeof(SERVICE_DESCRIPTIONA) + (w->lpDescription ? a.size() + 1 : 0));
  if (needed) *needed = total;
  if (buf == NULL || cb < total) {
    SetLastError(ERROR_INSUFFICIENT_BUFFER);
    return FALSE;
  }
  LPSERVICE_DESCRIPTIONA out = (LPSERVICE_DESCRIPTIONA)buf;
  if (w->lpDescription == NULL) {
    out->lpDescription = NULL;
  } else {
    out->lpDescription = (LPSTR)(out + 1);
    memcpy(out->lpDescription, a.c_str(), a.size() + 1);
  }
  return TRUE;
}

// Names: the display name of a service, and the service of a display name.
static bool FindName(const std::wstring& from, bool byKey, std::wstring* out) {
  if (byKey) {
    Config c;
    if (!ReadConfig(from, &c)) return false;
    *out = c.displayName;
    return true;
  }
  HKEY services;
  if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, kServicesKey, 0, KEY_READ, &services) == ERROR_SUCCESS) {
    WCHAR name[256];
    for (DWORD i = 0; RegEnumKeyW(services, i, name, 256) == ERROR_SUCCESS; i++) {
      Config c;
      if (ReadConfig(name, &c) && Lower(c.displayName) == Lower(from)) {
        *out = name;
        RegCloseKey(services);
        return true;
      }
    }
    RegCloseKey(services);
  }
  SetLastError(ERROR_SERVICE_DOES_NOT_EXIST);
  return false;
}
BOOL WINAPI GetServiceDisplayNameW(SC_HANDLE scm, LPCWSTR name, LPWSTR buf, LPDWORD cch) {
  std::wstring r;
  if (GetSc(scm, true) == NULL || cch == NULL || !FindName(W(name), true, &r)) return FALSE;
  if (buf == NULL || *cch <= r.size()) {
    *cch = (DWORD)r.size();
    SetLastError(ERROR_INSUFFICIENT_BUFFER);
    return FALSE;
  }
  wmemcpy(buf, r.c_str(), r.size() + 1);
  *cch = (DWORD)r.size();
  return TRUE;
}
BOOL WINAPI GetServiceKeyNameW(SC_HANDLE scm, LPCWSTR display, LPWSTR buf, LPDWORD cch) {
  std::wstring r;
  if (GetSc(scm, true) == NULL || cch == NULL || !FindName(W(display), false, &r)) return FALSE;
  if (buf == NULL || *cch <= r.size()) {
    *cch = (DWORD)r.size();
    SetLastError(ERROR_INSUFFICIENT_BUFFER);
    return FALSE;
  }
  wmemcpy(buf, r.c_str(), r.size() + 1);
  *cch = (DWORD)r.size();
  return TRUE;
}
static BOOL NameA(SC_HANDLE scm, LPCSTR from, LPSTR buf, LPDWORD cch, bool byKey) {
  std::wstring r;
  if (GetSc(scm, true) == NULL || cch == NULL || !FindName(W(from), byKey, &r)) return FALSE;
  std::string a = Narrow(r.c_str());
  if (buf == NULL || *cch <= a.size()) {
    *cch = (DWORD)a.size();
    SetLastError(ERROR_INSUFFICIENT_BUFFER);
    return FALSE;
  }
  memcpy(buf, a.c_str(), a.size() + 1);
  *cch = (DWORD)a.size();
  return TRUE;
}
BOOL WINAPI GetServiceDisplayNameA(SC_HANDLE scm, LPCSTR name, LPSTR buf, LPDWORD cch) {
  return NameA(scm, name, buf, cch, true);
}
BOOL WINAPI GetServiceKeyNameA(SC_HANDLE scm, LPCSTR display, LPSTR buf, LPDWORD cch) {
  return NameA(scm, display, buf, cch, false);
}

BOOL WINAPI EnumServicesStatusW(SC_HANDLE scm, DWORD types, DWORD states, LPENUM_SERVICE_STATUSW out, DWORD cb,
                                LPDWORD needed, LPDWORD returned, LPDWORD resume) {
  return Enum<ENUM_SERVICE_STATUSW, wchar_t>(scm, types, states, out, cb, needed, returned, resume);
}
BOOL WINAPI EnumServicesStatusA(SC_HANDLE scm, DWORD types, DWORD states, LPENUM_SERVICE_STATUSA out, DWORD cb,
                                LPDWORD needed, LPDWORD returned, LPDWORD resume) {
  return Enum<ENUM_SERVICE_STATUSA, char>(scm, types, states, out, cb, needed, returned, resume);
}

static BOOL Start(SC_HANDLE h, const std::vector<std::wstring>& args) {
  ScHandle* s = GetSc(h, false);
  Config c;
  if (s == NULL || !ReadConfig(s->name, &c)) return FALSE;
  if (c.start == SERVICE_DISABLED) {
    SetLastError(ERROR_SERVICE_DISABLED);
    return FALSE;
  }
  if (!(c.type & SERVICE_WIN32)) {
    SetLastError(ERROR_NOT_SUPPORTED);  // no drivers
    return FALSE;
  }
  if (ReadStatus(s->name, c.type, NULL).dwCurrentState != SERVICE_STOPPED) {
    SetLastError(ERROR_SERVICE_ALREADY_RUNNING);
    return FALSE;
  }
  // The environment, telling the program which service it is.
  std::string env;
  for (char** e = environ; *e; e++) {
    if (strncmp(*e, "WINPORT_SERVICE=", 16) == 0 || strncmp(*e, "WINPORT_SERVICE_ARGS=", 21) == 0) continue;
    env += *e;
    env += '\0';
  }
  env += std::string(kServiceEnv) + "=" + Narrow(s->name.c_str());
  env += '\0';
  std::string packed;
  for (size_t i = 0; i < args.size(); i++) {
    if (i) packed += '\x1f';
    packed += Narrow(args[i].c_str());
  }
  env += std::string(kArgsEnv) + "=" + packed;
  env += '\0';
  env += '\0';
  WCHAR cmd[32768];
  if (ExpandEnvironmentStringsW(c.path.c_str(), cmd, 32768) == 0) wcsncpy(cmd, c.path.c_str(), 32767);
  STARTUPINFOW si;
  memset(&si, 0, sizeof si);
  si.cb = sizeof si;
  PROCESS_INFORMATION pi;
  if (!CreateProcessW(NULL, cmd, NULL, NULL, FALSE, 0, (LPVOID)env.data(), NULL, &si, &pi)) return FALSE;
  SERVICE_STATUS st;
  memset(&st, 0, sizeof st);
  st.dwServiceType = c.type;
  st.dwCurrentState = SERVICE_START_PENDING;
  st.dwWaitHint = 2000;
  WriteStatus(s->name, st, pi.dwProcessId);
  // Until its dispatcher listens: then the start has worked.
  std::string sock = RuntimeBase(s->name) + ".sock";
  BOOL ok = FALSE;
  for (int i = 0; i < 3000; i++) {
    struct stat sb;
    if (stat(sock.c_str(), &sb) == 0 && S_ISSOCK(sb.st_mode)) {
      DWORD pid = 0;
      ReadStatus(s->name, c.type, &pid);
      if (pid == pi.dwProcessId) {
        ok = TRUE;
        break;
      }
    }
    if (WaitForSingleObject(pi.hProcess, 10) == WAIT_OBJECT_0) break;
  }
  if (!ok) {
    DWORD pid = 0;
    SERVICE_STATUS now = ReadStatus(s->name, c.type, &pid);
    // A service that ran and stopped at once still started.
    if (now.dwCurrentState == SERVICE_STOPPED && WaitForSingleObject(pi.hProcess, 0) == WAIT_OBJECT_0) {
      DWORD code = 0;
      GetExitCodeProcess(pi.hProcess, &code);
      st.dwCurrentState = SERVICE_STOPPED;
      st.dwWin32ExitCode = ERROR_SERVICE_REQUEST_TIMEOUT;
      WriteStatus(s->name, st, 0);
    }
    SetLastError(ERROR_SERVICE_REQUEST_TIMEOUT);
  }
  CloseHandle(pi.hThread);
  CloseHandle(pi.hProcess);
  return ok;
}
BOOL WINAPI StartServiceW(SC_HANDLE h, DWORD argc, LPCWSTR* argv) {
  std::vector<std::wstring> args;
  for (DWORD i = 0; i < argc && argv; i++) args.push_back(W(argv[i]));
  return Start(h, args);
}
BOOL WINAPI StartServiceA(SC_HANDLE h, DWORD argc, LPCSTR* argv) {
  std::vector<std::wstring> args;
  for (DWORD i = 0; i < argc && argv; i++) args.push_back(W(argv[i]));
  return Start(h, args);
}

BOOL WINAPI QueryServiceStatus(SC_HANDLE h, LPSERVICE_STATUS out) {
  ScHandle* s = GetSc(h, false);
  Config c;
  if (s == NULL || !ReadConfig(s->name, &c)) return FALSE;
  if (out == NULL) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  *out = ReadStatus(s->name, c.type, NULL);
  return TRUE;
}
BOOL WINAPI QueryServiceStatusEx(SC_HANDLE h, SC_STATUS_TYPE level, LPBYTE buf, DWORD cb, LPDWORD needed) {
  ScHandle* s = GetSc(h, false);
  Config c;
  if (s == NULL || !ReadConfig(s->name, &c)) return FALSE;
  if (level != SC_STATUS_PROCESS_INFO) {
    SetLastError(ERROR_INVALID_LEVEL);
    return FALSE;
  }
  if (needed) *needed = sizeof(SERVICE_STATUS_PROCESS);
  if (buf == NULL || cb < sizeof(SERVICE_STATUS_PROCESS)) {
    SetLastError(ERROR_INSUFFICIENT_BUFFER);
    return FALSE;
  }
  DWORD pid = 0;
  SERVICE_STATUS st = ReadStatus(s->name, c.type, &pid);
  SERVICE_STATUS_PROCESS* p = (SERVICE_STATUS_PROCESS*)buf;
  memcpy(p, &st, sizeof st);
  p->dwProcessId = pid;
  p->dwServiceFlags = 0;
  return TRUE;
}

BOOL WINAPI ControlService(SC_HANDLE h, DWORD code, LPSERVICE_STATUS out) {
  ScHandle* s = GetSc(h, false);
  Config c;
  if (s == NULL || !ReadConfig(s->name, &c)) return FALSE;
  SERVICE_STATUS st = ReadStatus(s->name, c.type, NULL);
  DWORD err;
  if (st.dwCurrentState == SERVICE_STOPPED) {
    err = ERROR_SERVICE_NOT_ACTIVE;
  } else if (code == SERVICE_CONTROL_STOP && st.dwCurrentState == SERVICE_STOP_PENDING) {
    err = ERROR_SERVICE_CANNOT_ACCEPT_CTRL;
  } else {
    err = SendControl(s->name, code);
    st = ReadStatus(s->name, c.type, NULL);
  }
  if (out) *out = st;
  if (err) {
    SetLastError(err);
    return FALSE;
  }
  return TRUE;
}

BOOL WINAPI DeleteService(SC_HANDLE h) {
  ScHandle* s = GetSc(h, false);
  if (s == NULL) return FALSE;
  HKEY services;
  if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, kServicesKey, 0, KEY_ALL_ACCESS, &services) != ERROR_SUCCESS) {
    SetLastError(ERROR_SERVICE_MARKED_FOR_DELETE);
    return FALSE;
  }
  HKEY k;
  if (RegOpenKeyExW(services, s->name.c_str(), 0, KEY_ALL_ACCESS, &k) != ERROR_SUCCESS) {
    RegCloseKey(services);
    SetLastError(ERROR_SERVICE_MARKED_FOR_DELETE);
    return FALSE;
  }
  // Its subkeys first (Parameters, Enum and the like).
  WCHAR sub[256];
  while (RegEnumKeyW(k, 0, sub, 256) == ERROR_SUCCESS) {
    HKEY child;
    if (RegOpenKeyExW(k, sub, 0, KEY_ALL_ACCESS, &child) == ERROR_SUCCESS) {
      WCHAR grand[256];
      while (RegEnumKeyW(child, 0, grand, 256) == ERROR_SUCCESS)
        if (RegDeleteKeyW(child, grand) != ERROR_SUCCESS) break;
      RegCloseKey(child);
    }
    if (RegDeleteKeyW(k, sub) != ERROR_SUCCESS) break;
  }
  RegCloseKey(k);
  LONG l = RegDeleteKeyW(services, s->name.c_str());
  RegCloseKey(services);
  if (l != ERROR_SUCCESS) {
    SetLastError((DWORD)l);
    return FALSE;
  }
  // A stopped service leaves no status behind.
  DWORD pid = 0;
  if (ReadStatus(s->name, SERVICE_WIN32_OWN_PROCESS, &pid).dwCurrentState == SERVICE_STOPPED)
    unlink((RuntimeBase(s->name) + ".status").c_str());
  return TRUE;
}

BOOL WINAPI CloseServiceHandle(SC_HANDLE h) {
  ScHandle* s = (ScHandle*)h;
  if (s == NULL || s->magic != ScHandle::kMagic) {
    SetLastError(ERROR_INVALID_HANDLE);
    return FALSE;
  }
  s->magic = 0;
  delete s;
  return TRUE;
}

SC_LOCK WINAPI LockServiceDatabase(SC_HANDLE scm) {
  if (GetSc(scm, true) == NULL) return NULL;
  static int token;
  return &token;
}
BOOL WINAPI UnlockServiceDatabase(SC_LOCK) { return TRUE; }

// The service's side.
BOOL WINAPI StartServiceCtrlDispatcherW(const SERVICE_TABLE_ENTRYW* table) {
  std::wstring name = DispatcherName();
  if (table == NULL || table[0].lpServiceProc == NULL) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  // Not started by StartService: as on Windows, run from a console.
  if (name.empty() || g_running) {
    SetLastError(g_running ? ERROR_SERVICE_ALREADY_RUNNING : ERROR_FAILED_SERVICE_CONTROLLER_CONNECT);
    return FALSE;
  }
  // A process of one service runs the first entry whatever its name; a
  // shared one, the entry of the name.
  const SERVICE_TABLE_ENTRYW* e = table;
  for (const SERVICE_TABLE_ENTRYW* t = table; t->lpServiceProc; t++)
    if (t->lpServiceName && Lower(t->lpServiceName) == Lower(name)) e = t;
  DWORD err = Dispatch(name, e->lpServiceProc, NULL);
  if (err) {
    SetLastError(err);
    return FALSE;
  }
  return TRUE;
}
BOOL WINAPI StartServiceCtrlDispatcherA(const SERVICE_TABLE_ENTRYA* table) {
  std::wstring name = DispatcherName();
  if (table == NULL || table[0].lpServiceProc == NULL) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  if (name.empty() || g_running) {
    SetLastError(g_running ? ERROR_SERVICE_ALREADY_RUNNING : ERROR_FAILED_SERVICE_CONTROLLER_CONNECT);
    return FALSE;
  }
  const SERVICE_TABLE_ENTRYA* e = table;
  for (const SERVICE_TABLE_ENTRYA* t = table; t->lpServiceProc; t++)
    if (t->lpServiceName && Lower(Widen(t->lpServiceName)) == Lower(name)) e = t;
  DWORD err = Dispatch(name, NULL, e->lpServiceProc);
  if (err) {
    SetLastError(err);
    return FALSE;
  }
  return TRUE;
}

SERVICE_STATUS_HANDLE WINAPI RegisterServiceCtrlHandlerW(LPCWSTR name, LPHANDLER_FUNCTION fn) {
  return Register(W(name), fn, NULL, NULL);
}
SERVICE_STATUS_HANDLE WINAPI RegisterServiceCtrlHandlerA(LPCSTR name, LPHANDLER_FUNCTION fn) {
  return Register(W(name), fn, NULL, NULL);
}
SERVICE_STATUS_HANDLE WINAPI RegisterServiceCtrlHandlerExW(LPCWSTR name, LPHANDLER_FUNCTION_EX fn, LPVOID ctx) {
  return Register(W(name), NULL, fn, ctx);
}
SERVICE_STATUS_HANDLE WINAPI RegisterServiceCtrlHandlerExA(LPCSTR name, LPHANDLER_FUNCTION_EX fn, LPVOID ctx) {
  return Register(W(name), NULL, fn, ctx);
}

BOOL WINAPI SetServiceStatus(SERVICE_STATUS_HANDLE h, LPSERVICE_STATUS st) {
  Running* r = g_running;
  if (r == NULL || h != (SERVICE_STATUS_HANDLE)r) {
    SetLastError(ERROR_INVALID_HANDLE);
    return FALSE;
  }
  if (st == NULL || st->dwCurrentState < SERVICE_STOPPED || st->dwCurrentState > SERVICE_PAUSED) {
    SetLastError(ERROR_INVALID_DATA);
    return FALSE;
  }
  pthread_mutex_lock(WaitLock());
  r->status = *st;
  pthread_mutex_unlock(WaitLock());
  WriteStatus(r->name, *st, (DWORD)getpid());
  return TRUE;
}

// The event log.
HANDLE WINAPI RegisterEventSourceW(LPCWSTR server, LPCWSTR source) {
  if (server && *server) {
    SetLastError(1722);  // RPC_S_SERVER_UNAVAILABLE
    return NULL;
  }
  EventSource* s = new EventSource;
  s->name = source ? Narrow(source) : std::string("Application");
  return (HANDLE)s;
}
HANDLE WINAPI RegisterEventSourceA(LPCSTR server, LPCSTR source) {
  std::wstring a = W(server), b = W(source);
  return RegisterEventSourceW(server ? a.c_str() : NULL, source ? b.c_str() : NULL);
}
BOOL WINAPI DeregisterEventSource(HANDLE h) {
  EventSource* s = (EventSource*)h;
  if (s == NULL || s->magic != EventSource::kMagic) {
    SetLastError(ERROR_INVALID_HANDLE);
    return FALSE;
  }
  s->magic = 0;
  delete s;
  return TRUE;
}
BOOL WINAPI ReportEventW(HANDLE h, WORD type, WORD, DWORD id, PSID, WORD n, DWORD, LPCWSTR* strings, LPVOID) {
  std::vector<std::string> v;
  for (WORD i = 0; i < n && strings; i++) v.push_back(strings[i] ? Narrow(strings[i]) : std::string());
  return Report(h, type, id, v);
}
BOOL WINAPI ReportEventA(HANDLE h, WORD type, WORD, DWORD id, PSID, WORD n, DWORD, LPCSTR* strings, LPVOID) {
  std::vector<std::string> v;
  for (WORD i = 0; i < n && strings; i++) v.push_back(strings[i] ? strings[i] : "");
  return Report(h, type, id, v);
}

}  // extern "C"
