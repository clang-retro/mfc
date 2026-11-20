// kernel_sync.cpp - kernel objects and handles, events, mutexes,
// semaphores, waits, threads, thread local storage and critical
// sections, over POSIX threads.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include "winport.h"
#include "file.h"

#include <errno.h>
#include <sched.h>
#include <signal.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
#include <deque>
#include <functional>
#include <map>
#include <new>
#include <set>
#include <vector>
#ifdef __APPLE__
#include <mach/mach_time.h>
#endif

namespace winport {

static pthread_mutex_t g_waitLock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_waitCond = PTHREAD_COND_INITIALIZER;
pthread_mutex_t* WaitLock() { return &g_waitLock; }
void SignalChange() { pthread_cond_broadcast(&g_waitCond); }

Object* FromHandle(HANDLE h, ObjectType type) {
  // Small values and the pseudo handles (-1, -2) are not objects.
  UINT_PTR v = (UINT_PTR)h;
  if (v < 0x10000 || v > (UINT_PTR)-0x10000) {
    SetLastError(ERROR_INVALID_HANDLE);
    return NULL;
  }
  Object* o = (Object*)h;
  if (o->magic != Object::kMagic || (type != 0 && o->type != type)) {
    SetLastError(ERROR_INVALID_HANDLE);
    return NULL;
  }
  return o;
}
HANDLE NewHandle(Object* o) { return (HANDLE)o; }
// Named objects (events, mutexes, semaphores, timers): the names are
// this process's.
static pthread_mutex_t g_namesLock = PTHREAD_MUTEX_INITIALIZER;
static std::map<std::wstring, Object*>& Names() {
  static auto* m = new std::map<std::wstring, Object*>;
  return *m;
}
std::wstring ObjectName(const wchar_t* name) {
  std::wstring n = name;
  // The Global and Local namespaces are one here.
  if (n.compare(0, 7, L"Global\\") == 0) n.erase(0, 7);
  else if (n.compare(0, 6, L"Local\\") == 0) n.erase(0, 6);
  return n;
}

void Release(Object* o) {
  if (o->refs.fetch_sub(1) == 1) {
    if (o->named) {
      pthread_mutex_lock(&g_namesLock);
      for (auto it = Names().begin(); it != Names().end(); ++it)
        if (it->second == o) {
          Names().erase(it);
          break;
        }
      pthread_mutex_unlock(&g_namesLock);
    }
    o->magic = 0;
    delete o;
  }
}

namespace {

struct Event : Object {
  bool manual, signaled;
  Event(bool m, bool s) : Object(kEvent), manual(m), signaled(s) {}
  bool Waitable() override { return true; }
  bool IsSignaled(DWORD) override { return signaled; }
  void Acquire(DWORD) override {
    if (!manual) signaled = false;
  }
};

struct Mutex : Object {
  DWORD owner = 0;
  int count = 0;
  Mutex() : Object(kMutex) {}
  bool Waitable() override { return true; }
  bool IsSignaled(DWORD tid) override { return count == 0 || owner == tid; }
  void Acquire(DWORD tid) override {
    owner = tid;
    count++;
  }
};

struct Semaphore : Object {
  LONG count, max;
  Semaphore(LONG c, LONG m) : Object(kSemaphore), count(c), max(m) {}
  bool Waitable() override { return true; }
  bool IsSignaled(DWORD) override { return count > 0; }
  void Acquire(DWORD) override { count--; }
};

struct Thread : Object {
  pthread_t thread;
  DWORD id = 0;
  bool done = false;
  DWORD exitCode = STILL_ACTIVE;
  LPTHREAD_START_ROUTINE start = NULL;
  LPVOID param = NULL;
  bool suspended = false;
  pthread_cond_t resume = PTHREAD_COND_INITIALIZER;
  int priority = THREAD_PRIORITY_NORMAL;
  Thread() : Object(kThread) {}
  bool Waitable() override { return true; }
  bool IsSignaled(DWORD) override { return done; }
};

// Waitable timers. A timer notices it is due when it is looked at: by a
// wait on it, which sleeps no longer than its due time, or by an
// alertable wait of the thread its completion routine runs on.
ULONGLONG NowFileTime() {
  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  return (ULONGLONG)ts.tv_sec * 10000000 + ts.tv_nsec / 100 + kEpochDelta;
}

struct Timer : Object {
  bool manual;
  bool signaled = false;
  ULONGLONG due = 0;  // 0: not set
  LONG period = 0;    // milliseconds
  PTIMERAPCROUTINE routine = NULL;
  LPVOID arg = NULL;
  DWORD apcThread = 0;
  explicit Timer(bool m) : Object(kOther), manual(m) {}
  ~Timer() override;
  bool Waitable() override { return true; }
  void Check();
  bool IsSignaled(DWORD) override {
    Check();
    return signaled;
  }
  void Acquire(DWORD) override {
    if (!manual) signaled = false;
  }
  ULONGLONG Due() override { return due; }
};
std::set<Timer*> g_apcTimers;  // the timers with a completion routine

// Asynchronous procedure calls, by thread id.
std::map<DWORD, std::deque<std::function<void()>>> g_apcs;

void QueueApc(DWORD tid, std::function<void()> fn) {
  g_apcs[tid].push_back(std::move(fn));
  SignalChange();
}

Timer::~Timer() {
  if (routine) {
    pthread_mutex_lock(WaitLock());
    g_apcTimers.erase(this);
    pthread_mutex_unlock(WaitLock());
  }
}

void Timer::Check() {
  if (due == 0) return;
  ULONGLONG now = NowFileTime();
  if (now < due) return;
  signaled = true;
  if (routine) {
    PTIMERAPCROUTINE r = routine;
    LPVOID a = arg;
    ULONGLONG t = now;
    QueueApc(apcThread, [r, a, t] { r(a, (DWORD)t, (DWORD)(t >> 32)); });
  }
  if (period > 0) {
    ULONGLONG step = (ULONGLONG)period * 10000;
    due += step;
    if (due <= now) due = now + step;  // missed periods are not made up
  } else {
    due = 0;
  }
  SignalChange();
}

// The soonest due time of the timers whose routines run on tid, checking
// them on the way.
ULONGLONG ApcTimersDue(DWORD tid) {
  ULONGLONG soonest = 0;
  for (Timer* t : g_apcTimers) {
    if (t->apcThread != tid) continue;
    t->Check();
    if (t->due && (soonest == 0 || t->due < soonest)) soonest = t->due;
  }
  return soonest;
}

// Completion ports: a queue of packets.
struct Packet {
  DWORD bytes;
  ULONG_PTR key;
  LPOVERLAPPED ov;
  DWORD error;
};
struct Port : Object {
  std::deque<Packet> packets;
  Port() : Object(kOther) {}
};

std::atomic<DWORD> g_nextThreadId{1};
thread_local DWORD t_threadId = 0;
thread_local Thread* t_thread = NULL;
thread_local DWORD t_lastError = 0;

}  // namespace

// A new named object, or another handle to the one of that name.
template <class T, class Make>
HANDLE CreateNamed(LPCWSTR name, LPSECURITY_ATTRIBUTES sa, Make make) {
  std::wstring n = name && *name ? ObjectName(name) : std::wstring();
  if (!n.empty()) {
    pthread_mutex_lock(&g_namesLock);
    auto it = Names().find(n);
    if (it != Names().end()) {
      Object* o = it->second;
      int r = o->refs.load();
      while (r > 0 && !o->refs.compare_exchange_weak(r, r + 1)) {
      }
      if (r > 0) {
        pthread_mutex_unlock(&g_namesLock);
        if (dynamic_cast<T*>(o) == NULL) {
          Release(o);
          SetLastError(ERROR_INVALID_HANDLE);
          return NULL;
        }
        SetLastError(ERROR_ALREADY_EXISTS);
        return NewHandle(o);
      }
    }
  }
  T* o = make();
  SetObjectSecurity(o, sa);
  if (!n.empty()) {
    o->named = true;
    Names()[n] = o;
    pthread_mutex_unlock(&g_namesLock);
  }
  SetLastError(0);
  return NewHandle(o);
}
template <class T>
HANDLE OpenNamed(LPCWSTR name) {
  if (name == NULL || !*name) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return NULL;
  }
  pthread_mutex_lock(&g_namesLock);
  auto it = Names().find(ObjectName(name));
  Object* o = it != Names().end() ? it->second : NULL;
  int r = o ? o->refs.load() : 0;
  while (r > 0 && !o->refs.compare_exchange_weak(r, r + 1)) {
  }
  pthread_mutex_unlock(&g_namesLock);
  if (r == 0) {
    SetLastError(ERROR_FILE_NOT_FOUND);
    return NULL;
  }
  if (dynamic_cast<T*>(o) == NULL) {
    Release(o);
    SetLastError(ERROR_INVALID_HANDLE);
    return NULL;
  }
  return NewHandle(o);
}
std::wstring WideName(LPCSTR s) { return s ? Widen(s) : std::wstring(); }

