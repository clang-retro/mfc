// afxmt.h - thread synchronization objects.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#ifndef __AFXMT_H__
#define __AFXMT_H__

#include "afx.h"

class CSyncObject : public CObject {
  DECLARE_DYNAMIC(CSyncObject)
public:
  explicit CSyncObject(LPCTSTR pstrName) { (void)pstrName; m_hObject = NULL; }
  virtual ~CSyncObject() {
    if (m_hObject != NULL) ::CloseHandle(m_hObject);
  }
  operator HANDLE() const { return m_hObject; }
  virtual BOOL Lock(DWORD dwTimeout = INFINITE) {
    DWORD dw = ::WaitForSingleObject(m_hObject, dwTimeout);
    return dw == WAIT_OBJECT_0 || dw == WAIT_ABANDONED;
  }
  virtual BOOL Unlock() = 0;
  virtual BOOL Unlock(LONG lCount, LPLONG lpPrevCount = NULL) {
    (void)lCount;
    (void)lpPrevCount;
    return TRUE;
  }
  HANDLE m_hObject;
};

class CSemaphore : public CSyncObject {
  DECLARE_DYNAMIC(CSemaphore)
public:
  CSemaphore(LONG lInitialCount = 1, LONG lMaxCount = 1, LPCTSTR pstrName = NULL,
             LPSECURITY_ATTRIBUTES lpsaAttributes = NULL)
      : CSyncObject(pstrName) {
    m_hObject = ::CreateSemaphore(lpsaAttributes, lInitialCount, lMaxCount, pstrName);
    if (m_hObject == NULL) AfxThrowResourceException();
  }
  virtual BOOL Unlock() { return Unlock(1, NULL); }
  virtual BOOL Unlock(LONG lCount, LPLONG lprevCount = NULL) {
    return ::ReleaseSemaphore(m_hObject, lCount, lprevCount);
  }
};

class CMutex : public CSyncObject {
  DECLARE_DYNAMIC(CMutex)
public:
  CMutex(BOOL bInitiallyOwn = FALSE, LPCTSTR lpszName = NULL,
         LPSECURITY_ATTRIBUTES lpsaAttribute = NULL)
      : CSyncObject(lpszName) {
    m_hObject = ::CreateMutex(lpsaAttribute, bInitiallyOwn, lpszName);
    if (m_hObject == NULL) AfxThrowResourceException();
  }
  virtual BOOL Unlock() { return ::ReleaseMutex(m_hObject); }
};

class CEvent : public CSyncObject {
  DECLARE_DYNAMIC(CEvent)
public:
  CEvent(BOOL bInitiallyOwn = FALSE, BOOL bManualReset = FALSE, LPCTSTR lpszName = NULL,
         LPSECURITY_ATTRIBUTES lpsaAttribute = NULL)
      : CSyncObject(lpszName) {
    m_hObject = ::CreateEvent(lpsaAttribute, bManualReset, bInitiallyOwn, lpszName);
    if (m_hObject == NULL) AfxThrowResourceException();
  }
  BOOL SetEvent() { return ::SetEvent(m_hObject); }
  BOOL PulseEvent() { return ::PulseEvent(m_hObject); }
  BOOL ResetEvent() { return ::ResetEvent(m_hObject); }
  virtual BOOL Unlock() { return TRUE; }
};

class CCriticalSection : public CSyncObject {
  DECLARE_DYNAMIC(CCriticalSection)
public:
  CCriticalSection() : CSyncObject(NULL) { ::InitializeCriticalSection(&m_sect); }
  virtual ~CCriticalSection() { ::DeleteCriticalSection(&m_sect); }
  operator CRITICAL_SECTION*() { return &m_sect; }
  BOOL Lock() {
    ::EnterCriticalSection(&m_sect);
    return TRUE;
  }
  virtual BOOL Lock(DWORD dwTimeout) {
    (void)dwTimeout;
    return Lock();
  }
  virtual BOOL Unlock() {
    ::LeaveCriticalSection(&m_sect);
    return TRUE;
  }
  CRITICAL_SECTION m_sect;
};

