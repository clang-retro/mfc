// atlbase.h - the base of the Active Template Library: threading models
// and critical sections, the module classes (CComModule of ATL 3, and
// CAtlModule, CAtlDllModuleT and CAtlExeModuleT of ATL 7) with their object
// maps, class registration and the registrar that runs .rgs scripts,
// CRegKey, and the COM helpers of atlcomcli.h and atlconv.h.
//
// Everything is in the headers: there is no atl.dll and no atlimpl.cpp to
// build (an #include <atlimpl.cpp> finds an empty file).
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#ifndef __ATLBASE_H__
#define __ATLBASE_H__

#ifndef _ATL_VER
#define _ATL_VER 0x0300
#endif

#include <windows.h>
#include <ole2.h>
#include <oleauto.h>
#include <olectl.h>
#include <ocidl.h>
#include <comcat.h>
#include <limits.h>
#include <stddef.h>
#include <tchar.h>
#include <string.h>
#include <wchar.h>

#include "atlexcept.h"
#include "atlalloc.h"
#include "atlcomcli.h"
#include "atlsimpcoll.h"

// Link the system libraries ATL uses, as Visual C++'s atlbase.h does,
// unless _ATL_NO_DEFAULT_LIBS is defined. (clang ignores #pragma
// comment(lib) for MinGW targets, so the linker directives are written
// directly.)
#if !defined(_ATL_NO_DEFAULT_LIBS) && defined(_WIN32)
__asm__(".section .drectve,\"yn\"\n"
        ".ascii \" -defaultlib:kernel32 -defaultlib:user32 -defaultlib:advapi32"
        " -defaultlib:ole32 -defaultlib:oleaut32 -defaultlib:uuid -defaultlib:shell32\"\n"
        ".text");
#endif
#include "atlconv.h"

#ifndef ATLASSERT
#define ATLASSERT(expr) ((void)0)
#endif
#ifndef ATLTRACE
#define ATLTRACE(...) ((void)0)
#endif
#ifndef ATLTRACE2
#define ATLTRACE2(...) ((void)0)
#endif
#define ATLTRACENOTIMPL(funcname) return E_NOTIMPL
#ifndef ATL_NO_VTABLE
#define ATL_NO_VTABLE
#endif
#ifndef ATLAPI
#define ATLAPI inline HRESULT __stdcall
#define ATLAPI_(x) inline x __stdcall
#endif
#ifndef _ATL_PACKING
#define _ATL_PACKING 8
#endif
#ifndef ATL_DEPRECATED
#define ATL_DEPRECATED
#endif

// The threading model the objects and the module get when nothing says.
#if !defined(_ATL_SINGLE_THREADED) && !defined(_ATL_APARTMENT_THREADED) && \
    !defined(_ATL_FREE_THREADED)
#define _ATL_APARTMENT_THREADED
#endif

// The image that holds this code (the DLL, or the program).
#ifdef _WINPORT
// The layer finds it from an address in it: this function's own static,
// one in each image as the function has internal linkage.
static inline HINSTANCE _AtlThisImage() {
  static const char marker = 0;
  HMODULE h = NULL;
  ::GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                           GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       &marker, &h);
  return (HINSTANCE)h;
}
#else
extern "C" IMAGE_DOS_HEADER __ImageBase;
static inline HINSTANCE _AtlThisImage() { return (HINSTANCE)&__ImageBase; }
#endif

namespace ATL {

// offsetofclass(base, derived): where base lies in derived.
#define _ATL_OFFSET_BASE 8
#define offsetofclass(base, derived) \
  ((DWORD_PTR)(static_cast<base*>((derived*)_ATL_OFFSET_BASE)) - _ATL_OFFSET_BASE)
#define offsetofclass2(base, branch, derived)                                       \
  ((DWORD_PTR)(static_cast<base*>(static_cast<branch*>((derived*)_ATL_OFFSET_BASE))) - \
   _ATL_OFFSET_BASE)

// ---------------------------------------------------------------------
// Critical sections and threading models.

class CComCriticalSection {
public:
  CRITICAL_SECTION m_sec;
  CComCriticalSection() { memset(&m_sec, 0, sizeof(m_sec)); }
  HRESULT Lock() {
    ::EnterCriticalSection(&m_sec);
    return S_OK;
  }
  HRESULT Unlock() {
    ::LeaveCriticalSection(&m_sec);
    return S_OK;
  }
  HRESULT Init() {
    ::InitializeCriticalSection(&m_sec);
    return S_OK;
  }
  HRESULT Term() {
    ::DeleteCriticalSection(&m_sec);
    return S_OK;
  }
};

class CComAutoCriticalSection : public CComCriticalSection {
public:
  CComAutoCriticalSection() { CComCriticalSection::Init(); }
  ~CComAutoCriticalSection() { CComCriticalSection::Term(); }

private:
  HRESULT Init();  // done by the constructor
  HRESULT Term();  // and the destructor
};

class CComSafeDeleteCriticalSection : public CComCriticalSection {
public:
  CComSafeDeleteCriticalSection() : m_bInitialized(false) {}
  ~CComSafeDeleteCriticalSection() { Term(); }
  HRESULT Init() {
    HRESULT hr = CComCriticalSection::Init();
    m_bInitialized = SUCCEEDED(hr);
    return hr;
  }
  HRESULT Term() {
    if (!m_bInitialized) return S_OK;
    m_bInitialized = false;
    return CComCriticalSection::Term();
  }
  HRESULT Lock() { return m_bInitialized ? CComCriticalSection::Lock() : S_OK; }

private:
  bool m_bInitialized;
};

class CComAutoDeleteCriticalSection : public CComSafeDeleteCriticalSection {};

class CComFakeCriticalSection {
public:
  HRESULT Lock() { return S_OK; }
  HRESULT Unlock() { return S_OK; }
  HRESULT Init() { return S_OK; }
  HRESULT Term() { return S_OK; }
};

template <class TLock>
class CComCritSecLock {
public:
  CComCritSecLock(TLock& cs, bool bInitialLock = true) : m_cs(cs), m_bLocked(false) {
    if (bInitialLock) Lock();
  }
  ~CComCritSecLock() {
    if (m_bLocked) Unlock();
  }
  HRESULT Lock() {
    HRESULT hr = m_cs.Lock();
    if (SUCCEEDED(hr)) m_bLocked = true;
    return hr;
  }
  void Unlock() {
    m_cs.Unlock();
    m_bLocked = false;
  }

private:
  TLock& m_cs;
  bool m_bLocked;
  CComCritSecLock(const CComCritSecLock&);
  CComCritSecLock& operator=(const CComCritSecLock&);
};

class CComMultiThreadModelNoCS {
public:
  static ULONG WINAPI Increment(LONG* p) { return (ULONG)::InterlockedIncrement(p); }
  static ULONG WINAPI Decrement(LONG* p) { return (ULONG)::InterlockedDecrement(p); }
  typedef CComFakeCriticalSection AutoCriticalSection;
  typedef CComFakeCriticalSection AutoDeleteCriticalSection;
  typedef CComFakeCriticalSection CriticalSection;
  typedef CComMultiThreadModelNoCS ThreadModelNoCS;
};

class CComMultiThreadModel {
public:
  static ULONG WINAPI Increment(LONG* p) { return (ULONG)::InterlockedIncrement(p); }
  static ULONG WINAPI Decrement(LONG* p) { return (ULONG)::InterlockedDecrement(p); }
  typedef CComAutoCriticalSection AutoCriticalSection;
  typedef CComAutoDeleteCriticalSection AutoDeleteCriticalSection;
  typedef CComCriticalSection CriticalSection;
  typedef CComMultiThreadModelNoCS ThreadModelNoCS;
};

class CComSingleThreadModel {
public:
  static ULONG WINAPI Increment(LONG* p) { return (ULONG)++(*p); }
  static ULONG WINAPI Decrement(LONG* p) { return (ULONG)--(*p); }
  typedef CComFakeCriticalSection AutoCriticalSection;
  typedef CComFakeCriticalSection AutoDeleteCriticalSection;
  typedef CComFakeCriticalSection CriticalSection;
  typedef CComSingleThreadModel ThreadModelNoCS;
};

#if defined(_ATL_SINGLE_THREADED)
typedef CComSingleThreadModel CComObjectThreadModel;
typedef CComSingleThreadModel CComGlobalsThreadModel;
#elif defined(_ATL_APARTMENT_THREADED)
typedef CComSingleThreadModel CComObjectThreadModel;
typedef CComMultiThreadModel CComGlobalsThreadModel;
#else
typedef CComMultiThreadModel CComObjectThreadModel;
typedef CComMultiThreadModel CComGlobalsThreadModel;
#endif

// ---------------------------------------------------------------------
// The interface maps (atlcom.h's BEGIN_COM_MAP) and the object maps.

typedef HRESULT(WINAPI _ATL_CREATORFUNC)(void* pv, REFIID riid, LPVOID* ppv);
typedef HRESULT(WINAPI _ATL_CREATORARGFUNC)(void* pv, REFIID riid, LPVOID* ppv, DWORD_PTR dw);
typedef LPCTSTR(WINAPI _ATL_DESCRIPTIONFUNC)();

struct _ATL_CATMAP_ENTRY {
  int iType;
  const CATID* pcatid;
};
#define _ATL_CATMAP_ENTRY_END 0
#define _ATL_CATMAP_ENTRY_IMPLEMENTED 1
#define _ATL_CATMAP_ENTRY_REQUIRED 2
typedef const _ATL_CATMAP_ENTRY*(_ATL_CATMAPFUNC)();

struct _ATL_INTMAP_ENTRY {
  const IID* piid;  // the interface, or NULL for a blind entry
  DWORD_PTR dw;
  _ATL_CREATORARGFUNC* pFunc;  // _ATL_SIMPLEMAPENTRY: dw is an offset
};
#define _ATL_SIMPLEMAPENTRY ((ATL::_ATL_CREATORARGFUNC*)1)

struct _ATL_OBJMAP_ENTRY {
  const CLSID* pclsid;
  HRESULT(WINAPI* pfnUpdateRegistry)(BOOL bRegister);
  _ATL_CREATORFUNC* pfnGetClassObject;
  _ATL_CREATORFUNC* pfnCreateInstance;
  IUnknown* pCF;
  DWORD dwRegister;
  _ATL_DESCRIPTIONFUNC* pfnGetObjectDescription;
  _ATL_CATMAPFUNC* pfnGetCategoryMap;
  void(WINAPI* pfnObjectMain)(bool bStarting);