DWORD CurrentThreadId() {
  if (t_threadId == 0) t_threadId = g_nextThreadId.fetch_add(1);
  return t_threadId;
}

}  // namespace winport

using namespace winport;

extern "C" {

DWORD WINAPI GetLastError(void) { return t_lastError; }
void WINAPI SetLastError(DWORD e) { t_lastError = e; }

BOOL WINAPI CloseHandle(HANDLE h) {
  if (h == GetCurrentProcess() || h == GetCurrentThread()) return TRUE;
  Object* o = FromHandle(h);
  if (o == NULL) {
    SetLastError(ERROR_INVALID_HANDLE);
    return FALSE;
  }
  Release(o);
  return TRUE;
}

// Duplicated handles share their object (with a count of references),
// which is what programs see of handle duplication within a process.
BOOL WINAPI DuplicateHandle(HANDLE hSourceProcess, HANDLE hSource, HANDLE hTargetProcess, LPHANDLE lpTarget,
                            DWORD dwAccess, BOOL bInherit, DWORD dwOptions) {
  (void)hSourceProcess; (void)hTargetProcess; (void)dwAccess; (void)bInherit;
  Object* o = FromHandle(hSource);
  if (o == NULL) {
    if (hSource == GetCurrentThread() && t_thread != NULL) {
      o = t_thread;
    } else {
      SetLastError(ERROR_INVALID_HANDLE);
      return FALSE;
    }
  }
  if (o->type == kFile) {
    // Files get a descriptor of their own, so that closing either
    // handle leaves the other usable.
    HANDLE h = winport::DuplicateFile(o);
    if (h == NULL) return FALSE;
    if (lpTarget) *lpTarget = h;
  } else {
    o->refs.fetch_add(1);
    if (lpTarget) *lpTarget = NewHandle(o);
  }
  if (dwOptions & DUPLICATE_CLOSE_SOURCE) CloseHandle(hSource);
  return TRUE;
}

// Events, mutexes and semaphores. Named objects are shared within the
// process only.
HANDLE WINAPI CreateEventW(LPSECURITY_ATTRIBUTES sa, BOOL bManualReset, BOOL bInitialState, LPCWSTR name) {
  return CreateNamed<Event>(name, sa, [&] { return new Event(bManualReset != 0, bInitialState != 0); });
}
HANDLE WINAPI CreateEventA(LPSECURITY_ATTRIBUTES sa, BOOL m, BOOL s, LPCSTR name) {
  std::wstring w = WideName(name);
  return CreateEventW(sa, m, s, name ? w.c_str() : NULL);
}
HANDLE WINAPI OpenEventW(DWORD, BOOL, LPCWSTR name) { return OpenNamed<Event>(name); }
HANDLE WINAPI OpenEventA(DWORD, BOOL, LPCSTR name) {
  std::wstring w = WideName(name);
  return OpenNamed<Event>(name ? w.c_str() : NULL);
}
static BOOL SetEventState(HANDLE h, int how) {
  Event* e = (Event*)FromHandle(h, kEvent);
  if (e == NULL) {
    SetLastError(ERROR_INVALID_HANDLE);
    return FALSE;
  }
  pthread_mutex_lock(WaitLock());
  if (how == 0) {
    e->signaled = false;
  } else {
    e->signaled = true;
    SignalChange();
    if (how == 2) {
      // PulseEvent: wake the waiters there are now, then reset. The
      // waiters re-check under the lock, so give them a moment to run.
      pthread_mutex_unlock(WaitLock());
      sched_yield();
      pthread_mutex_lock(WaitLock());
      e->signaled = false;
    }
  }
  pthread_mutex_unlock(WaitLock());
  return TRUE;
}
BOOL WINAPI SetEvent(HANDLE h) { return SetEventState(h, 1); }
BOOL WINAPI ResetEvent(HANDLE h) { return SetEventState(h, 0); }
BOOL WINAPI PulseEvent(HANDLE h) { return SetEventState(h, 2); }

HANDLE WINAPI CreateMutexW(LPSECURITY_ATTRIBUTES sa, BOOL bInitialOwner, LPCWSTR name) {
  return CreateNamed<Mutex>(name, sa, [&] {
    Mutex* m = new Mutex();
    if (bInitialOwner) m->Acquire(CurrentThreadId());
    return m;
  });
}
HANDLE WINAPI CreateMutexA(LPSECURITY_ATTRIBUTES sa, BOOL b, LPCSTR name) {
  std::wstring w = WideName(name);
  return CreateMutexW(sa, b, name ? w.c_str() : NULL);
}
HANDLE WINAPI OpenMutexW(DWORD, BOOL, LPCWSTR name) { return OpenNamed<Mutex>(name); }
HANDLE WINAPI OpenMutexA(DWORD, BOOL, LPCSTR name) {
  std::wstring w = WideName(name);
  return OpenNamed<Mutex>(name ? w.c_str() : NULL);
}
BOOL WINAPI ReleaseMutex(HANDLE h) {
  Mutex* m = (Mutex*)FromHandle(h, kMutex);
  if (m == NULL) {
    SetLastError(ERROR_INVALID_HANDLE);
    return FALSE;
  }
  BOOL ok = FALSE;
  pthread_mutex_lock(WaitLock());
  if (m->count > 0 && m->owner == CurrentThreadId()) {
    if (--m->count == 0) {
      m->owner = 0;
      SignalChange();
    }
    ok = TRUE;
  }
  pthread_mutex_unlock(WaitLock());
  if (!ok) SetLastError(288);  // ERROR_NOT_OWNER
  return ok;
}

HANDLE WINAPI CreateSemaphoreW(LPSECURITY_ATTRIBUTES sa, LONG lInitialCount, LONG lMaximumCount, LPCWSTR name) {
  if (lMaximumCount <= 0 || lInitialCount < 0 || lInitialCount > lMaximumCount) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return NULL;
  }
  return CreateNamed<Semaphore>(name, sa, [&] { return new Semaphore(lInitialCount, lMaximumCount); });
}
HANDLE WINAPI CreateSemaphoreA(LPSECURITY_ATTRIBUTES sa, LONG i, LONG m, LPCSTR name) {
  std::wstring w = WideName(name);
  return CreateSemaphoreW(sa, i, m, name ? w.c_str() : NULL);
}
HANDLE WINAPI OpenSemaphoreW(DWORD, BOOL, LPCWSTR name) { return OpenNamed<Semaphore>(name); }
HANDLE WINAPI OpenSemaphoreA(DWORD, BOOL, LPCSTR name) {
  std::wstring w = WideName(name);
  return OpenNamed<Semaphore>(name ? w.c_str() : NULL);
}
BOOL WINAPI ReleaseSemaphore(HANDLE h, LONG lReleaseCount, LPLONG lpPreviousCount) {
  Semaphore* s = (Semaphore*)FromHandle(h, kSemaphore);
  if (s == NULL) {
    SetLastError(ERROR_INVALID_HANDLE);
    return FALSE;
  }
  BOOL ok = FALSE;
  pthread_mutex_lock(WaitLock());
  if (lReleaseCount > 0 && s->count + lReleaseCount <= s->max) {
    if (lpPreviousCount) *lpPreviousCount = s->count;
    s->count += lReleaseCount;
    SignalChange();
    ok = TRUE;
  }
  pthread_mutex_unlock(WaitLock());
  if (!ok) SetLastError(298);  // ERROR_TOO_MANY_POSTS
  return ok;
}