class CSingleLock {
public:
  explicit CSingleLock(CSyncObject* pObject, BOOL bInitialLock = FALSE)
      : m_pObject(pObject), m_hObject(pObject->m_hObject), m_bAcquired(FALSE) {
    if (bInitialLock) Lock();
  }
  ~CSingleLock() { Unlock(); }
  BOOL Lock(DWORD dwTimeOut = INFINITE) {
    m_bAcquired = m_pObject->Lock(dwTimeOut);
    return m_bAcquired;
  }
  BOOL Unlock() {
    if (m_bAcquired) m_bAcquired = !m_pObject->Unlock();
    return !m_bAcquired;
  }
  BOOL Unlock(LONG lCount, LPLONG lPrevCount = NULL) {
    if (m_bAcquired) m_bAcquired = !m_pObject->Unlock(lCount, lPrevCount);
    return !m_bAcquired;
  }
  BOOL IsLocked() { return m_bAcquired; }

protected:
  CSyncObject* m_pObject;
  HANDLE m_hObject;
  BOOL m_bAcquired;
};

class CMultiLock {
public:
  CMultiLock(CSyncObject* ppObjects[], DWORD dwCount, BOOL bInitialLock = FALSE)
      : m_ppObjectArray(ppObjects), m_dwCount(dwCount) {
    m_pHandleArray = new HANDLE[dwCount];
    m_bLockedArray = new BOOL[dwCount];
    for (DWORD i = 0; i < dwCount; i++) {
      m_pHandleArray[i] = ppObjects[i]->m_hObject;
      m_bLockedArray[i] = FALSE;
    }
    if (bInitialLock) Lock();
  }
  ~CMultiLock() {
    Unlock();
    delete[] m_pHandleArray;
    delete[] m_bLockedArray;
  }
  DWORD Lock(DWORD dwTimeOut = INFINITE, BOOL bWaitForAll = TRUE, DWORD dwWakeMask = 0) {
    DWORD dwResult =
        dwWakeMask == 0
            ? ::WaitForMultipleObjects(m_dwCount, m_pHandleArray, bWaitForAll, dwTimeOut)
            : ::MsgWaitForMultipleObjects(m_dwCount, m_pHandleArray, bWaitForAll, dwTimeOut,
                                          dwWakeMask);
    DWORD dwUpper = WAIT_OBJECT_0 + m_dwCount;
    if (dwResult >= WAIT_OBJECT_0 && dwResult < dwUpper) {
      if (bWaitForAll)
        for (DWORD i = 0; i < m_dwCount; i++) m_bLockedArray[i] = TRUE;
      else
        m_bLockedArray[dwResult - WAIT_OBJECT_0] = TRUE;
    }
    return dwResult;
  }
  BOOL Unlock() {
    for (DWORD i = 0; i < m_dwCount; i++)
      if (m_bLockedArray[i]) m_bLockedArray[i] = !m_ppObjectArray[i]->Unlock();
    return TRUE;
  }
  BOOL Unlock(LONG lCount, LPLONG lPrevCount = NULL) {
    BOOL bGotOne = FALSE;
    for (DWORD i = 0; i < m_dwCount; i++)
      if (m_bLockedArray[i] && m_ppObjectArray[i]->IsKindOf(RUNTIME_CLASS(CSemaphore))) {
        m_bLockedArray[i] = !m_ppObjectArray[i]->Unlock(lCount, lPrevCount);
        bGotOne = TRUE;
      }
    return bGotOne;
  }
  BOOL IsLocked(DWORD dwItem) { return dwItem < m_dwCount && m_bLockedArray[dwItem]; }

protected:
  CSyncObject** m_ppObjectArray;
  HANDLE* m_pHandleArray;
  BOOL* m_bLockedArray;
  DWORD m_dwCount;
};

#endif  // __AFXMT_H__