  HRESULT WINAPI RevokeClassObject() {
    if (dwRegister == 0) return S_OK;
    HRESULT hr = ::CoRevokeClassObject(dwRegister);
    dwRegister = 0;
    return hr;
  }
  HRESULT WINAPI RegisterClassObject(DWORD dwClsContext, DWORD dwFlags) {
    IUnknown* p = NULL;
    if (pfnGetClassObject == NULL) return S_OK;
    HRESULT hr = pfnGetClassObject((void*)pfnCreateInstance, IID_IUnknown, (LPVOID*)&p);
    if (SUCCEEDED(hr)) hr = ::CoRegisterClassObject(*pclsid, p, dwClsContext, dwFlags, &dwRegister);
    if (p != NULL) p->Release();
    return hr;
  }
};
typedef _ATL_OBJMAP_ENTRY _ATL_OBJMAP_ENTRY30;

// The replacements a .rgs script may use besides %MODULE%.
struct _ATL_REGMAP_ENTRY {
  LPCOLESTR szKey;
  LPCOLESTR szData;
};

// The interfaces an object's map answers for; its QueryInterface walks it.
inline HRESULT WINAPI AtlInternalQueryInterface(void* pThis, const _ATL_INTMAP_ENTRY* pEntries,
                                                REFIID iid, void** ppvObject) {
  if (ppvObject == NULL) return E_POINTER;
  *ppvObject = NULL;
  if (pThis == NULL || pEntries == NULL) return E_INVALIDARG;
  if (InlineIsEqualGUID(iid, IID_IUnknown)) {
    // The first entry is a plain one: its interface is the object's
    // identity.
    IUnknown* pUnk = (IUnknown*)((DWORD_PTR)pThis + pEntries->dw);
    pUnk->AddRef();
    *ppvObject = pUnk;
    return S_OK;
  }
  for (; pEntries->pFunc != NULL; pEntries++) {
    BOOL bBlind = pEntries->piid == NULL;
    if (bBlind || InlineIsEqualGUID(*(pEntries->piid), iid)) {
      if (pEntries->pFunc == _ATL_SIMPLEMAPENTRY) {
        IUnknown* pUnk = (IUnknown*)((DWORD_PTR)pThis + pEntries->dw);
        pUnk->AddRef();
        *ppvObject = pUnk;
        return S_OK;
      }
      HRESULT hr = pEntries->pFunc(pThis, iid, ppvObject, pEntries->dw);
      if (hr == S_OK || (!bBlind && FAILED(hr))) return hr;
    }
  }
  return E_NOINTERFACE;
}

// ---------------------------------------------------------------------
// Connections.

inline HRESULT WINAPI AtlAdvise(IUnknown* pUnkCP, IUnknown* pUnk, const IID& iid, LPDWORD pdw) {
  if (pUnkCP == NULL) return E_INVALIDARG;
  IConnectionPointContainer* pCPC = NULL;
  IConnectionPoint* pCP = NULL;
  HRESULT hr = pUnkCP->QueryInterface(IID_IConnectionPointContainer, (void**)&pCPC);
  if (SUCCEEDED(hr)) hr = pCPC->FindConnectionPoint(iid, &pCP);
  if (SUCCEEDED(hr)) hr = pCP->Advise(pUnk, pdw);
  if (pCP != NULL) pCP->Release();
  if (pCPC != NULL) pCPC->Release();
  return hr;
}

inline HRESULT WINAPI AtlUnadvise(IUnknown* pUnkCP, const IID& iid, DWORD dw) {
  if (pUnkCP == NULL) return E_INVALIDARG;
  IConnectionPointContainer* pCPC = NULL;
  IConnectionPoint* pCP = NULL;
  HRESULT hr = pUnkCP->QueryInterface(IID_IConnectionPointContainer, (void**)&pCPC);
  if (SUCCEEDED(hr)) hr = pCPC->FindConnectionPoint(iid, &pCP);
  if (SUCCEEDED(hr)) hr = pCP->Unadvise(dw);
  if (pCP != NULL) pCP->Release();
  if (pCPC != NULL) pCPC->Release();
  return hr;
}

template <class T>
inline HRESULT CComPtr<T>::Advise(IUnknown* pUnk, const IID& iid, LPDWORD pdw) {
  return AtlAdvise(p, pUnk, iid, pdw);
}

inline HRESULT WINAPI AtlFreeMarshalStream(IStream* pStream) {
  if (pStream != NULL) {
    LARGE_INTEGER l;
    l.QuadPart = 0;
    pStream->Seek(l, STREAM_SEEK_SET, NULL);
    ::CoReleaseMarshalData(pStream);
    pStream->Release();
  }
  return S_OK;
}

inline HRESULT WINAPI AtlMarshalPtrInProc(IUnknown* pUnk, const IID& iid, IStream** ppStream) {
  HRESULT hr = ::CreateStreamOnHGlobal(NULL, TRUE, ppStream);
  if (SUCCEEDED(hr)) {
    hr = ::CoMarshalInterface(*ppStream, iid, pUnk, MSHCTX_INPROC, NULL, MSHLFLAGS_TABLESTRONG);
    if (FAILED(hr)) {
      (*ppStream)->Release();
      *ppStream = NULL;
    }
  }
  return hr;
}

inline HRESULT WINAPI AtlUnmarshalPtr(IStream* pStream, const IID& iid, IUnknown** ppUnk) {
  *ppUnk = NULL;
  if (pStream == NULL) return E_INVALIDARG;
  LARGE_INTEGER l;
  l.QuadPart = 0;
  pStream->Seek(l, STREAM_SEEK_SET, NULL);
  return ::CoUnmarshalInterface(pStream, iid, (void**)ppUnk);
}

inline BOOL WINAPI AtlWaitWithMessageLoop(HANDLE hEvent) {
  for (;;) {
    DWORD dwRet = ::MsgWaitForMultipleObjects(1, &hEvent, FALSE, INFINITE, QS_ALLINPUT);
    if (dwRet == WAIT_OBJECT_0) return TRUE;
    if (dwRet != WAIT_OBJECT_0 + 1) return FALSE;
    MSG msg;
    while (::PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
      ::TranslateMessage(&msg);
      ::DispatchMessage(&msg);
      if (::WaitForSingleObject(hEvent, 0) == WAIT_OBJECT_0) return TRUE;
    }
  }
}

// ---------------------------------------------------------------------
// CRegKey.

class CRegKey {
public:
  HKEY m_hKey;

  CRegKey() : m_hKey(NULL) {}
  CRegKey(HKEY hKey) : m_hKey(hKey) {}
  CRegKey(CRegKey& key) : m_hKey(NULL) { Attach(key.Detach()); }
  ~CRegKey() { Close(); }
  CRegKey& operator=(CRegKey& key) {
    if (m_hKey != key.m_hKey) {
      Close();
      Attach(key.Detach());
    }
    return *this;
  }

  operator HKEY() const { return m_hKey; }

  LONG Create(HKEY hKeyParent, LPCTSTR lpszKeyName, LPTSTR lpszClass = (LPTSTR)REG_NONE,
              DWORD dwOptions = REG_OPTION_NON_VOLATILE, REGSAM samDesired = KEY_ALL_ACCESS,
              LPSECURITY_ATTRIBUTES lpSecAttr = NULL, LPDWORD lpdwDisposition = NULL) {
    HKEY hKey = NULL;
    DWORD dw;
    LONG lRes = ::RegCreateKeyEx(hKeyParent, lpszKeyName, 0, lpszClass, dwOptions, samDesired,
                                 lpSecAttr, &hKey, &dw);
    if (lpdwDisposition != NULL) *lpdwDisposition = dw;
    if (lRes == ERROR_SUCCESS) {
      Close();
      m_hKey = hKey;
    }
    return lRes;
  }
  LONG Open(HKEY hKeyParent, LPCTSTR lpszKeyName, REGSAM samDesired = KEY_ALL_ACCESS) {
    HKEY hKey = NULL;
    LONG lRes = ::RegOpenKeyEx(hKeyParent, lpszKeyName, 0, samDesired, &hKey);
    if (lRes == ERROR_SUCCESS) {
      Close();
      m_hKey = hKey;
    }
    return lRes;
  }
  LONG Close() {
    LONG lRes = ERROR_SUCCESS;
    if (m_hKey != NULL) {
      lRes = ::RegCloseKey(m_hKey);
      m_hKey = NULL;
    }
    return lRes;
  }
  void Attach(HKEY hKey) {
    Close();
    m_hKey = hKey;
  }
  HKEY Detach() {
    HKEY hKey = m_hKey;
    m_hKey = NULL;
    return hKey;
  }
  LONG Flush() { return ::RegFlushKey(m_hKey); }

  // Values.
  LONG SetValue(DWORD dwValue, LPCTSTR lpszValueName) {
    return SetDWORDValue(lpszValueName, dwValue);
  }
  LONG SetValue(LPCTSTR lpszValue, LPCTSTR lpszValueName = NULL, bool bMulti = false,
                int nValueLen = -1) {
    if (lpszValue == NULL) return ERROR_INVALID_PARAMETER;
    if (bMulti && nValueLen == -1) return SetMultiStringValue(lpszValueName, lpszValue);
    DWORD cb = (DWORD)((nValueLen == -1 ? lstrlen(lpszValue) + 1 : nValueLen) * sizeof(TCHAR));
    return ::RegSetValueEx(m_hKey, lpszValueName, 0, bMulti ? REG_MULTI_SZ : REG_SZ,
                           (const BYTE*)lpszValue, cb);
  }
  LONG SetValue(LPCTSTR lpszValueName, DWORD dwType, const void* pValue, ULONG nBytes) {
    return ::RegSetValueEx(m_hKey, lpszValueName, 0, dwType, (const BYTE*)pValue, nBytes);
  }
  static LONG WINAPI SetValue(HKEY hKeyParent, LPCTSTR lpszKeyName, LPCTSTR lpszValue,
                              LPCTSTR lpszValueName = NULL) {
    CRegKey key;
    LONG lRes = key.Create(hKeyParent, lpszKeyName);
    if (lRes == ERROR_SUCCESS) lRes = key.SetStringValue(lpszValueName, lpszValue);
    return lRes;
  }
  LONG SetKeyValue(LPCTSTR lpszKeyName, LPCTSTR lpszValue, LPCTSTR lpszValueName = NULL) {
    CRegKey key;
    LONG lRes = key.Create(m_hKey, lpszKeyName);
    if (lRes == ERROR_SUCCESS) lRes = key.SetStringValue(lpszValueName, lpszValue);
    return lRes;
  }
  LONG SetDWORDValue(LPCTSTR pszValueName, DWORD dwValue) {
    return ::RegSetValueEx(m_hKey, pszValueName, 0, REG_DWORD, (const BYTE*)&dwValue,
                           sizeof(DWORD));
  }
  LONG SetQWORDValue(LPCTSTR pszValueName, ULONGLONG qwValue) {
    return ::RegSetValueEx(m_hKey, pszValueName, 0, REG_QWORD, (const BYTE*)&qwValue,
                           sizeof(ULONGLONG));
  }
  LONG SetStringValue(LPCTSTR pszValueName, LPCTSTR pszValue, DWORD dwType = REG_SZ) {
    if (pszValue == NULL) return ERROR_INVALID_PARAMETER;
    return ::RegSetValueEx(m_hKey, pszValueName, 0, dwType, (const BYTE*)pszValue,
                           (DWORD)((lstrlen(pszValue) + 1) * sizeof(TCHAR)));
  }
  LONG SetMultiStringValue(LPCTSTR pszValueName, LPCTSTR pszValue) {
    if (pszValue == NULL) return ERROR_INVALID_PARAMETER;
    LPCTSTR p = pszValue;
    while (*p != 0) p += lstrlen(p) + 1;
    return ::RegSetValueEx(m_hKey, pszValueName, 0, REG_MULTI_SZ, (const BYTE*)pszValue,
                           (DWORD)((p - pszValue + 1) * sizeof(TCHAR)));
  }
  LONG SetBinaryValue(LPCTSTR pszValueName, const void* pValue, ULONG nBytes) {
    return ::RegSetValueEx(m_hKey, pszValueName, 0, REG_BINARY, (const BYTE*)pValue, nBytes);
  }
  LONG SetGUIDValue(LPCTSTR pszValueName, REFGUID guidValue) {
    OLECHAR sz[40];
    ::StringFromGUID2(guidValue, sz, 40);
#ifdef UNICODE
    return SetStringValue(pszValueName, sz);
#else
    char a[40];
    ::WideCharToMultiByte(CP_ACP, 0, sz, -1, a, 40, NULL, NULL);
    return SetStringValue(pszValueName, a);
#endif
  }