// Waits. The deadline is absolute on the realtime clock, which is what
// pthread_cond_timedwait takes everywhere.
static void Deadline(DWORD ms, struct timespec* ts) {
  struct timeval tv;
  gettimeofday(&tv, NULL);
  long long ns = (long long)tv.tv_usec * 1000 + (long long)(ms % 1000) * 1000000;
  ts->tv_sec = tv.tv_sec + ms / 1000 + ns / 1000000000;
  ts->tv_nsec = (long)(ns % 1000000000);
}

// Runs the queued APCs of the calling thread; called with WaitLock held,
// which it lets go of while they run.
static bool RunApcsLocked(DWORD tid) {
  auto it = g_apcs.find(tid);
  if (it == g_apcs.end() || it->second.empty()) return false;
  std::deque<std::function<void()>> q;
  q.swap(it->second);
  g_apcs.erase(it);
  pthread_mutex_unlock(WaitLock());
  for (auto& fn : q) fn();
  pthread_mutex_lock(WaitLock());
  return true;
}

}  // extern "C"
namespace winport {
bool DeliverApcs() {
  DWORD tid = CurrentThreadId();
  pthread_mutex_lock(WaitLock());
  ApcTimersDue(tid);
  bool ran = RunApcsLocked(tid);
  pthread_mutex_unlock(WaitLock());
  return ran;
}
}  // namespace winport
extern "C" {

// Sleeps on the wait condition until the deadline or the soonest of the
// due times, whichever is first; with WaitLock held. Returns false once
// the deadline has passed.
static bool SleepUntil(bool forever, const struct timespec& deadline, ULONGLONG due) {
  struct timespec ts = deadline;
  bool byDue = false;
  if (due) {
    ULONGLONG unix100 = due > kEpochDelta ? due - kEpochDelta : 0;
    struct timespec d = {(time_t)(unix100 / 10000000), (long)(unix100 % 10000000) * 100};
    if (forever || d.tv_sec < ts.tv_sec || (d.tv_sec == ts.tv_sec && d.tv_nsec < ts.tv_nsec)) {
      ts = d;
      byDue = true;
    }
  }
  if (forever && !byDue) {
    pthread_cond_wait(&g_waitCond, WaitLock());
    return true;
  }
  return pthread_cond_timedwait(&g_waitCond, WaitLock(), &ts) != ETIMEDOUT || byDue;
}

static DWORD Wait(DWORD nCount, Object** objs, BOOL bWaitAll, DWORD dwMilliseconds, BOOL bAlertable) {
  DWORD tid = CurrentThreadId();
  struct timespec ts = {0, 0};
  if (dwMilliseconds != INFINITE) Deadline(dwMilliseconds, &ts);
  pthread_mutex_lock(WaitLock());
  DWORD result = WAIT_TIMEOUT;
  for (;;) {
    ULONGLONG due = 0;
    if (bAlertable) {
      due = ApcTimersDue(tid);
      if (RunApcsLocked(tid)) {
        result = WAIT_IO_COMPLETION;
        break;
      }
    }
    if (nCount && bWaitAll) {
      bool all = true;
      for (DWORD i = 0; i < nCount; i++) all = objs[i]->IsSignaled(tid) && all;
      if (all) {
        for (DWORD i = 0; i < nCount; i++) objs[i]->Acquire(tid);
        result = WAIT_OBJECT_0;
        break;
      }
    } else if (nCount) {
      DWORD i = 0;
      for (; i < nCount; i++)
        if (objs[i]->IsSignaled(tid)) break;
      if (i < nCount) {
        objs[i]->Acquire(tid);
        result = WAIT_OBJECT_0 + i;
        break;
      }
    }
    if (dwMilliseconds == 0) break;
    for (DWORD i = 0; i < nCount; i++) {
      ULONGLONG d = objs[i]->Due();
      if (d && (due == 0 || d < due)) due = d;
    }
    if (!SleepUntil(dwMilliseconds == INFINITE, ts, due)) dwMilliseconds = 0;  // one last look
  }
  pthread_mutex_unlock(WaitLock());
  return result;
}

DWORD WINAPI WaitForMultipleObjectsEx(DWORD nCount, const HANDLE* lpHandles, BOOL bWaitAll, DWORD dwMilliseconds,
                                      BOOL bAlertable) {
  if (nCount == 0 || nCount > MAXIMUM_WAIT_OBJECTS) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return WAIT_FAILED;
  }
  Object* objs[MAXIMUM_WAIT_OBJECTS];
  for (DWORD i = 0; i < nCount; i++) {
    HANDLE h = lpHandles[i];
    if (h == GetCurrentThread() && t_thread) h = (HANDLE)t_thread;
    objs[i] = FromHandle(h);
    if (objs[i] == NULL || !objs[i]->Waitable()) {
      SetLastError(ERROR_INVALID_HANDLE);
      return WAIT_FAILED;
    }
  }
  return Wait(nCount, objs, bWaitAll, dwMilliseconds, bAlertable);
}
DWORD WINAPI WaitForMultipleObjects(DWORD n, const HANDLE* h, BOOL all, DWORD ms) {
  return WaitForMultipleObjectsEx(n, h, all, ms, FALSE);
}
DWORD WINAPI WaitForSingleObject(HANDLE h, DWORD ms) { return WaitForMultipleObjectsEx(1, &h, FALSE, ms, FALSE); }
DWORD WINAPI WaitForSingleObjectEx(HANDLE h, DWORD ms, BOOL a) { return WaitForMultipleObjectsEx(1, &h, FALSE, ms, a); }
DWORD WINAPI SignalObjectAndWait(HANDLE hSignal, HANDLE hWait, DWORD ms, BOOL a) {
  Object* o = FromHandle(hSignal);
  if (o && o->type == kEvent) SetEvent(hSignal);
  else if (o && o->type == kMutex) ReleaseMutex(hSignal);
  else if (o && o->type == kSemaphore) ReleaseSemaphore(hSignal, 1, NULL);
  else {
    SetLastError(ERROR_INVALID_HANDLE);
    return WAIT_FAILED;
  }
  return WaitForSingleObjectEx(hWait, ms, a);
}

