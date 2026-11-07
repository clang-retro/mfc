// kernel_sync.cpp - kernel objects and handles, events, mutexes,
// semaphores, waits, threads, thread local storage and critical
// sections, over POSIX threads.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include "winport.h"

#include <errno.h>
#include <sched.h>
#include <signal.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
#include <new>
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
void Release(Object* o) {
  if (o->refs.fetch_sub(1) == 1) {
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

std::atomic<DWORD> g_nextThreadId{1};
thread_local DWORD t_threadId = 0;
thread_local Thread* t_thread = NULL;
thread_local DWORD t_lastError = 0;

}  // namespace

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
HANDLE WINAPI CreateEventW(LPSECURITY_ATTRIBUTES, BOOL bManualReset, BOOL bInitialState, LPCWSTR) {
  SetLastError(0);
  return NewHandle(new Event(bManualReset != 0, bInitialState != 0));
}
HANDLE WINAPI CreateEventA(LPSECURITY_ATTRIBUTES sa, BOOL m, BOOL s, LPCSTR) {
  return CreateEventW(sa, m, s, NULL);
}
HANDLE WINAPI OpenEventA(DWORD, BOOL, LPCSTR) {
  SetLastError(ERROR_FILE_NOT_FOUND);
  return NULL;
}
HANDLE WINAPI OpenEventW(DWORD, BOOL, LPCWSTR) {
  SetLastError(ERROR_FILE_NOT_FOUND);
  return NULL;
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

HANDLE WINAPI CreateMutexW(LPSECURITY_ATTRIBUTES, BOOL bInitialOwner, LPCWSTR) {
  Mutex* m = new Mutex();
  if (bInitialOwner) m->Acquire(CurrentThreadId());
  SetLastError(0);
  return NewHandle(m);
}
HANDLE WINAPI CreateMutexA(LPSECURITY_ATTRIBUTES sa, BOOL b, LPCSTR) { return CreateMutexW(sa, b, NULL); }
HANDLE WINAPI OpenMutexA(DWORD, BOOL, LPCSTR) {
  SetLastError(ERROR_FILE_NOT_FOUND);
  return NULL;
}
HANDLE WINAPI OpenMutexW(DWORD, BOOL, LPCWSTR) {
  SetLastError(ERROR_FILE_NOT_FOUND);
  return NULL;
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

HANDLE WINAPI CreateSemaphoreW(LPSECURITY_ATTRIBUTES, LONG lInitialCount, LONG lMaximumCount, LPCWSTR) {
  if (lMaximumCount <= 0 || lInitialCount < 0 || lInitialCount > lMaximumCount) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return NULL;
  }
  SetLastError(0);
  return NewHandle(new Semaphore(lInitialCount, lMaximumCount));
}
HANDLE WINAPI CreateSemaphoreA(LPSECURITY_ATTRIBUTES sa, LONG i, LONG m, LPCSTR) {
  return CreateSemaphoreW(sa, i, m, NULL);
}
HANDLE WINAPI OpenSemaphoreA(DWORD, BOOL, LPCSTR) {
  SetLastError(ERROR_FILE_NOT_FOUND);
  return NULL;
}
HANDLE WINAPI OpenSemaphoreW(DWORD, BOOL, LPCWSTR) {
  SetLastError(ERROR_FILE_NOT_FOUND);
  return NULL;
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

DWORD WINAPI WaitForMultipleObjectsEx(DWORD nCount, const HANDLE* lpHandles, BOOL bWaitAll, DWORD dwMilliseconds,
                                      BOOL bAlertable) {
  (void)bAlertable;
  if (nCount == 0 || nCount > MAXIMUM_WAIT_OBJECTS) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return WAIT_FAILED;
  }
  std::vector<Object*> objs(nCount);
  for (DWORD i = 0; i < nCount; i++) {
    HANDLE h = lpHandles[i];
    if (h == GetCurrentThread() && t_thread) h = (HANDLE)t_thread;
    objs[i] = FromHandle(h);
    if (objs[i] == NULL || !objs[i]->Waitable()) {
      SetLastError(ERROR_INVALID_HANDLE);
      return WAIT_FAILED;
    }
  }
  DWORD tid = CurrentThreadId();
  struct timespec ts;
  if (dwMilliseconds != INFINITE) Deadline(dwMilliseconds, &ts);
  pthread_mutex_lock(WaitLock());
  DWORD result = WAIT_TIMEOUT;
  for (;;) {
    if (bWaitAll) {
      bool all = true;
      for (DWORD i = 0; i < nCount; i++) all = all && objs[i]->IsSignaled(tid);
      if (all) {
        for (DWORD i = 0; i < nCount; i++) objs[i]->Acquire(tid);
        result = WAIT_OBJECT_0;
        break;
      }
    } else {
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
    if (dwMilliseconds == INFINITE) {
      pthread_cond_wait(&g_waitCond, WaitLock());
    } else if (pthread_cond_timedwait(&g_waitCond, WaitLock(), &ts) == ETIMEDOUT) {
      dwMilliseconds = 0;  // one last look, then time out
    }
  }
  pthread_mutex_unlock(WaitLock());
  return result;
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
DWORD WINAPI SleepEx(DWORD ms, BOOL) {
  Sleep(ms);
  return 0;
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