  LONG QueryValue(DWORD& dwValue, LPCTSTR lpszValueName) {
    return QueryDWORDValue(lpszValueName, dwValue);
  }
  LONG QueryValue(LPTSTR szValue, LPCTSTR lpszValueName, DWORD* pdwCount) {
    if (pdwCount == NULL) return ERROR_INVALID_PARAMETER;
    DWORD dwType = 0;
    DWORD cb = *pdwCount * sizeof(TCHAR);
    LONG lRes = ::RegQueryValueEx(m_hKey, lpszValueName, NULL, &dwType, (LPBYTE)szValue, &cb);
    *pdwCount = cb / sizeof(TCHAR);
    if (lRes == ERROR_SUCCESS && dwType != REG_SZ && dwType != REG_EXPAND_SZ &&
        dwType != REG_MULTI_SZ)
      return ERROR_INVALID_DATA;
    return lRes;
  }
  LONG QueryValue(LPCTSTR pszValueName, DWORD* pdwType, void* pData, ULONG* pnBytes) {
    return ::RegQueryValueEx(m_hKey, pszValueName, NULL, pdwType, (LPBYTE)pData, pnBytes);
  }
  LONG QueryDWORDValue(LPCTSTR pszValueName, DWORD& dwValue) {
    DWORD dwType = 0, cb = sizeof(DWORD), dw = 0;
    LONG lRes = ::RegQueryValueEx(m_hKey, pszValueName, NULL, &dwType, (LPBYTE)&dw, &cb);
    if (lRes != ERROR_SUCCESS) return lRes;
    if (dwType != REG_DWORD) return ERROR_INVALID_DATA;
    dwValue = dw;
    return ERROR_SUCCESS;
  }
  LONG QueryQWORDValue(LPCTSTR pszValueName, ULONGLONG& qwValue) {
    DWORD dwType = 0, cb = sizeof(ULONGLONG);
    ULONGLONG qw = 0;
    LONG lRes = ::RegQueryValueEx(m_hKey, pszValueName, NULL, &dwType, (LPBYTE)&qw, &cb);
    if (lRes != ERROR_SUCCESS) return lRes;
    if (dwType != REG_QWORD) return ERROR_INVALID_DATA;
    qwValue = qw;
    return ERROR_SUCCESS;
  }
  LONG QueryStringValue(LPCTSTR pszValueName, LPTSTR pszValue, ULONG* pnChars) {
    if (pnChars == NULL) return ERROR_INVALID_PARAMETER;
    DWORD dwType = 0;
    DWORD cb = *pnChars * sizeof(TCHAR);
    LONG lRes = ::RegQueryValueEx(m_hKey, pszValueName, NULL, &dwType, (LPBYTE)pszValue, &cb);
    *pnChars = cb / sizeof(TCHAR);
    if (lRes != ERROR_SUCCESS) return lRes;
    if (dwType != REG_SZ && dwType != REG_EXPAND_SZ) return ERROR_INVALID_DATA;
    return ERROR_SUCCESS;
  }
  LONG QueryMultiStringValue(LPCTSTR pszValueName, LPTSTR pszValue, ULONG* pnChars) {
    if (pnChars == NULL) return ERROR_INVALID_PARAMETER;
    DWORD dwType = 0;
    DWORD cb = *pnChars * sizeof(TCHAR);
    LONG lRes = ::RegQueryValueEx(m_hKey, pszValueName, NULL, &dwType, (LPBYTE)pszValue, &cb);
    *pnChars = cb / sizeof(TCHAR);
    if (lRes != ERROR_SUCCESS) return lRes;
    return dwType == REG_MULTI_SZ ? ERROR_SUCCESS : ERROR_INVALID_DATA;
  }
  LONG QueryBinaryValue(LPCTSTR pszValueName, void* pValue, ULONG* pnBytes) {
    DWORD dwType = 0;
    LONG lRes = ::RegQueryValueEx(m_hKey, pszValueName, NULL, &dwType, (LPBYTE)pValue, pnBytes);
    if (lRes != ERROR_SUCCESS) return lRes;
    return dwType == REG_BINARY ? ERROR_SUCCESS : ERROR_INVALID_DATA;
  }
  LONG QueryGUIDValue(LPCTSTR pszValueName, GUID& guidValue) {
    TCHAR sz[40];
    ULONG n = 40;
    LONG lRes = QueryStringValue(pszValueName, sz, &n);
    if (lRes != ERROR_SUCCESS) return lRes;
#ifdef UNICODE
    LPOLESTR w = sz;
#else
    OLECHAR w[40];
    ::MultiByteToWideChar(CP_ACP, 0, sz, -1, w, 40);
#endif
    return SUCCEEDED(::CLSIDFromString(w, &guidValue)) ? ERROR_SUCCESS : ERROR_INVALID_DATA;
  }

  LONG EnumKey(DWORD iIndex, LPTSTR pszName, LPDWORD pnNameLength, FILETIME* pftLastWriteTime = NULL) {
    FILETIME ft;
    return ::RegEnumKeyEx(m_hKey, iIndex, pszName, pnNameLength, NULL, NULL, NULL,
                          pftLastWriteTime != NULL ? pftLastWriteTime : &ft);
  }
  LONG DeleteSubKey(LPCTSTR lpszSubKey) { return ::RegDeleteKey(m_hKey, lpszSubKey); }
  LONG DeleteValue(LPCTSTR lpszValue) { return ::RegDeleteValue(m_hKey, lpszValue); }
  LONG RecurseDeleteKey(LPCTSTR lpszKey) {
    CRegKey key;
    LONG lRes = key.Open(m_hKey, lpszKey, KEY_READ | KEY_WRITE);
    if (lRes != ERROR_SUCCESS) return lRes;
    TCHAR szBuffer[256];
    for (;;) {
      DWORD dwSize = 256;
      FILETIME time;
      if (::RegEnumKeyEx(key.m_hKey, 0, szBuffer, &dwSize, NULL, NULL, NULL, &time) !=
          ERROR_SUCCESS)
        break;
      lRes = key.RecurseDeleteKey(szBuffer);
      if (lRes != ERROR_SUCCESS) return lRes;
    }
    key.Close();
    return DeleteSubKey(lpszKey);
  }
};

// ---------------------------------------------------------------------
// The registrar: runs the .rgs scripts of DECLARE_REGISTRY_RESOURCEID.
//
//   HKCR
//   {
//     MyLib.MyObj.1 = s 'MyObj Class'
//     {
//       CLSID = s '{...}'
//     }
//     NoRemove CLSID
//     {
//       ForceRemove {...} = s 'MyObj Class'
//       {
//         InprocServer32 = s '%MODULE%'
//         {
//           val ThreadingModel = s 'Apartment'
//         }
//       }
//     }
//   }
//
// Registering creates the keys and values; ForceRemove deletes a key
// first, Delete deletes one. Unregistering deletes the keys the script
// names, except those marked NoRemove, whose contents it goes into.
// %NAME% is replaced from the replacement map (%MODULE% is the module's
// path), %% by %. Values are s (string), e (expandable string), d (DWORD),
// m (strings), b (binary, in hex).

class CRegObject {
public:
  CRegObject() : m_nRep(0) {}
  ~CRegObject() { ClearReplacements(); }

  HRESULT AddReplacement(LPCOLESTR key, LPCOLESTR item) {
    if (key == NULL || item == NULL) return E_INVALIDARG;
    if (m_nRep == kMaxRep) return E_OUTOFMEMORY;
    m_rep[m_nRep].key = Dup(key);
    m_rep[m_nRep].item = Dup(item);
    m_nRep++;
    return S_OK;
  }
  HRESULT ClearReplacements() {
    for (int i = 0; i < m_nRep; i++) {
      delete[] m_rep[i].key;
      delete[] m_rep[i].item;
    }
    m_nRep = 0;
    return S_OK;
  }

  HRESULT StringRegister(LPCOLESTR bstrData) { return Run(bstrData, TRUE); }
  HRESULT StringUnregister(LPCOLESTR bstrData) { return Run(bstrData, FALSE); }
  HRESULT ResourceRegister(LPCOLESTR szFileName, UINT nID, LPCOLESTR szType) {
    return RunResource(szFileName, MAKEINTRESOURCEW(nID), szType, TRUE);
  }
  HRESULT ResourceUnregister(LPCOLESTR szFileName, UINT nID, LPCOLESTR szType) {
    return RunResource(szFileName, MAKEINTRESOURCEW(nID), szType, FALSE);
  }
  HRESULT ResourceRegisterSz(LPCOLESTR szFileName, LPCOLESTR szID, LPCOLESTR szType) {
    return RunResource(szFileName, szID, szType, TRUE);
  }
  HRESULT ResourceUnregisterSz(LPCOLESTR szFileName, LPCOLESTR szID, LPCOLESTR szType) {
    return RunResource(szFileName, szID, szType, FALSE);
  }
  // A script in a module that is loaded already.
  HRESULT ModuleResource(HINSTANCE hInst, LPCWSTR lpszRes, BOOL bRegister) {
    HRSRC hrsrc = ::FindResourceW(hInst, lpszRes, L"REGISTRY");
    if (hrsrc == NULL) return HRESULT_FROM_WIN32(::GetLastError());
    HGLOBAL hg = ::LoadResource(hInst, hrsrc);
    DWORD cb = ::SizeofResource(hInst, hrsrc);
    const char* p = (const char*)::LockResource(hg);
    if (p == NULL) return E_FAIL;
    // The script is ANSI text, or UTF-16 when it starts with a BOM.
    LPWSTR w;
    if (cb >= 2 && (BYTE)p[0] == 0xFF && (BYTE)p[1] == 0xFE) {
      UINT n = (cb - 2) / 2;
      w = new WCHAR[n + 1];
      memcpy(w, p + 2, n * 2);
      w[n] = 0;
    } else {
      int n = ::MultiByteToWideChar(CP_ACP, 0, p, (int)cb, NULL, 0);
      w = new WCHAR[n + 1];
      ::MultiByteToWideChar(CP_ACP, 0, p, (int)cb, w, n);
      w[n] = 0;
    }
    HRESULT hr = Run(w, bRegister);
    delete[] w;
    return hr;
  }

private:
  enum { kMaxRep = 64 };
  struct Rep {
    LPWSTR key;
    LPWSTR item;
  };
  Rep m_rep[kMaxRep];
  int m_nRep;

  // The parser's state.
  LPCWSTR m_p;
  BOOL m_bRegister;

  static LPWSTR Dup(LPCOLESTR s) {
    size_t n = wcslen(s);
    LPWSTR d = new WCHAR[n + 1];
    memcpy(d, s, (n + 1) * sizeof(WCHAR));
    return d;
  }

  HRESULT RunResource(LPCOLESTR szFileName, LPCWSTR lpszRes, LPCOLESTR szType, BOOL bRegister) {
    (void)szType;
    HINSTANCE h = ::LoadLibraryExW(szFileName, NULL, LOAD_LIBRARY_AS_DATAFILE);
    if (h == NULL) return HRESULT_FROM_WIN32(::GetLastError());
    HRESULT hr = ModuleResource(h, lpszRes, bRegister);
    ::FreeLibrary(h);
    return hr;
  }

  // A growable wide string.
  struct Buf {
    LPWSTR p;
    size_t n, cap;
    Buf() : p(NULL), n(0), cap(0) {}
    ~Buf() { delete[] p; }
    void Add(WCHAR c) {
      if (n + 2 > cap) {
        size_t c2 = cap ? cap * 2 : 64;
        LPWSTR q = new WCHAR[c2];
        if (n) memcpy(q, p, n * sizeof(WCHAR));
        delete[] p;
        p = q;
        cap = c2;
      }
      p[n++] = c;
      p[n] = 0;
    }
    void Add(LPCWSTR s) {
      while (*s) Add(*s++);
    }
    LPCWSTR Str() {
      if (p == NULL) Add((WCHAR)0), n = 0;
      return p;
    }
  };