void WINAPI Sleep(DWORD ms) {
  if (ms == 0) {
    sched_yield();
    return;
  }
  if (ms == INFINITE) {
    for (;;) pause();
  }
  struct timespec ts = {(time_t)(ms / 1000), (long)(ms % 1000) * 1000000};
  while (nanosleep(&ts, &ts) == -1 && errno == EINTR) {
  }
}
DWORD WINAPI SleepEx(DWORD ms, BOOL bAlertable) {
  if (!bAlertable) {
    Sleep(ms);
    return 0;
  }
  return Wait(0, NULL, FALSE, ms, TRUE) == WAIT_IO_COMPLETION ? WAIT_IO_COMPLETION : 0;
}

DWORD WINAPI QueueUserAPC(PAPCFUNC pfn, HANDLE hThread, ULONG_PTR data) {
  DWORD tid;
  if (hThread == GetCurrentThread()) {
    tid = CurrentThreadId();
  } else {
    Thread* t = (Thread*)FromHandle(hThread, kThread);
    if (t == NULL || pfn == NULL) {
      SetLastError(ERROR_INVALID_PARAMETER);
      return 0;
    }
    tid = t->id;
  }
  pthread_mutex_lock(WaitLock());
  QueueApc(tid, [pfn, data] { pfn(data); });
  pthread_mutex_unlock(WaitLock());
  return 1;
}

