// atlsync.h - ATL's synchronization objects: CCriticalSection, CEvent,
// CMutex and CSemaphore over the kernel objects, and CMutexLock. They are
// in namespace ATL only, apart from MFC's classes of the same names in
// afxmt.h; with both headers, name them ATL::CEvent and so on.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#ifndef __ATLSYNC_H__
#define __ATLSYNC_H__

#include <atlbase.h>

namespace ATL {

class CCriticalSection : public CRITICAL_SECTION {
public:
  CCriticalSection() { ::InitializeCriticalSection(this); }
  explicit CCriticalSection(ULONG nSpinCount) {
    if (!::InitializeCriticalSectionAndSpinCount(this, nSpinCount)) AtlThrowLastWin32();
  }
  ~CCriticalSection() { ::DeleteCriticalSection(this); }

  void Enter() { ::EnterCriticalSection(this); }
  void Leave() { ::LeaveCriticalSection(this); }
  BOOL TryEnter() { return ::TryEnterCriticalSection(this); }
  ULONG SetSpinCount(ULONG nSpinCount) { return ::SetCriticalSectionSpinCount(this, nSpinCount); }

private:
  CCriticalSection(const CCriticalSection&);
  CCriticalSection& operator=(const CCriticalSection&);
};

class CEvent : public CHandle {
public:
  CEvent() {}
  CEvent(CEvent& h) : CHandle(h) {}
  explicit CEvent(HANDLE h) : CHandle(h) {}
  CEvent(BOOL bManualReset, BOOL bInitialState) {
    if (!Create(NULL, bManualReset, bInitialState, NULL)) AtlThrowLastWin32();
  }
  CEvent(LPSECURITY_ATTRIBUTES pSecurity, BOOL bManualReset, BOOL bInitialState,
         LPCTSTR pszName) {
    if (!Create(pSecurity, bManualReset, bInitialState, pszName)) AtlThrowLastWin32();
  }

  BOOL Create(LPSECURITY_ATTRIBUTES pSecurity, BOOL bManualReset, BOOL bInitialState,
              LPCTSTR pszName) {
    Attach(::CreateEvent(pSecurity, bManualReset, bInitialState, pszName));
    return m_h != NULL;
  }
  BOOL Open(DWORD dwAccess, BOOL bInheritHandle, LPCTSTR pszName) {
    Attach(::OpenEvent(dwAccess, bInheritHandle, pszName));
    return m_h != NULL;
  }
  BOOL Pulse() { return ::PulseEvent(m_h); }
  BOOL Reset() { return ::ResetEvent(m_h); }
  BOOL Set() { return ::SetEvent(m_h); }
};

class CMutex : public CHandle {
public:
  CMutex() {}
  CMutex(CMutex& h) : CHandle(h) {}
  explicit CMutex(HANDLE h) : CHandle(h) {}
  explicit CMutex(BOOL bInitialOwner) {
    if (!Create(NULL, bInitialOwner, NULL)) AtlThrowLastWin32();
  }
  CMutex(LPSECURITY_ATTRIBUTES pSecurity, BOOL bInitialOwner, LPCTSTR pszName) {
    if (!Create(pSecurity, bInitialOwner, pszName)) AtlThrowLastWin32();
  }

  BOOL Create(LPSECURITY_ATTRIBUTES pSecurity, BOOL bInitialOwner, LPCTSTR pszName) {
    Attach(::CreateMutex(pSecurity, bInitialOwner, pszName));
    return m_h != NULL;
  }
  BOOL Open(DWORD dwAccess, BOOL bInheritHandle, LPCTSTR pszName) {
    Attach(::OpenMutex(dwAccess, bInheritHandle, pszName));
    return m_h != NULL;
  }
  BOOL Release() { return ::ReleaseMutex(m_h); }
};

class CSemaphore : public CHandle {
public:
  CSemaphore() {}
  CSemaphore(CSemaphore& h) : CHandle(h) {}
  explicit CSemaphore(HANDLE h) : CHandle(h) {}
  CSemaphore(LONG nInitialCount, LONG nMaxCount) {
    if (!Create(NULL, nInitialCount, nMaxCount, NULL)) AtlThrowLastWin32();
  }
  CSemaphore(LPSECURITY_ATTRIBUTES pSecurity, LONG nInitialCount, LONG nMaxCount,
             LPCTSTR pszName) {
    if (!Create(pSecurity, nInitialCount, nMaxCount, pszName)) AtlThrowLastWin32();
  }

  BOOL Create(LPSECURITY_ATTRIBUTES pSecurity, LONG nInitialCount, LONG nMaxCount,
              LPCTSTR pszName) {
    Attach(::CreateSemaphore(pSecurity, nInitialCount, nMaxCount, pszName));
    return m_h != NULL;
  }
  BOOL Open(DWORD dwAccess, BOOL bInheritHandle, LPCTSTR pszName) {
    Attach(::OpenSemaphore(dwAccess, bInheritHandle, pszName));
    return m_h != NULL;
  }
  BOOL Release(LONG nReleaseCount = 1, LONG* pnOldCount = NULL) {
    return ::ReleaseSemaphore(m_h, nReleaseCount, pnOldCount);
  }
};

// Holds a mutex for its lifetime.
class CMutexLock {
public:
  CMutexLock(CMutex& mtx, bool bInitialLock = true) : m_mtx(mtx), m_bLocked(false) {
    if (bInitialLock) Lock();
  }
  ~CMutexLock() {
    if (m_bLocked) Unlock();
  }
  void Lock() {
    DWORD dw = ::WaitForSingleObject(m_mtx, INFINITE);
    if (dw == WAIT_FAILED) AtlThrowLastWin32();
    m_bLocked = true;  // WAIT_ABANDONED still owns it
  }
  void Unlock() {
    m_mtx.Release();
    m_bLocked = false;
  }

private:
  CMutex& m_mtx;
  bool m_bLocked;
  CMutexLock(const CMutexLock&);
  CMutexLock& operator=(const CMutexLock&);
};

}  // namespace ATL

#endif  // __ATLSYNC_H__