  // The script with the replacements made.
  HRESULT Expand(LPCOLESTR in, Buf& out) {
    for (LPCOLESTR p = in; *p;) {
      if (*p != L'%') {
        out.Add(*p++);
        continue;
      }
      if (p[1] == L'%') {
        out.Add(L'%');
        p += 2;
        continue;
      }
      LPCOLESTR e = wcschr(p + 1, L'%');
      if (e == NULL) return DISP_E_EXCEPTION;
      size_t len = (size_t)(e - p - 1);
      int i;
      for (i = 0; i < m_nRep; i++)
        if (wcslen(m_rep[i].key) == len &&
            ::CompareStringW(LOCALE_SYSTEM_DEFAULT, NORM_IGNORECASE, m_rep[i].key, (int)len,
                             p + 1, (int)len) == CSTR_EQUAL)
          break;
      if (i == m_nRep) return DISP_E_EXCEPTION;
      out.Add(m_rep[i].item);
      p = e + 1;
    }
    out.Str();
    return S_OK;
  }

  void SkipSpace() {
    while (*m_p == L' ' || *m_p == L'\t' || *m_p == L'\r' || *m_p == L'\n' ||
           *m_p == 0xFEFF)
      m_p++;
  }
  // The next token: a quoted string, a single { } or =, or a word.
  bool Token(Buf& tok, bool* pQuoted = NULL) {
    tok.n = 0;
    tok.Str();
    if (pQuoted) *pQuoted = false;
    SkipSpace();
    if (*m_p == 0) return false;
    if (*m_p == L'\'') {
      if (pQuoted) *pQuoted = true;
      m_p++;
      for (;;) {
        if (*m_p == 0) return false;
        if (*m_p == L'\'') {
          if (m_p[1] == L'\'') {
            tok.Add(L'\'');
            m_p += 2;
            continue;
          }
          m_p++;
          break;
        }
        tok.Add(*m_p++);
      }
      tok.Str();
      return true;
    }
    if (*m_p == L'{' || *m_p == L'}' || *m_p == L'=') {
      // A brace that starts a GUID is part of a word.
      if (*m_p != L'{' || !IsGuidStart(m_p)) {
        tok.Add(*m_p++);
        return true;
      }
    }
    while (*m_p && *m_p != L' ' && *m_p != L'\t' && *m_p != L'\r' && *m_p != L'\n' &&
           *m_p != L'=' && !(*m_p == L'}' && tok.n == 0) &&
           !(*m_p == L'{' && tok.n != 0 && !IsGuidStart(m_p)))
      tok.Add(*m_p++);
    tok.Str();
    return true;
  }
  static bool IsGuidStart(LPCWSTR p) {
    // {xxxxxxxx-...}
    for (int i = 1; i <= 8; i++)
      if (!((p[i] >= L'0' && p[i] <= L'9') || (p[i] >= L'a' && p[i] <= L'f') ||
            (p[i] >= L'A' && p[i] <= L'F')))
        return false;
    return p[9] == L'-';
  }
  static bool Is(const Buf& t, LPCWSTR s) { return t.p != NULL && ::lstrcmpiW(t.p, s) == 0; }

  static HKEY RootKey(const Buf& t) {
    static const struct {
      LPCWSTR shortName, longName;
      HKEY key;
    } roots[] = {
        {L"HKCR", L"HKEY_CLASSES_ROOT", HKEY_CLASSES_ROOT},
        {L"HKCU", L"HKEY_CURRENT_USER", HKEY_CURRENT_USER},
        {L"HKLM", L"HKEY_LOCAL_MACHINE", HKEY_LOCAL_MACHINE},
        {L"HKU", L"HKEY_USERS", HKEY_USERS},
        {L"HKPD", L"HKEY_PERFORMANCE_DATA", HKEY_PERFORMANCE_DATA},
        {L"HKDD", L"HKEY_DYN_DATA", HKEY_DYN_DATA},
        {L"HKCC", L"HKEY_CURRENT_CONFIG", HKEY_CURRENT_CONFIG},
    };
    for (size_t i = 0; i < sizeof(roots) / sizeof(roots[0]); i++)
      if (Is(t, roots[i].shortName) || Is(t, roots[i].longName)) return roots[i].key;
    return NULL;
  }

  // A value after "=": its type letter and data.
  HRESULT ReadValue(DWORD& type, Buf& data, DWORD& dw) {
    Buf t;
    if (!Token(t)) return DISP_E_EXCEPTION;
    WCHAR c = t.p != NULL ? (WCHAR)(t.p[0] >= L'A' && t.p[0] <= L'Z' ? t.p[0] + 32 : t.p[0]) : 0;
    bool quoted;
    if (!Token(data, &quoted)) return DISP_E_EXCEPTION;
    switch (c) {
    case L's': type = REG_SZ; break;
    case L'e': type = REG_EXPAND_SZ; break;
    case L'm': type = REG_MULTI_SZ; break;
    case L'b': type = REG_BINARY; break;
    case L'd':
      type = REG_DWORD;
      dw = (DWORD)wcstoul(data.p, NULL, 0);
      break;
    default: return DISP_E_EXCEPTION;
    }
    return S_OK;
  }

  static LONG SetValueW(HKEY hKey, LPCWSTR name, DWORD type, Buf& data, DWORD dw) {
    switch (type) {
    case REG_DWORD:
      return ::RegSetValueExW(hKey, name, 0, REG_DWORD, (const BYTE*)&dw, sizeof(dw));
    case REG_BINARY: {
      size_t n = data.n / 2;
      BYTE* b = new BYTE[n + 1];
      for (size_t i = 0; i < n; i++) {
        WCHAR h[3] = {data.p[2 * i], data.p[2 * i + 1], 0};
        b[i] = (BYTE)wcstoul(h, NULL, 16);
      }
      LONG l = ::RegSetValueExW(hKey, name, 0, REG_BINARY, b, (DWORD)n);
      delete[] b;
      return l;
    }
    case REG_MULTI_SZ: {
      // '\0' in the script separates the strings.
      Buf m;
      for (size_t i = 0; i < data.n; i++) {
        if (data.p[i] == L'\\' && i + 1 < data.n && data.p[i + 1] == L'0') {
          m.Add((WCHAR)0);
          i++;
        } else {
          m.Add(data.p[i]);
        }
      }
      m.Add((WCHAR)0);
      m.Add((WCHAR)0);
      return ::RegSetValueExW(hKey, name, 0, REG_MULTI_SZ, (const BYTE*)m.p,
                              (DWORD)(m.n * sizeof(WCHAR)));
    }
    default: {
      LPCWSTR s = data.Str();
      return ::RegSetValueExW(hKey, name, 0, type, (const BYTE*)s,
                              (DWORD)((wcslen(s) + 1) * sizeof(WCHAR)));
    }
    }
  }

  static LONG DeleteTree(HKEY hParent, LPCWSTR name) {
    HKEY h;
    LONG l = ::RegOpenKeyExW(hParent, name, 0, KEY_READ | KEY_WRITE, &h);
    if (l != ERROR_SUCCESS) return l;
    WCHAR sub[256];
    for (;;) {
      DWORD n = 256;
      if (::RegEnumKeyExW(h, 0, sub, &n, NULL, NULL, NULL, NULL) != ERROR_SUCCESS) break;
      if (DeleteTree(h, sub) != ERROR_SUCCESS) break;
    }
    ::RegCloseKey(h);
    return ::RegDeleteKeyW(hParent, name);
  }
  static bool HasSubKeys(HKEY hParent, LPCWSTR name) {
    HKEY h;
    if (::RegOpenKeyExW(hParent, name, 0, KEY_READ, &h) != ERROR_SUCCESS) return false;
    DWORD nKeys = 0;
    ::RegQueryInfoKeyW(h, NULL, NULL, NULL, &nKeys, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
    ::RegCloseKey(h);
    return nKeys != 0;
  }

  // The contents of a { } block under hKey (NULL when the key is absent,
  // when unregistering, or below the root). bSkip reads it without acting.
  HRESULT Block(HKEY hKey, bool bSkip) {
    Buf t;
    for (;;) {
      if (!Token(t)) return DISP_E_EXCEPTION;
      if (Is(t, L"}")) return S_OK;
      HRESULT hr = Entry(hKey, t, bSkip);
      if (FAILED(hr)) return hr;
    }
  }

  // One key or value: [NoRemove|ForceRemove|Delete|val] name [= v] [{ ... }]
  HRESULT Entry(HKEY hParent, Buf& t, bool bSkip) {
    bool bNoRemove = false, bForceRemove = false, bDelete = false, bVal = false;
    Buf name;
    for (;;) {
      if (Is(t, L"NoRemove")) bNoRemove = true;
      else if (Is(t, L"ForceRemove")) bForceRemove = true;
      else if (Is(t, L"Delete")) bDelete = true;
      else if (Is(t, L"val")) bVal = true;
      else break;
      if (!Token(t)) return DISP_E_EXCEPTION;
    }
    name.Add(t.Str());
    if (name.n == 0 && t.p != NULL && t.p[0] == 0) {
      // An empty quoted name: the key's default value.
    }
    // What follows: "=" and a value, "{", or another entry.
    LPCWSTR save = m_p;
    Buf next;
    bool haveValue = false, haveBlock = false;
    DWORD type = 0, dw = 0;
    Buf data;
    if (Token(next) && Is(next, L"=")) {
      HRESULT hr = ReadValue(type, data, dw);
      if (FAILED(hr)) return hr;
      haveValue = true;
      save = m_p;
      if (Token(next) && Is(next, L"{")) haveBlock = true;
      else m_p = save;
    } else if (Is(next, L"{")) {
      haveBlock = true;
    } else {
      m_p = save;
    }

    LPCWSTR nm = name.Str();
    if (bVal) {
      if (!bSkip && hParent != NULL) {
        if (m_bRegister) {
          if (haveValue && SetValueW(hParent, nm, type, data, dw) != ERROR_SUCCESS)
            return E_FAIL;
        } else if (!bNoRemove) {
          ::RegDeleteValueW(hParent, nm);
        }
      }
      return haveBlock ? Block(NULL, true) : S_OK;
    }

    if (bSkip || hParent == NULL) return haveBlock ? Block(NULL, true) : S_OK;

    if (m_bRegister) {
      if (bDelete) {
        DeleteTree(hParent, nm);
        return haveBlock ? Block(NULL, true) : S_OK;
      }
      if (bForceRemove) DeleteTree(hParent, nm);
      HKEY h;
      if (::RegCreateKeyExW(hParent, nm, 0, NULL, REG_OPTION_NON_VOLATILE, KEY_ALL_ACCESS, NULL,
                            &h, NULL) != ERROR_SUCCESS)
        return E_FAIL;
      HRESULT hr = S_OK;
      if (haveValue && SetValueW(h, NULL, type, data, dw) != ERROR_SUCCESS) hr = E_FAIL;
      if (SUCCEEDED(hr) && haveBlock) hr = Block(h, false);
      ::RegCloseKey(h);
      return hr;
    }
    // Unregistering.
    HKEY h = NULL;
    ::RegOpenKeyExW(hParent, nm, 0, KEY_ALL_ACCESS, &h);
    HRESULT hr = haveBlock ? Block(h, h == NULL) : S_OK;
    if (h != NULL) ::RegCloseKey(h);
    if (FAILED(hr)) return hr;
    // The key goes with everything in it when it is ForceRemove, else only
    // when its entries have left it empty (a NoRemove key inside keeps
    // it, and so do keys the script does not name).
    if (!bNoRemove && !bDelete) {
      if (bForceRemove || !HasSubKeys(hParent, nm)) DeleteTree(hParent, nm);
    }
    return S_OK;
  }

  HRESULT Run(LPCOLESTR script, BOOL bRegister) {
    Buf text;
    HRESULT hr = Expand(script, text);
    if (FAILED(hr)) return hr;
    m_p = text.Str();
    m_bRegister = bRegister;
    Buf t;
    while (Token(t)) {
      HKEY root = RootKey(t);
      if (root == NULL) return DISP_E_EXCEPTION;
      Buf brace;
      if (!Token(brace) || !Is(brace, L"{")) return DISP_E_EXCEPTION;
      hr = Block(root, false);
      if (FAILED(hr)) return hr;
    }
    return S_OK;
  }
};

// ---------------------------------------------------------------------
// The module.

// The module that holds this code, and where its resources are.
class CAtlBaseModule {
public:
  HINSTANCE m_hInst;
  HINSTANCE m_hInstResource;
  CAtlBaseModule() {
    m_hInst = m_hInstResource = _AtlThisImage();
  }
  HINSTANCE GetModuleInstance() { return m_hInst; }
  HINSTANCE GetResourceInstance() { return m_hInstResource; }
  HINSTANCE SetResourceInstance(HINSTANCE h) {
    HINSTANCE old = m_hInstResource;
    m_hInstResource = h;
    return old;
  }
};
inline CAtlBaseModule _AtlBaseModule;

// The objects that OBJECT_ENTRY_AUTO adds, ATL 7's object map.
struct _ATL_AUTO_OBJMAP {
  _ATL_OBJMAP_ENTRY entry;
  _ATL_AUTO_OBJMAP* pNext;
};
inline _ATL_AUTO_OBJMAP* _AtlAutoObjectMap = NULL;

// The functions to run when the module ends (AddTermFunc).
typedef void(__stdcall _ATL_TERMFUNC)(DWORD_PTR dw);

class CAtlModule;
inline CAtlModule* _pAtlModule = NULL;

// What every module has: the lock count that keeps it loaded, and
// registration from scripts.
class CAtlModule {
public:
  LONG m_nLockCnt;
  CComCriticalSection m_csStaticDataInitAndTypeInfo;
  const GUID* m_pLibID;
  // The module's type library (DECLARE_LIBID), the default of
  // IDispatchImpl's.
  static inline GUID m_libid = {0, 0, 0, {0, 0, 0, 0, 0, 0, 0, 0}};
  struct TermFunc {
    _ATL_TERMFUNC* pFunc;
    DWORD_PTR dw;
    TermFunc* pNext;
  };
  TermFunc* m_pTermFuncs;