// Waitable timers
HANDLE WINAPI CreateWaitableTimerW(LPSECURITY_ATTRIBUTES sa, BOOL bManualReset, LPCWSTR name) {
  return CreateNamed<Timer>(name, sa, [&] { return new Timer(bManualReset != FALSE); });
}
HANDLE WINAPI CreateWaitableTimerA(LPSECURITY_ATTRIBUTES sa, BOOL m, LPCSTR name) {
  std::wstring w = WideName(name);
  return CreateWaitableTimerW(sa, m, name ? w.c_str() : NULL);
}
HANDLE WINAPI CreateWaitableTimerExW(LPSECURITY_ATTRIBUTES sa, LPCWSTR name, DWORD flags, DWORD) {
  return CreateWaitableTimerW(sa, (flags & CREATE_WAITABLE_TIMER_MANUAL_RESET) != 0, name);
}
HANDLE WINAPI OpenWaitableTimerW(DWORD, BOOL, LPCWSTR name) { return OpenNamed<Timer>(name); }
HANDLE WINAPI OpenWaitableTimerA(DWORD, BOOL, LPCSTR name) {
  std::wstring w = WideName(name);
  return OpenNamed<Timer>(name ? w.c_str() : NULL);
}
static Timer* GetTimer(HANDLE h) {
  Object* o = FromHandle(h, kOther);
  Timer* t = o ? dynamic_cast<Timer*>(o) : NULL;
  if (t == NULL) SetLastError(ERROR_INVALID_HANDLE);
  return t;
}
BOOL WINAPI SetWaitableTimer(HANDLE h, const LARGE_INTEGER* pDue, LONG lPeriod, PTIMERAPCROUTINE pfn, LPVOID arg,
                             BOOL) {
  Timer* t = GetTimer(h);
  if (t == NULL) return FALSE;
  if (pDue == NULL || lPeriod < 0) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  // Negative: relative, in 100 ns units; positive: an absolute FILETIME.
  ULONGLONG now = NowFileTime();
  ULONGLONG due = pDue->QuadPart < 0 ? now + (ULONGLONG)(-pDue->QuadPart) : (ULONGLONG)pDue->QuadPart;
  if (due == 0) due = 1;
  pthread_mutex_lock(WaitLock());
  t->due = due;
  t->period = lPeriod;
  t->signaled = false;
  t->routine = pfn;
  t->arg = arg;
  t->apcThread = CurrentThreadId();
  if (pfn)
    g_apcTimers.insert(t);
  else
    g_apcTimers.erase(t);
  SignalChange();
  pthread_mutex_unlock(WaitLock());
  SetLastError(0);
  return TRUE;
}
BOOL WINAPI CancelWaitableTimer(HANDLE h) {
  Timer* t = GetTimer(h);
  if (t == NULL) return FALSE;
  pthread_mutex_lock(WaitLock());
  t->due = 0;
  g_apcTimers.erase(t);
  pthread_mutex_unlock(WaitLock());
  return TRUE;
}