  CAtlModule() : m_nLockCnt(0), m_pLibID(NULL), m_pTermFuncs(NULL) {
    m_csStaticDataInitAndTypeInfo.Init();
    if (_pAtlModule == NULL) _pAtlModule = this;
  }
  virtual ~CAtlModule() {
    Term();
    m_csStaticDataInitAndTypeInfo.Term();
    if (_pAtlModule == this) _pAtlModule = NULL;
  }
  void Term() {
    while (m_pTermFuncs != NULL) {
      TermFunc* f = m_pTermFuncs;
      m_pTermFuncs = f->pNext;
      f->pFunc(f->dw);
      delete f;
    }
  }

  virtual LONG Lock() { return CComGlobalsThreadModel::Increment(&m_nLockCnt); }
  virtual LONG Unlock() { return CComGlobalsThreadModel::Decrement(&m_nLockCnt); }
  virtual LONG GetLockCount() { return m_nLockCnt; }

  HRESULT AddTermFunc(_ATL_TERMFUNC* pFunc, DWORD_PTR dw) {
    TermFunc* f = new TermFunc;
    f->pFunc = pFunc;
    f->dw = dw;
    f->pNext = m_pTermFuncs;
    m_pTermFuncs = f;
    return S_OK;
  }

  // The %MODULE% and %MODULE_RAW% replacements (and %APPID% for a module
  // that has one).
  virtual HRESULT AddCommonRGSReplacements(CRegObject* pRegistrar) {
    WCHAR szModule[MAX_PATH + 2];
    DWORD n = ::GetModuleFileNameW(_AtlBaseModule.GetModuleInstance(), szModule, MAX_PATH);
    szModule[n] = 0;
    pRegistrar->AddReplacement(L"MODULE_RAW", szModule);
    // %MODULE% doubles quotes, so that it fits in a quoted string.
    WCHAR szQuoted[2 * MAX_PATH + 2];
    WCHAR* q = szQuoted;
    for (WCHAR* p = szModule; *p; p++) {
      if (*p == L'\'') *q++ = L'\'';
      *q++ = *p;
    }
    *q = 0;
    return pRegistrar->AddReplacement(L"MODULE", szQuoted);
  }

  HRESULT UpdateRegistryFromResourceS(UINT nResID, BOOL bRegister,
                                      _ATL_REGMAP_ENTRY* pMapEntries = NULL) {
    return UpdateRegistryFromResourceW(MAKEINTRESOURCEW(nResID), bRegister, pMapEntries);
  }
  HRESULT UpdateRegistryFromResourceS(LPCTSTR lpszRes, BOOL bRegister,
                                      _ATL_REGMAP_ENTRY* pMapEntries = NULL) {
    if (IS_INTRESOURCE(lpszRes))
      return UpdateRegistryFromResourceW((LPCWSTR)lpszRes, bRegister, pMapEntries);
#ifdef UNICODE
    return UpdateRegistryFromResourceW(lpszRes, bRegister, pMapEntries);
#else
    WCHAR w[256];
    ::MultiByteToWideChar(CP_ACP, 0, lpszRes, -1, w, 256);
    return UpdateRegistryFromResourceW(w, bRegister, pMapEntries);
#endif
  }
  HRESULT UpdateRegistryFromResourceD(UINT nResID, BOOL bRegister,
                                      _ATL_REGMAP_ENTRY* pMapEntries = NULL) {
    return UpdateRegistryFromResourceS(nResID, bRegister, pMapEntries);
  }
  HRESULT UpdateRegistryFromResourceD(LPCTSTR lpszRes, BOOL bRegister,
                                      _ATL_REGMAP_ENTRY* pMapEntries = NULL) {
    return UpdateRegistryFromResourceS(lpszRes, bRegister, pMapEntries);
  }
  HRESULT UpdateRegistryFromResource(UINT nResID, BOOL bRegister,
                                     _ATL_REGMAP_ENTRY* pMapEntries = NULL) {
    return UpdateRegistryFromResourceS(nResID, bRegister, pMapEntries);
  }
  HRESULT UpdateRegistryFromResource(LPCTSTR lpszRes, BOOL bRegister,
                                     _ATL_REGMAP_ENTRY* pMapEntries = NULL) {
    return UpdateRegistryFromResourceS(lpszRes, bRegister, pMapEntries);
  }

  HRESULT UpdateRegistryFromResourceW(LPCWSTR lpszRes, BOOL bRegister,
                                      _ATL_REGMAP_ENTRY* pMapEntries) {
    CRegObject ro;
    HRESULT hr = AddCommonRGSReplacements(&ro);
    if (FAILED(hr)) return hr;
    for (; pMapEntries != NULL && pMapEntries->szKey != NULL; pMapEntries++)
      ro.AddReplacement(pMapEntries->szKey, pMapEntries->szData);
    return ro.ModuleResource(_AtlBaseModule.GetResourceInstance(), lpszRes, bRegister);
  }

  // The type library in this module's resources (index 0 is the first).
  HRESULT RegisterTypeLib(LPCTSTR lpszIndex = NULL);
  HRESULT UnRegisterTypeLib(LPCTSTR lpszIndex = NULL);
};

// The module's file name, with "\N" for type library N.
inline HRESULT AtlLoadTypeLib(HINSTANCE hInstTypeLib, LPCOLESTR lpszIndex, BSTR* pbstrPath,
                              ITypeLib** ppTypeLib) {
  *pbstrPath = NULL;
  *ppTypeLib = NULL;
  WCHAR szModule[MAX_PATH + 10];
  DWORD n = ::GetModuleFileNameW(hInstTypeLib, szModule, MAX_PATH);
  if (n == 0) return HRESULT_FROM_WIN32(::GetLastError());
  szModule[n] = 0;
  if (lpszIndex != NULL) wcscat(szModule, lpszIndex);
  HRESULT hr = ::LoadTypeLib(szModule, ppTypeLib);
  if (FAILED(hr) && lpszIndex == NULL) {
    // A .tlb file of the module's name, beside it.
    WCHAR* dot = wcsrchr(szModule, L'.');
    if (dot != NULL) {
      wcscpy(dot, L".tlb");
      hr = ::LoadTypeLib(szModule, ppTypeLib);
    }
  }
  if (SUCCEEDED(hr)) *pbstrPath = ::SysAllocString(szModule);
  return hr;
}

inline HRESULT WINAPI AtlRegisterTypeLib(HINSTANCE hInstTypeLib, LPCOLESTR lpszIndex) {
  BSTR bstrPath = NULL;
  ITypeLib* pTypeLib = NULL;
  HRESULT hr = AtlLoadTypeLib(hInstTypeLib, lpszIndex, &bstrPath, &pTypeLib);
  if (SUCCEEDED(hr)) {
    // The help directory is the module's.
    WCHAR szDir[MAX_PATH];
    wcsncpy(szDir, bstrPath, MAX_PATH - 1);
    szDir[MAX_PATH - 1] = 0;
    WCHAR* slash = wcsrchr(szDir, L'\\');
    if (slash != NULL) *slash = 0;
    hr = ::RegisterTypeLib(pTypeLib, bstrPath, szDir);
  }
  if (pTypeLib != NULL) pTypeLib->Release();
  ::SysFreeString(bstrPath);
  return hr;
}

inline HRESULT WINAPI AtlUnRegisterTypeLib(HINSTANCE hInstTypeLib, LPCOLESTR lpszIndex) {
  BSTR bstrPath = NULL;
  ITypeLib* pTypeLib = NULL;
  HRESULT hr = AtlLoadTypeLib(hInstTypeLib, lpszIndex, &bstrPath, &pTypeLib);
  if (SUCCEEDED(hr)) {
    TLIBATTR* pAttr = NULL;
    hr = pTypeLib->GetLibAttr(&pAttr);
    if (SUCCEEDED(hr)) {
      hr = ::UnRegisterTypeLib(pAttr->guid, pAttr->wMajorVerNum, pAttr->wMinorVerNum, pAttr->lcid,
                               pAttr->syskind);
      pTypeLib->ReleaseTLibAttr(pAttr);
    }
  }
  if (pTypeLib != NULL) pTypeLib->Release();
  ::SysFreeString(bstrPath);
  return hr;
}

inline HRESULT CAtlModule::RegisterTypeLib(LPCTSTR lpszIndex) {
  USES_CONVERSION;
  return AtlRegisterTypeLib(_AtlBaseModule.GetModuleInstance(),
                            lpszIndex != NULL ? T2COLE(lpszIndex) : NULL);
}
inline HRESULT CAtlModule::UnRegisterTypeLib(LPCTSTR lpszIndex) {
  USES_CONVERSION;
  return AtlUnRegisterTypeLib(_AtlBaseModule.GetModuleInstance(),
                              lpszIndex != NULL ? T2COLE(lpszIndex) : NULL);
}

// Registration in the old style, DECLARE_REGISTRY(class, progid,
// versionless progid, description id, flags).
#define THREADFLAGS_APARTMENT 0x1
#define THREADFLAGS_BOTH 0x2
#define AUTPRXFLAG 0x4

inline HRESULT WINAPI AtlRegisterClassCategoriesHelper(REFCLSID clsid,
                                                       const _ATL_CATMAP_ENTRY* pCatMap,
                                                       BOOL bRegister) {
  if (pCatMap == NULL) return S_OK;
  // CLSID_StdComponentCategoriesMgr and IID_ICatRegister, here so that
  // a server needs no libuuid.
  static const CLSID clsidMgr = {
      0x0002e005, 0x0000, 0x0000, {0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};
  static const IID iidCatRegister = {
      0x0002e012, 0x0000, 0x0000, {0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};
  ICatRegister* pcr = NULL;
  HRESULT hr = ::CoCreateInstance(clsidMgr, NULL, CLSCTX_INPROC_SERVER, iidCatRegister,
                                  (void**)&pcr);
  if (FAILED(hr)) return bRegister ? hr : S_OK;
  for (; pCatMap->iType != _ATL_CATMAP_ENTRY_END && SUCCEEDED(hr); pCatMap++) {
    CATID catid = *pCatMap->pcatid;
    bool impl = pCatMap->iType == _ATL_CATMAP_ENTRY_IMPLEMENTED;
    if (bRegister)
      hr = impl ? pcr->RegisterClassImplCategories(clsid, 1, &catid)
                : pcr->RegisterClassReqCategories(clsid, 1, &catid);
    else
      impl ? pcr->UnRegisterClassImplCategories(clsid, 1, &catid)
           : pcr->UnRegisterClassReqCategories(clsid, 1, &catid);
  }
  pcr->Release();
  return bRegister ? hr : S_OK;
}

// ---------------------------------------------------------------------
// ATL 3's CComModule: the object map is the one Init is given (or, when
// that is NULL, the OBJECT_ENTRY_AUTO entries).

class CComModule : public CAtlModule {
public:
  HINSTANCE m_hInst;
  HINSTANCE m_hInstResource;
  HINSTANCE m_hInstTypeLib;
  _ATL_OBJMAP_ENTRY* m_pObjMap;
  CComCriticalSection m_csTypeInfoHolder;
  CComCriticalSection m_csWindowCreate;
  CComCriticalSection m_csObjMap;
  const GUID* m_plibid;

  CComModule()
      : m_hInst(NULL), m_hInstResource(NULL), m_hInstTypeLib(NULL), m_pObjMap(NULL),
        m_plibid(NULL) {
    m_csTypeInfoHolder.Init();
    m_csWindowCreate.Init();
    m_csObjMap.Init();
    // The module a program declares is the one ATL uses.
    _pAtlModule = this;
  }
  ~CComModule() {
    m_csTypeInfoHolder.Term();
    m_csWindowCreate.Term();
    m_csObjMap.Term();
  }

  HRESULT Init(_ATL_OBJMAP_ENTRY* p, HINSTANCE h, const GUID* plibid = NULL) {
    m_pObjMap = p;
    m_hInst = m_hInstResource = m_hInstTypeLib = h;
    _AtlBaseModule.m_hInst = _AtlBaseModule.m_hInstResource = h;
    m_plibid = plibid;
    if (plibid != NULL) {
      m_pLibID = plibid;
      m_libid = *plibid;
    }
    for (_ATL_OBJMAP_ENTRY* e = FirstEntry(); e != NULL; e = NextEntry(e)) {
      if (e->pfnObjectMain != NULL) e->pfnObjectMain(true);
    }
    return S_OK;
  }
  void Term() {
    for (_ATL_OBJMAP_ENTRY* e = FirstEntry(); e != NULL; e = NextEntry(e)) {
      if (e->pCF != NULL) {
        e->pCF->Release();
        e->pCF = NULL;
      }
      if (e->pfnObjectMain != NULL) e->pfnObjectMain(false);
    }
    CAtlModule::Term();
  }

  HINSTANCE GetModuleInstance() { return m_hInst; }
  HINSTANCE GetResourceInstance() { return m_hInstResource; }
  HINSTANCE GetTypeLibInstance() { return m_hInstTypeLib; }

  // The object map: the one Init was given, else ATL 7's auto entries.
  _ATL_OBJMAP_ENTRY* FirstEntry() {
    if (m_pObjMap != NULL) return m_pObjMap->pclsid != NULL ? m_pObjMap : NULL;
    return _AtlAutoObjectMap != NULL ? &_AtlAutoObjectMap->entry : NULL;
  }
  _ATL_OBJMAP_ENTRY* NextEntry(_ATL_OBJMAP_ENTRY* e) {
    if (m_pObjMap != NULL) {
      e++;
      return e->pclsid != NULL ? e : NULL;
    }
    _ATL_AUTO_OBJMAP* a = ((_ATL_AUTO_OBJMAP*)e)->pNext;
    return a != NULL ? &a->entry : NULL;
  }

  HRESULT GetClassObject(REFCLSID rclsid, REFIID riid, LPVOID* ppv) {
    if (ppv == NULL) return E_POINTER;
    *ppv = NULL;
    for (_ATL_OBJMAP_ENTRY* e = FirstEntry(); e != NULL; e = NextEntry(e)) {
      if (e->pfnGetClassObject == NULL || !InlineIsEqualGUID(rclsid, *e->pclsid)) continue;
      if (e->pCF == NULL) {
        m_csObjMap.Lock();
        HRESULT hr = S_OK;
        if (e->pCF == NULL)
          hr = e->pfnGetClassObject((void*)e->pfnCreateInstance, IID_IUnknown, (LPVOID*)&e->pCF);
        m_csObjMap.Unlock();
        if (FAILED(hr)) return hr;
      }
      return e->pCF != NULL ? e->pCF->QueryInterface(riid, ppv) : CLASS_E_CLASSNOTAVAILABLE;
    }
    return CLASS_E_CLASSNOTAVAILABLE;
  }

  // An EXE server's class objects.
  HRESULT RegisterClassObjects(DWORD dwClsContext, DWORD dwFlags) {
    HRESULT hr = S_OK;
    for (_ATL_OBJMAP_ENTRY* e = FirstEntry(); e != NULL && SUCCEEDED(hr); e = NextEntry(e))
      hr = e->RegisterClassObject(dwClsContext, dwFlags);
    return hr;
  }
  HRESULT RevokeClassObjects() {
    HRESULT hr = S_OK;
    for (_ATL_OBJMAP_ENTRY* e = FirstEntry(); e != NULL; e = NextEntry(e)) {
      HRESULT h = e->RevokeClassObject();
      if (FAILED(h)) hr = h;
    }
    return hr;
  }

  // Registers the objects (all, or the one pCLSID names) and, with
  // bRegTypeLib, the type library in the module's resources.
  HRESULT RegisterServer(BOOL bRegTypeLib = FALSE, const CLSID* pCLSID = NULL) {
    HRESULT hr = S_OK;
    for (_ATL_OBJMAP_ENTRY* e = FirstEntry(); e != NULL && SUCCEEDED(hr); e = NextEntry(e)) {
      if (pCLSID != NULL && !InlineIsEqualGUID(*pCLSID, *e->pclsid)) continue;
      if (e->pfnUpdateRegistry != NULL) hr = e->pfnUpdateRegistry(TRUE);
      if (SUCCEEDED(hr) && e->pfnGetCategoryMap != NULL)
        hr = AtlRegisterClassCategoriesHelper(*e->pclsid, e->pfnGetCategoryMap(), TRUE);
    }
    if (SUCCEEDED(hr) && bRegTypeLib) hr = AtlRegisterTypeLib(m_hInstTypeLib, NULL);
    return hr;
  }
  HRESULT UnregisterServer(BOOL bUnRegTypeLib, const CLSID* pCLSID = NULL) {
    for (_ATL_OBJMAP_ENTRY* e = FirstEntry(); e != NULL; e = NextEntry(e)) {
      if (pCLSID != NULL && !InlineIsEqualGUID(*pCLSID, *e->pclsid)) continue;
      if (e->pfnGetCategoryMap != NULL)
        AtlRegisterClassCategoriesHelper(*e->pclsid, e->pfnGetCategoryMap(), FALSE);
      if (e->pfnUpdateRegistry != NULL) e->pfnUpdateRegistry(FALSE);
    }
    if (bUnRegTypeLib) AtlUnRegisterTypeLib(m_hInstTypeLib, NULL);
    return S_OK;
  }
  HRESULT UnregisterServer(const CLSID* pCLSID = NULL) { return UnregisterServer(FALSE, pCLSID); }

  HRESULT RegisterTypeLib() { return AtlRegisterTypeLib(m_hInstTypeLib, NULL); }
  HRESULT RegisterTypeLib(LPCTSTR lpszIndex) {
    USES_CONVERSION;
    return AtlRegisterTypeLib(m_hInstTypeLib, T2COLE(lpszIndex));
  }
  HRESULT UnRegisterTypeLib() { return AtlUnRegisterTypeLib(m_hInstTypeLib, NULL); }
  HRESULT UnRegisterTypeLib(LPCTSTR lpszIndex) {
    USES_CONVERSION;
    return AtlUnRegisterTypeLib(m_hInstTypeLib, T2COLE(lpszIndex));
  }

  // The old DECLARE_REGISTRY: the ProgIDs and the CLSID key of an object.
  HRESULT RegisterClassHelper(const CLSID& clsid, LPCTSTR lpszProgID, LPCTSTR lpszVerIndProgID,
                              UINT nDescID, DWORD dwFlags) {
    TCHAR szDesc[256];
    szDesc[0] = 0;
    ::LoadString(m_hInstResource, nDescID, szDesc, 256);
    TCHAR szModule[MAX_PATH];
    DWORD n = ::GetModuleFileName(m_hInst, szModule, MAX_PATH);
    szModule[n] = 0;
    OLECHAR wClsid[40];
    ::StringFromGUID2(clsid, wClsid, 40);
    TCHAR szClsid[40];
#ifdef UNICODE
    lstrcpy(szClsid, wClsid);
#else
    ::WideCharToMultiByte(CP_ACP, 0, wClsid, -1, szClsid, 40, NULL, NULL);
#endif
    CRegKey key, sub;
    if (lpszProgID != NULL && *lpszProgID) {
      if (key.Create(HKEY_CLASSES_ROOT, lpszProgID) == ERROR_SUCCESS) {
        key.SetStringValue(NULL, szDesc);
        key.SetKeyValue(TEXT("CLSID"), szClsid);
      }
    }
    if (lpszVerIndProgID != NULL && *lpszVerIndProgID) {
      if (key.Create(HKEY_CLASSES_ROOT, lpszVerIndProgID) == ERROR_SUCCESS) {
        key.SetStringValue(NULL, szDesc);
        key.SetKeyValue(TEXT("CLSID"), szClsid);
        if (lpszProgID != NULL) key.SetKeyValue(TEXT("CurVer"), lpszProgID);
      }
    }
    TCHAR szKey[64];
    lstrcpy(szKey, TEXT("CLSID\\"));
    lstrcat(szKey, szClsid);
    if (key.Create(HKEY_CLASSES_ROOT, szKey) != ERROR_SUCCESS) return E_FAIL;
    key.SetStringValue(NULL, szDesc);
    if (lpszProgID != NULL) key.SetKeyValue(TEXT("ProgID"), lpszProgID);
    if (lpszVerIndProgID != NULL) key.SetKeyValue(TEXT("VersionIndependentProgID"), lpszVerIndProgID);
    // A DLL is an in-process server, a program a local one.
#ifdef _WINPORT
    bool bDll = m_hInst != ::GetModuleHandle(NULL);
#else
    bool bDll = (((IMAGE_NT_HEADERS*)((BYTE*)m_hInst + ((IMAGE_DOS_HEADER*)m_hInst)->e_lfanew))
                     ->FileHeader.Characteristics & IMAGE_FILE_DLL) != 0;
#endif
    if (sub.Create(key, bDll ? TEXT("InprocServer32") : TEXT("LocalServer32")) == ERROR_SUCCESS) {
      sub.SetStringValue(NULL, szModule);
      if (bDll && (dwFlags & (THREADFLAGS_APARTMENT | THREADFLAGS_BOTH)))
        sub.SetStringValue(TEXT("ThreadingModel"),
                           (dwFlags & THREADFLAGS_BOTH) ? TEXT("Both") : TEXT("Apartment"));
    }
    return S_OK;
  }
  HRESULT UnregisterClassHelper(const CLSID& clsid, LPCTSTR lpszProgID, LPCTSTR lpszVerIndProgID) {
    CRegKey key;
    key.Attach(HKEY_CLASSES_ROOT);
    if (lpszProgID != NULL && *lpszProgID) key.RecurseDeleteKey(lpszProgID);
    if (lpszVerIndProgID != NULL && *lpszVerIndProgID) key.RecurseDeleteKey(lpszVerIndProgID);
    OLECHAR wClsid[40];
    ::StringFromGUID2(clsid, wClsid, 40);
    TCHAR szKey[64];
#ifdef UNICODE
    lstrcpy(szKey, TEXT("CLSID\\"));
    lstrcat(szKey, wClsid);
#else
    char a[40];
    ::WideCharToMultiByte(CP_ACP, 0, wClsid, -1, a, 40, NULL, NULL);
    lstrcpy(szKey, "CLSID\\");
    lstrcat(szKey, a);
#endif
    key.RecurseDeleteKey(szKey);
    key.Detach();
    return S_OK;
  }
  HRESULT UpdateRegistryClass(const CLSID& clsid, LPCTSTR lpszProgID, LPCTSTR lpszVerIndProgID,
                              UINT nDescID, DWORD dwFlags, BOOL bRegister) {
    return bRegister ? RegisterClassHelper(clsid, lpszProgID, lpszVerIndProgID, nDescID, dwFlags)
                     : UnregisterClassHelper(clsid, lpszProgID, lpszVerIndProgID);
  }

  // The module of an EXE server takes the same functions; it ends when
  // its last object goes (see CComModule::Unlock's override in the
  // program, or CAtlExeModuleT).
  HRESULT RegisterAppId(LPCTSTR) { return S_OK; }
};

// ---------------------------------------------------------------------
// ATL 7's module classes, for code whose module derives from them:
//
//   class CMyModule : public CAtlDllModuleT<CMyModule> {
//   public:
//     DECLARE_LIBID(LIBID_MyLib)
//     DECLARE_REGISTRY_APPID_RESOURCEID(IDR_MYAPP, "{...}")
//   };
//   CMyModule _AtlModule;
//   STDAPI DllGetClassObject(REFCLSID c, REFIID i, LPVOID* p)
//   { return _AtlModule.DllGetClassObject(c, i, p); }
//
// Their object map is OBJECT_ENTRY_AUTO's.

#define DECLARE_LIBID(libid)                 \
  static void InitLibId() {                  \
    ATL::CAtlModule::m_libid = libid;        \
    ATL::_pAtlModule->m_pLibID = &libid;     \
  }

#define DECLARE_REGISTRY_APPID_RESOURCEID(resid, appid)                                  \
  static LPCOLESTR GetAppId() { return OLESTR(appid); }                                  \
  static TCHAR* GetAppIdT() { return (TCHAR*)_T(appid); }                                \
  static HRESULT WINAPI UpdateRegistryAppId(BOOL bRegister) {                            \
    ATL::_ATL_REGMAP_ENTRY aMapEntries[] = {{OLESTR("APPID"), GetAppId()}, {NULL, NULL}}; \
    return ATL::_pAtlModule->UpdateRegistryFromResource(resid, bRegister, aMapEntries);  \
  }

template <class T>
class CAtlModuleT : public CComModule {
public:
  CAtlModuleT() {
    T::InitLibId();
    m_plibid = m_pLibID;
  }
  static void InitLibId() {}
  static HRESULT WINAPI UpdateRegistryAppId(BOOL) { return S_OK; }
  // The module's AppID (DECLARE_REGISTRY_APPID_RESOURCEID gives one).
  static LPCOLESTR GetAppId() { return NULL; }

  HRESULT RegisterServer(BOOL bRegTypeLib = FALSE, const CLSID* pCLSID = NULL) {
    HRESULT hr = T::UpdateRegistryAppId(TRUE);
    if (SUCCEEDED(hr)) hr = CComModule::RegisterServer(bRegTypeLib, pCLSID);
    return hr;
  }
  HRESULT UnregisterServer(BOOL bUnRegTypeLib, const CLSID* pCLSID = NULL) {
    CComModule::UnregisterServer(bUnRegTypeLib, pCLSID);
    T::UpdateRegistryAppId(FALSE);
    return S_OK;
  }
};

template <class T>
class CAtlDllModuleT : public CAtlModuleT<T> {
public:
  CAtlDllModuleT() {
    this->m_hInst = this->m_hInstResource = this->m_hInstTypeLib =
        _AtlBaseModule.GetModuleInstance();
  }
  BOOL WINAPI DllMain(DWORD dwReason, LPVOID) {
    if (dwReason == DLL_PROCESS_ATTACH) {
      for (_ATL_OBJMAP_ENTRY* e = this->FirstEntry(); e != NULL; e = this->NextEntry(e))
        if (e->pfnObjectMain != NULL) e->pfnObjectMain(true);
    } else if (dwReason == DLL_PROCESS_DETACH) {
      this->Term();
    }
    return TRUE;
  }
  HRESULT DllCanUnloadNow() { return this->GetLockCount() == 0 ? S_OK : S_FALSE; }
  HRESULT DllGetClassObject(REFCLSID rclsid, REFIID riid, LPVOID* ppv) {
    return this->GetClassObject(rclsid, riid, ppv);
  }
  HRESULT DllRegisterServer(BOOL bRegTypeLib = TRUE) {
    return static_cast<T*>(this)->RegisterServer(bRegTypeLib);
  }
  HRESULT DllUnregisterServer(BOOL bUnRegTypeLib = TRUE) {
    return static_cast<T*>(this)->UnregisterServer(bUnRegTypeLib);
  }
};

// An EXE server: registers its class objects, runs a message loop until
// its last object and lock go, and handles /RegServer and /UnregServer.
template <class T>
class CAtlExeModuleT : public CAtlModuleT<T> {
public:
  DWORD m_dwMainThreadID;
  bool m_bDelayShutdown;
  DWORD m_dwTimeOut;

  CAtlExeModuleT() : m_dwMainThreadID(::GetCurrentThreadId()), m_bDelayShutdown(true),
                     m_dwTimeOut(5000) {
    this->m_hInst = this->m_hInstResource = this->m_hInstTypeLib =
        _AtlBaseModule.GetModuleInstance();
  }
  LONG Unlock() {
    LONG l = CComModule::Unlock();
    if (l == 0) ::PostThreadMessage(m_dwMainThreadID, WM_QUIT, 0, 0);
    return l;
  }
  static HRESULT InitializeCom() { return ::CoInitialize(NULL); }
  static void UninitializeCom() { ::CoUninitialize(); }

  bool ParseCommandLine(LPCTSTR lpCmdLine, HRESULT* pnRetCode) {
    *pnRetCode = S_OK;
    for (LPCTSTR p = lpCmdLine; p != NULL && *p; p++) {
      if (*p != TEXT('/') && *p != TEXT('-')) continue;
      if (lstrcmpi(p + 1, TEXT("UnregServer")) == 0 ||
          CompareString(LOCALE_USER_DEFAULT, NORM_IGNORECASE, p + 1, 11, TEXT("UnregServer"), 11) ==
              CSTR_EQUAL) {
        *pnRetCode = static_cast<T*>(this)->UnregisterServer(TRUE);
        return false;
      }
      if (CompareString(LOCALE_USER_DEFAULT, NORM_IGNORECASE, p + 1, 9, TEXT("RegServer"), 9) ==
          CSTR_EQUAL) {
        *pnRetCode = static_cast<T*>(this)->RegisterServer(TRUE);
        return false;
      }
    }
    return true;
  }
  HRESULT PreMessageLoop(int) {
    return this->RegisterClassObjects(CLSCTX_LOCAL_SERVER, REGCLS_MULTIPLEUSE);
  }
  HRESULT PostMessageLoop() { return this->RevokeClassObjects(); }
  void RunMessageLoop() {
    MSG msg;
    while (::GetMessage(&msg, NULL, 0, 0) > 0) {
      ::TranslateMessage(&msg);
      ::DispatchMessage(&msg);
    }
  }
  int Run(int nShowCmd = SW_HIDE) {
    T* pT = static_cast<T*>(this);
    HRESULT hr = pT->PreMessageLoop(nShowCmd);
    if (hr == S_OK) {
      pT->RunMessageLoop();
      hr = pT->PostMessageLoop();
    }
    return (int)hr;
  }
  int WinMain(int nShowCmd) {
    HRESULT hr = T::InitializeCom();
    if (FAILED(hr)) return (int)hr;
    LPTSTR lpCmdLine = ::GetCommandLine();
    T* pT = static_cast<T*>(this);
    if (pT->ParseCommandLine(lpCmdLine, &hr)) hr = (HRESULT)pT->Run(nShowCmd);
    T::UninitializeCom();
    return (int)hr;
  }
};

// A Windows service that is also a COM server (the AppID's LocalService
// names it). /Service registers it and installs it with the service
// control manager, /RegServer registers it as an ordinary local server,
// and /UnregServer undoes both. Started by the service control manager
// it runs as a service; started otherwise, as a local server.
template <class T, UINT nServiceNameID>
class CAtlServiceModuleT : public CAtlExeModuleT<T> {
public:
  TCHAR m_szServiceName[256];
  SERVICE_STATUS_HANDLE m_hServiceStatus;
  SERVICE_STATUS m_status;
  BOOL m_bService;
  DWORD dwThreadID;

  CAtlServiceModuleT() : m_hServiceStatus(NULL), m_bService(TRUE), dwThreadID(0) {
    m_szServiceName[0] = 0;
    ::LoadString(_AtlBaseModule.GetResourceInstance(), nServiceNameID, m_szServiceName,
                 sizeof(m_szServiceName) / sizeof(TCHAR));
    ::memset(&m_status, 0, sizeof(m_status));
    m_status.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    m_status.dwCurrentState = SERVICE_STOPPED;
    m_status.dwControlsAccepted = SERVICE_ACCEPT_STOP;
  }

  int WinMain(int nShowCmd) {
    T* pT = static_cast<T*>(this);
    HRESULT hr = S_OK;
    if (pT->ParseCommandLine(::GetCommandLine(), &hr)) hr = pT->Start(nShowCmd);
    return (int)hr;
  }

  // A service when the AppID says so (and the dispatcher takes it), a
  // local server otherwise.
  HRESULT Start(int nShowCmd) {
    T* pT = static_cast<T*>(this);
    m_bService = _IsLocalService();
    if (m_bService) {
      SERVICE_TABLE_ENTRY st[] = {{m_szServiceName, _ServiceMain}, {NULL, NULL}};
      if (::StartServiceCtrlDispatcher(st) == 0) m_status.dwWin32ExitCode = ::GetLastError();
      return (HRESULT)m_status.dwWin32ExitCode;
    }
    m_status.dwWin32ExitCode = (DWORD)pT->Run(nShowCmd);
    return (HRESULT)m_status.dwWin32ExitCode;
  }

  void ServiceMain(DWORD dwArgc, LPTSTR* lpszArgv) {
    (void)dwArgc;
    (void)lpszArgv;
    T* pT = static_cast<T*>(this);
    m_hServiceStatus = ::RegisterServiceCtrlHandler(m_szServiceName, _Handler);
    if (m_hServiceStatus == NULL) {
      LogEvent(TEXT("Handler not installed"));
      return;
    }
    SetServiceStatus(SERVICE_START_PENDING);
    m_status.dwWin32ExitCode = S_OK;
    m_status.dwCheckPoint = 0;
    m_status.dwWaitHint = 0;
    m_status.dwWin32ExitCode = (DWORD)pT->Run(SW_HIDE);
    SetServiceStatus(SERVICE_STOPPED);
  }

  HRESULT Run(int nShowCmd = SW_HIDE) {
    T* pT = static_cast<T*>(this);
    HRESULT hr = T::InitializeCom();
    if (FAILED(hr)) return hr;
    hr = pT->PreMessageLoop(nShowCmd);
    if (hr == S_OK) {
      if (m_bService) {
        LogEvent(TEXT("Service started"));
        SetServiceStatus(SERVICE_RUNNING);
      }
      pT->RunMessageLoop();
    }
    if (SUCCEEDED(hr)) hr = pT->PostMessageLoop();
    T::UninitializeCom();
    return hr;
  }
  HRESULT PreMessageLoop(int nShowCmd) {
    dwThreadID = ::GetCurrentThreadId();
    this->m_dwMainThreadID = dwThreadID;
    (void)nShowCmd;
    return this->RegisterClassObjects(CLSCTX_LOCAL_SERVER, REGCLS_MULTIPLEUSE);
  }
  // A service keeps running when its last object goes.
  LONG Unlock() {
    if (m_bService) return CAtlModule::Unlock();
    return CAtlExeModuleT<T>::Unlock();
  }

  void OnStop() {
    SetServiceStatus(SERVICE_STOP_PENDING);
    ::PostThreadMessage(dwThreadID, WM_QUIT, 0, 0);
  }
  void OnPause() {}
  void OnContinue() {}
  void OnInterrogate() {}
  void OnShutdown() {}
  void OnUnknownRequest(DWORD) { LogEvent(TEXT("Bad service request")); }

  void Handler(DWORD dwOpcode) {
    T* pT = static_cast<T*>(this);
    switch (dwOpcode) {
    case SERVICE_CONTROL_STOP: pT->OnStop(); break;
    case SERVICE_CONTROL_PAUSE: pT->OnPause(); break;
    case SERVICE_CONTROL_CONTINUE: pT->OnContinue(); break;
    case SERVICE_CONTROL_INTERROGATE: pT->OnInterrogate(); break;
    case SERVICE_CONTROL_SHUTDOWN: pT->OnShutdown(); break;
    default: pT->OnUnknownRequest(dwOpcode); break;
    }
  }
  void SetServiceStatus(DWORD dwState) {
    m_status.dwCurrentState = dwState;
    if (m_hServiceStatus != NULL) ::SetServiceStatus(m_hServiceStatus, &m_status);
  }

  BOOL IsInstalled() {
    BOOL bResult = FALSE;
    SC_HANDLE hSCM = ::OpenSCManager(NULL, NULL, SC_MANAGER_CONNECT);
    if (hSCM != NULL) {
      SC_HANDLE hService = ::OpenService(hSCM, m_szServiceName, SERVICE_QUERY_CONFIG);
      if (hService != NULL) {
        bResult = TRUE;
        ::CloseServiceHandle(hService);
      }
      ::CloseServiceHandle(hSCM);
    }
    return bResult;
  }
  BOOL Install() {
    if (IsInstalled()) return TRUE;
    TCHAR szFilePath[MAX_PATH + 2];
    DWORD n = ::GetModuleFileName(NULL, szFilePath + 1, MAX_PATH);
    if (n == 0 || n == MAX_PATH) return FALSE;
    // The path quoted, as it may hold spaces.
    szFilePath[0] = TEXT('"');
    szFilePath[n + 1] = TEXT('"');
    szFilePath[n + 2] = 0;
    TCHAR szQuoted[MAX_PATH + 3];
    ::lstrcpyn(szQuoted, szFilePath, MAX_PATH + 3);
    SC_HANDLE hSCM = ::OpenSCManager(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    if (hSCM == NULL) return FALSE;
    SC_HANDLE hService = ::CreateService(hSCM, m_szServiceName, m_szServiceName,
                                         SERVICE_ALL_ACCESS, SERVICE_WIN32_OWN_PROCESS,
                                         SERVICE_DEMAND_START, SERVICE_ERROR_NORMAL, szQuoted,
                                         NULL, NULL, TEXT("RPCSS\0"), NULL, NULL);
    if (hService == NULL) {
      ::CloseServiceHandle(hSCM);
      return FALSE;
    }
    ::CloseServiceHandle(hService);
    ::CloseServiceHandle(hSCM);
    return TRUE;
  }
  BOOL Uninstall() {
    if (!IsInstalled()) return TRUE;
    SC_HANDLE hSCM = ::OpenSCManager(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    if (hSCM == NULL) return FALSE;
    SC_HANDLE hService = ::OpenService(hSCM, m_szServiceName, SERVICE_STOP | DELETE);
    if (hService == NULL) {
      ::CloseServiceHandle(hSCM);
      return FALSE;
    }
    SERVICE_STATUS status;
    ::ControlService(hService, SERVICE_CONTROL_STOP, &status);
    BOOL bDelete = ::DeleteService(hService);
    ::CloseServiceHandle(hService);
    ::CloseServiceHandle(hSCM);
    return bDelete;
  }

  // A message in the event log, under the service's name.
  void __cdecl LogEvent(LPCTSTR pszFormat, ...) {
    TCHAR chMsg[256];
    va_list pArg;
    va_start(pArg, pszFormat);
    ::wvsprintf(chMsg, pszFormat, pArg);
    va_end(pArg);
    LogEventEx(0, chMsg);
  }
  void LogEventEx(int id, LPCTSTR pszMessage = NULL, WORD type = EVENTLOG_INFORMATION_TYPE) {
    HANDLE hEventSource = ::RegisterEventSource(NULL, m_szServiceName);
    if (hEventSource == NULL) return;
    LPCTSTR lpszStrings[1] = {pszMessage};
    ::ReportEvent(hEventSource, type, 0, (DWORD)id, NULL, pszMessage != NULL ? 1 : 0, 0,
                  pszMessage != NULL ? lpszStrings : NULL, NULL);
    ::DeregisterEventSource(hEventSource);
  }

  bool ParseCommandLine(LPCTSTR lpCmdLine, HRESULT* pnRetCode) {
    T* pT = static_cast<T*>(this);
    *pnRetCode = S_OK;
    for (LPCTSTR p = lpCmdLine; p != NULL && *p; p++) {
      if (*p != TEXT('/') && *p != TEXT('-')) continue;
      if (_IsWord(p + 1, TEXT("UnregServer"))) {
        *pnRetCode = pT->UnregisterAppId();
        if (SUCCEEDED(*pnRetCode)) *pnRetCode = pT->UnregisterServer(TRUE);
        return false;
      }
      if (_IsWord(p + 1, TEXT("RegServer"))) {
        *pnRetCode = pT->RegisterAppId(false);
        if (SUCCEEDED(*pnRetCode)) *pnRetCode = pT->RegisterServer(TRUE);
        return false;
      }
      if (_IsWord(p + 1, TEXT("Service"))) {
        *pnRetCode = pT->RegisterAppId(true);
        if (SUCCEEDED(*pnRetCode)) *pnRetCode = pT->RegisterServer(TRUE);
        return false;
      }
    }
    return true;
  }

  // The AppID's LocalService value names the service (bService) or is
  // removed (a local server); the service is installed or uninstalled to
  // match.
  HRESULT RegisterAppId(bool bService = false) {
    T* pT = static_cast<T*>(this);
    if (!Uninstall()) return E_FAIL;
    HRESULT hr = T::UpdateRegistryAppId(TRUE);
    if (FAILED(hr)) return hr;
    LPCOLESTR pszAppId = T::GetAppId();
    if (pszAppId != NULL) {
      CRegKey keyAppID;
      LONG lRes = keyAppID.Open(HKEY_CLASSES_ROOT, TEXT("AppID"), KEY_WRITE);
      if (lRes != ERROR_SUCCESS) return AtlHresultFromWin32((DWORD)lRes);
      CRegKey key;
      lRes = key.Create(keyAppID, _AppIdT(pszAppId));
      if (lRes != ERROR_SUCCESS) return AtlHresultFromWin32((DWORD)lRes);
      key.DeleteValue(TEXT("LocalService"));
      if (bService) {
        key.SetStringValue(TEXT("LocalService"), m_szServiceName);
        key.SetStringValue(TEXT("ServiceParameters"), TEXT("-Service"));
      }
    }
    if (bService && !pT->Install()) return AtlHresultFromLastError();
    return S_OK;
  }
  HRESULT UnregisterAppId() {
    if (!Uninstall()) return E_FAIL;
    LPCOLESTR pszAppId = T::GetAppId();
    if (pszAppId != NULL) {
      CRegKey keyAppID;
      if (keyAppID.Open(HKEY_CLASSES_ROOT, TEXT("AppID"), KEY_WRITE) == ERROR_SUCCESS) {
        CRegKey key;
        if (key.Open(keyAppID, _AppIdT(pszAppId), KEY_WRITE) == ERROR_SUCCESS) {
          key.DeleteValue(TEXT("LocalService"));
          key.DeleteValue(TEXT("ServiceParameters"));
        }
      }
    }
    return T::UpdateRegistryAppId(FALSE);
  }
  HRESULT InitializeSecurity() { return S_OK; }

  static void WINAPI _ServiceMain(DWORD dwArgc, LPTSTR* lpszArgv) {
    static_cast<T*>(_pAtlModule)->ServiceMain(dwArgc, lpszArgv);
  }
  static void WINAPI _Handler(DWORD dwOpcode) { static_cast<T*>(_pAtlModule)->Handler(dwOpcode); }

private:
  static bool _IsWord(LPCTSTR p, LPCTSTR pszWord) {
    int n = ::lstrlen(pszWord);
    if (::CompareString(LOCALE_USER_DEFAULT, NORM_IGNORECASE, p, n, pszWord, n) != CSTR_EQUAL)
      return false;
    return p[n] == 0 || p[n] == TEXT(' ') || p[n] == TEXT('\t') || p[n] == TEXT('"');
  }
  // The AppID as TCHARs.
  struct _AppIdT {
    TCHAR sz[64];
    explicit _AppIdT(LPCOLESTR psz) {
      int i = 0;
      for (; psz[i] && i < 63; i++) sz[i] = (TCHAR)psz[i];
      sz[i] = 0;
    }
    operator LPCTSTR() const { return sz; }
  };
  // Whether the AppID names this service as its LocalService.
  bool _IsLocalService() {
    LPCOLESTR pszAppId = T::GetAppId();
    if (pszAppId == NULL) return false;
    CRegKey keyAppID;
    if (keyAppID.Open(HKEY_CLASSES_ROOT, TEXT("AppID"), KEY_READ) != ERROR_SUCCESS) return false;
    CRegKey key;
    if (key.Open(keyAppID, _AppIdT(pszAppId), KEY_READ) != ERROR_SUCCESS) return false;
    TCHAR szValue[256];
    ULONG nChars = 256;
    if (key.QueryStringValue(TEXT("LocalService"), szValue, &nChars) != ERROR_SUCCESS) return false;
    return ::lstrcmpi(szValue, m_szServiceName) == 0;
  }
};

// ---------------------------------------------------------------------
// String resources.

inline int AtlLoadString(UINT nID, LPTSTR lpBuffer, int nBufferMax) {
  return ::LoadString(_AtlBaseModule.GetResourceInstance(), nID, lpBuffer, nBufferMax);
}
inline HINSTANCE AtlFindStringResourceInstance(UINT, WORD = 0) {
  return _AtlBaseModule.GetResourceInstance();
}

}  // namespace ATL

#ifndef _ATL_NO_AUTOMATIC_NAMESPACE
using namespace ATL;
#endif

#endif  // __ATLBASE_H__