// I/O completion ports
HANDLE WINAPI CreateIoCompletionPort(HANDLE hFile, HANDLE hExisting, ULONG_PTR key, DWORD) {
  Port* port = NULL;
  if (hExisting) {
    Object* o = FromHandle(hExisting, kOther);
    port = o ? dynamic_cast<Port*>(o) : NULL;
    if (port == NULL) {
      SetLastError(ERROR_INVALID_PARAMETER);
      return NULL;
    }
  }
  File* f = NULL;
  if (hFile != INVALID_HANDLE_VALUE) {
    f = GetFile(hFile);
    if (f == NULL) {
      SetLastError(ERROR_INVALID_HANDLE);
      return NULL;
    }
    if (f->port) {
      SetLastError(ERROR_INVALID_PARAMETER);
      return NULL;
    }
  }
  if (port == NULL) port = new Port();
  if (f) {
    // The file keeps the port alive.
    port->refs.fetch_add(1);
    f->port = (HANDLE)port;
    f->portKey = key;
  }
  SetLastError(0);
  return (HANDLE)port;
}
static Port* GetPort(HANDLE h) {
  Object* o = FromHandle(h, kOther);
  Port* p = o ? dynamic_cast<Port*>(o) : NULL;
  if (p == NULL) SetLastError(ERROR_INVALID_HANDLE);
  return p;
}
}  // extern "C"
BOOL winport::PostCompletion(HANDLE h, DWORD bytes, ULONG_PTR key, LPOVERLAPPED ov, DWORD error) {
  Port* p = GetPort(h);
  if (p == NULL) return FALSE;
  pthread_mutex_lock(WaitLock());
  p->packets.push_back(Packet{bytes, key, ov, error});
  SignalChange();
  pthread_mutex_unlock(WaitLock());
  return TRUE;
}
extern "C" {
BOOL WINAPI PostQueuedCompletionStatus(HANDLE h, DWORD bytes, ULONG_PTR key, LPOVERLAPPED ov) {
  return PostCompletion(h, bytes, key, ov, 0);
}
static ULONG GetPackets(HANDLE h, Packet* out, ULONG max, DWORD ms, BOOL alertable, bool* apc) {
  *apc = false;
  Port* p = GetPort(h);
  if (p == NULL) return 0;
  DWORD tid = CurrentThreadId();
  struct timespec ts = {0, 0};
  if (ms != INFINITE) Deadline(ms, &ts);
  ULONG n = 0;
  pthread_mutex_lock(WaitLock());
  for (;;) {
    ULONGLONG due = 0;
    if (alertable) {
      due = ApcTimersDue(tid);
      if (RunApcsLocked(tid)) {
        *apc = true;
        break;
      }
    }
    while (n < max && !p->packets.empty()) {
      out[n++] = p->packets.front();
      p->packets.pop_front();
    }
    if (n || ms == 0) break;
    if (!SleepUntil(ms == INFINITE, ts, due)) ms = 0;
  }
  pthread_mutex_unlock(WaitLock());
  return n;
}
BOOL WINAPI GetQueuedCompletionStatus(HANDLE h, LPDWORD pBytes, PULONG_PTR pKey, LPOVERLAPPED* pOv, DWORD ms) {
  Packet pk;
  bool apc;
  if (pOv) *pOv = NULL;
  if (GetPackets(h, &pk, 1, ms, FALSE, &apc) == 0) {
    if (GetLastError() != ERROR_INVALID_HANDLE) SetLastError(WAIT_TIMEOUT);
    return FALSE;
  }
  if (pBytes) *pBytes = pk.bytes;
  if (pKey) *pKey = pk.key;
  if (pOv) *pOv = pk.ov;
  // A failed operation's packet comes back with its error.
  if (pk.error) {
    SetLastError(pk.error);
    return FALSE;
  }
  return TRUE;
}
BOOL WINAPI GetQueuedCompletionStatusEx(HANDLE h, LPOVERLAPPED_ENTRY entries, ULONG count, PULONG removed,
                                        DWORD ms, BOOL alertable) {
  if (removed) *removed = 0;
  if (entries == NULL || count == 0) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  std::vector<Packet> pk(count);
  bool apc;
  ULONG n = GetPackets(h, pk.data(), count, ms, alertable, &apc);
  if (n == 0) {
    if (apc) SetLastError(WAIT_IO_COMPLETION);
    else if (GetLastError() != ERROR_INVALID_HANDLE) SetLastError(WAIT_TIMEOUT);
    return FALSE;
  }
  for (ULONG i = 0; i < n; i++) {
    entries[i].lpCompletionKey = pk[i].key;
    entries[i].lpOverlapped = pk[i].ov;
    entries[i].Internal = pk[i].error ? 0xE0000000u | pk[i].error : 0;
    entries[i].dwNumberOfBytesTransferred = pk[i].bytes;
  }
  if (removed) *removed = n;
  return TRUE;
}

// Threads
static void* ThreadMain(void* p) {
  Thread* t = (Thread*)p;
  t_thread = t;
  t_threadId = t->id;
  pthread_mutex_lock(WaitLock());
  while (t->suspended) pthread_cond_wait(&t->resume, WaitLock());
  pthread_mutex_unlock(WaitLock());
  DWORD code = t->start(t->param);
  ExitThread(code);
}

HANDLE WINAPI CreateThread(LPSECURITY_ATTRIBUTES, SIZE_T dwStackSize, LPTHREAD_START_ROUTINE lpStartAddress,
                           LPVOID lpParameter, DWORD dwCreationFlags, LPDWORD lpThreadId) {
  Thread* t = new Thread();
  t->start = lpStartAddress;
  t->param = lpParameter;
  t->id = g_nextThreadId.fetch_add(1);
  t->suspended = (dwCreationFlags & CREATE_SUSPENDED) != 0;
  t->refs.fetch_add(1);  // the thread's own reference, dropped when it ends
  pthread_attr_t attr;
  pthread_attr_init(&attr);
  if (dwStackSize >= 65536) pthread_attr_setstacksize(&attr, dwStackSize);
  pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
  int e = pthread_create(&t->thread, &attr, ThreadMain, t);
  pthread_attr_destroy(&attr);
  if (e != 0) {
    t->refs = 1;
    Release(t);
    SetLastError(ErrorFromErrno(e));
    return NULL;
  }
  if (lpThreadId) *lpThreadId = t->id;
  return NewHandle(t);
}

void WINAPI ExitThread(DWORD dwExitCode) {
  Thread* t = t_thread;
  if (t != NULL) {
    pthread_mutex_lock(WaitLock());
    t->exitCode = dwExitCode;
    t->done = true;
    SignalChange();
    pthread_mutex_unlock(WaitLock());
    t_thread = NULL;
    Release(t);
  }
  pthread_exit((void*)(uintptr_t)dwExitCode);
}

BOOL WINAPI TerminateThread(HANDLE, DWORD) {
  SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
  return FALSE;
}

BOOL WINAPI GetExitCodeThread(HANDLE h, LPDWORD lpExitCode) {
  if (h == GetCurrentThread()) {
    if (lpExitCode) *lpExitCode = STILL_ACTIVE;
    return TRUE;
  }
  Thread* t = (Thread*)FromHandle(h, kThread);
  if (t == NULL) {
    SetLastError(ERROR_INVALID_HANDLE);
    return FALSE;
  }
  pthread_mutex_lock(WaitLock());
  if (lpExitCode) *lpExitCode = t->exitCode;
  pthread_mutex_unlock(WaitLock());
  return TRUE;
}

// Only threads made suspended can be resumed; other threads cannot be
// stopped from outside with POSIX threads.
DWORD WINAPI SuspendThread(HANDLE) {
  SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
  return (DWORD)-1;
}
DWORD WINAPI ResumeThread(HANDLE h) {
  Thread* t = (Thread*)FromHandle(h, kThread);
  if (t == NULL) {
    SetLastError(ERROR_INVALID_HANDLE);
    return (DWORD)-1;
  }
  pthread_mutex_lock(WaitLock());
  DWORD was = t->suspended ? 1 : 0;
  t->suspended = false;
  pthread_cond_signal(&t->resume);
  pthread_mutex_unlock(WaitLock());
  return was;
}

BOOL WINAPI SetThreadPriority(HANDLE h, int nPriority) {
  Thread* t = (Thread*)FromHandle(h == GetCurrentThread() && t_thread ? (HANDLE)t_thread : h, kThread);
  if (t) t->priority = nPriority;
  return TRUE;
}
int WINAPI GetThreadPriority(HANDLE h) {
  Thread* t = (Thread*)FromHandle(h == GetCurrentThread() && t_thread ? (HANDLE)t_thread : h, kThread);
  return t ? t->priority : THREAD_PRIORITY_NORMAL;
}

HANDLE WINAPI GetCurrentThread(void) { return (HANDLE)(LONG_PTR)-2; }
DWORD WINAPI GetCurrentThreadId(void) { return CurrentThreadId(); }
HANDLE WINAPI GetCurrentProcess(void) { return (HANDLE)(LONG_PTR)-1; }
DWORD WINAPI GetCurrentProcessId(void) { return (DWORD)getpid(); }

// Thread local storage
DWORD WINAPI TlsAlloc(void) {
  pthread_key_t k;
  if (pthread_key_create(&k, NULL) != 0) {
    SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return TLS_OUT_OF_INDEXES;
  }
  return (DWORD)k;
}
BOOL WINAPI TlsFree(DWORD i) { return pthread_key_delete((pthread_key_t)i) == 0; }
LPVOID WINAPI TlsGetValue(DWORD i) {
  SetLastError(0);
  return pthread_getspecific((pthread_key_t)i);
}
BOOL WINAPI TlsSetValue(DWORD i, LPVOID v) { return pthread_setspecific((pthread_key_t)i, v) == 0; }

// Critical sections: a recursive mutex inside the structure.
static_assert(sizeof(pthread_mutex_t) <= sizeof(((CRITICAL_SECTION*)0)->_Mutex), "CRITICAL_SECTION too small");
static pthread_mutex_t* CsMutex(LPCRITICAL_SECTION cs) { return (pthread_mutex_t*)cs->_Mutex; }
void WINAPI InitializeCriticalSection(LPCRITICAL_SECTION cs) {
  memset(cs, 0, sizeof *cs);
  cs->LockCount = -1;
  pthread_mutexattr_t a;
  pthread_mutexattr_init(&a);
  pthread_mutexattr_settype(&a, PTHREAD_MUTEX_RECURSIVE);
  pthread_mutex_init(CsMutex(cs), &a);
  pthread_mutexattr_destroy(&a);
}
BOOL WINAPI InitializeCriticalSectionAndSpinCount(LPCRITICAL_SECTION cs, DWORD n) {
  InitializeCriticalSection(cs);
  cs->SpinCount = n;
  return TRUE;
}
DWORD WINAPI SetCriticalSectionSpinCount(LPCRITICAL_SECTION cs, DWORD n) {
  DWORD old = (DWORD)cs->SpinCount;
  cs->SpinCount = n;
  return old;
}
void WINAPI EnterCriticalSection(LPCRITICAL_SECTION cs) {
  pthread_mutex_lock(CsMutex(cs));
  cs->OwningThread = (HANDLE)(ULONG_PTR)CurrentThreadId();
  cs->RecursionCount++;
}
BOOL WINAPI TryEnterCriticalSection(LPCRITICAL_SECTION cs) {
  if (pthread_mutex_trylock(CsMutex(cs)) != 0) return FALSE;
  cs->OwningThread = (HANDLE)(ULONG_PTR)CurrentThreadId();
  cs->RecursionCount++;
  return TRUE;
}
void WINAPI LeaveCriticalSection(LPCRITICAL_SECTION cs) {
  if (--cs->RecursionCount == 0) cs->OwningThread = NULL;
  pthread_mutex_unlock(CsMutex(cs));
}
void WINAPI DeleteCriticalSection(LPCRITICAL_SECTION cs) { pthread_mutex_destroy(CsMutex(cs)); }

// Time since start-up
static ULONGLONG MonotonicMs() {
#ifdef __APPLE__
  static mach_timebase_info_data_t tb;
  if (tb.denom == 0) mach_timebase_info(&tb);
  return mach_absolute_time() * tb.numer / tb.denom / 1000000;
#else
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (ULONGLONG)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
#endif
}
DWORD WINAPI GetTickCount(void) { return (DWORD)MonotonicMs(); }
ULONGLONG WINAPI GetTickCount64(void) { return MonotonicMs(); }
BOOL WINAPI QueryPerformanceFrequency(LARGE_INTEGER* f) {
  f->QuadPart = 1000000;
  return TRUE;
}
BOOL WINAPI QueryPerformanceCounter(LARGE_INTEGER* c) {
#ifdef __APPLE__
  static mach_timebase_info_data_t tb;
  if (tb.denom == 0) mach_timebase_info(&tb);
  c->QuadPart = (LONGLONG)(mach_absolute_time() * tb.numer / tb.denom / 1000);
#else
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  c->QuadPart = (LONGLONG)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
#endif
  return TRUE;
}

}  // extern "C"
