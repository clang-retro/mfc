// atlperf.h - performance counters with ATL: CPerfMon publishes the
// counters of its categories (CPerfObject classes with counter maps) for
// the performance monitor, from the application through shared memory to
// the collection functions a performance DLL exports.
//
//   class CRequests : public CPerfObject {
//   public:
//     DECLARE_PERF_CATEGORY(CRequests, 1, _T("Requests"), _T("..."), -1)
//     BEGIN_COUNTER_MAP(CRequests)
//       DEFINE_COUNTER(m_nCount, _T("Count"), _T("..."), PERF_COUNTER_RAWCOUNT, 0)
//     END_COUNTER_MAP()
//     ULONG m_nCount;
//   };
//   class CMyPerfMon : public CPerfMon {
//   public:
//     BEGIN_PERF_MAP(_T("MyApp"))
//       CHAIN_PERF_CATEGORY(CRequests)
//     END_PERF_MAP()
//   };
//
// The application calls Initialize, then CreateInstanceT and changes the
// counters it gets (under CPerfLock where they must agree). The DLL's
// exports call Open, Collect and Close; Register writes the service's
// Performance key and the names and help to Perflib, Unregister removes
// them. Names and help may be resource IDs (MAKEINTRESOURCE). PERFREG_ENTRY
// lists a CPerfMon for the exports OpenPerfMon, CollectPerfMon and
// ClosePerfMon, which a DLL gets (unless _ATL_PERF_NOEXPORT), and for
// AtlPerfRegister and AtlPerfUnregister.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#ifndef __ATLPERF_H__
#define __ATLPERF_H__

#include <atlbase.h>
#include <winperf.h>
#include <stdio.h>
#include <string>
#include <utility>
#include <vector>

#if !defined(_ATL_NO_DEFAULT_LIBS) && defined(_WIN32)
__asm__(".section .drectve,\"yn\"\n"
        ".ascii \" -defaultlib:advapi32\"\n"
        ".text");
#endif

#ifndef ATLPERF_DEFAULT_MAXINSTNAMELENGTH
#define ATLPERF_DEFAULT_MAXINSTNAMELENGTH 64
#endif
#ifndef ATLPERF_DEFAULT_MAXINSTANCES
#define ATLPERF_DEFAULT_MAXINSTANCES 256
#endif

namespace ATL {

struct _ATL_PERF_COUNTER_INFO {
  DWORD dwCounterId;
  LPCTSTR szName;
  LPCTSTR szHelp;
  DWORD dwDetailLevel;
  DWORD dwCounterType;
  ULONG nMaxCounterSize;
  ULONG nDataOffset;
  LONG nDefaultScale;
};

struct _ATL_PERF_CATEGORY_INFO {
  DWORD dwCategoryId;
  LPCTSTR szName;
  LPCTSTR szHelp;
  DWORD dwDetailLevel;
  LONG nDefaultCounter;
  bool bInstanceless;
  ULONG nStructSize;
  ULONG nMaxInstanceNameLen;
  const _ATL_PERF_COUNTER_INFO* pCounters;
  ULONG nCounters;
};

typedef const _ATL_PERF_CATEGORY_INFO* (*_ATL_PERF_GETCATEGORY)();

// An instance of a category, in the shared memory: these, then the
// category's counters, then the instance's name.
class CPerfObject {
public:
  LPWSTR GetInstanceName() { return (LPWSTR)((BYTE*)this + m_nInstanceNameOffset); }

  ULONG m_nAllocSize;
  DWORD m_dwCategoryId;
  DWORD m_dwInstance;
  ULONG m_nRefCount;
  ULONG m_nInstanceNameOffset;
};

// The offset of a member, as offsetof but for classes with bases.
#define _ATL_PERF_OFFSET(cls, member) ((ULONG)((size_t) & (((cls*)8)->member) - 8))

#define DECLARE_PERF_CATEGORY_EX(dwCategoryId, namestring, helpstring, detail, defcounter, instanceless, \
                                 structsize, maxinstnamelen)                                         \
  static const DWORD kCategoryId = dwCategoryId;                                                      \
  static const ATL::_ATL_PERF_CATEGORY_INFO* _GetPerfCategory() {                                     \
    static ATL::_ATL_PERF_CATEGORY_INFO s_info = {dwCategoryId, namestring, helpstring, detail,       \
                                                  defcounter, instanceless, (ULONG)(structsize),      \
                                                  maxinstnamelen, NULL, 0};                           \
    if (s_info.pCounters == NULL) s_info.pCounters = _GetPerfCounters(&s_info.nCounters);            \
    return &s_info;                                                                                   \
  }
#define DECLARE_PERF_CATEGORY(objectclass, dwCategoryId, namestring, helpstring, defcounter)               \
  DECLARE_PERF_CATEGORY_EX(dwCategoryId, namestring, helpstring, PERF_DETAIL_NOVICE, defcounter, false, \
                           sizeof(objectclass), ATLPERF_DEFAULT_MAXINSTNAMELENGTH)
#define DECLARE_PERF_CATEGORY_NO_INSTANCES(objectclass, dwCategoryId, namestring, helpstring, defcounter) \
  DECLARE_PERF_CATEGORY_EX(dwCategoryId, namestring, helpstring, PERF_DETAIL_NOVICE, defcounter, true, \
                           sizeof(objectclass), 0)

#define BEGIN_COUNTER_MAP(objectclass)                                           \
  typedef objectclass _PerfCounterClass;                                         \
  static const ATL::_ATL_PERF_COUNTER_INFO* _GetPerfCounters(ULONG* pnCount) {   \
    static const ATL::_ATL_PERF_COUNTER_INFO s_counters[] = {
#define DEFINE_COUNTER_EX(member, dwCounterId, namestring, helpstring, detail, countertype, maxcountersize, \
                          defscale)                                                                        \
  {dwCounterId,           namestring,      helpstring,                                                     \
   detail,                countertype,     (ULONG)(maxcountersize),                                       \
   _ATL_PERF_OFFSET(_PerfCounterClass, member), defscale},
#define DEFINE_COUNTER(member, namestring, helpstring, countertype, defscale)                       \
  DEFINE_COUNTER_EX(member, 0, namestring, helpstring, PERF_DETAIL_NOVICE, countertype,             \
                    sizeof(((_PerfCounterClass*)8)->member), defscale)
#define END_COUNTER_MAP()                                               \
  {0, NULL, NULL, 0, 0, 0, 0, 0}                                        \
  }                                                                     \
  ;                                                                     \
  *pnCount = (ULONG)(sizeof(s_counters) / sizeof(s_counters[0]) - 1);   \
  return s_counters;                                                    \
  }

#define BEGIN_PERF_MAP(AppName)                                           \
 public:                                                                  \
  LPCTSTR GetAppName() const { return AppName; }                          \
  const ATL::_ATL_PERF_GETCATEGORY* _GetPerfMap() const {                 \
    static const ATL::_ATL_PERF_GETCATEGORY s_map[] = {
#define CHAIN_PERF_CATEGORY(categoryclass) &categoryclass::_GetPerfCategory,
#define END_PERF_MAP() \
  NULL                 \
  }                    \
  ;                    \
  return s_map;        \
  }

// ---------------------------------------------------------------------

class CPerfMon {
public:
  CPerfMon() : m_hMutex(NULL), m_bInitialized(false), m_dwFirstCounter(0), m_dwFirstHelp(0), m_bOpen(false) {}
  virtual ~CPerfMon() { UnInitialize(); }

  virtual LPCTSTR GetAppName() const = 0;
  virtual const _ATL_PERF_GETCATEGORY* _GetPerfMap() const = 0;

  // ----- In the application.

  // The shared memory of each category, and the lock.
  HRESULT Initialize() {
    if (m_bInitialized) return S_OK;
    m_hMutex = ::CreateMutexW(_Security(), FALSE, _ObjectName(L"LOCK").c_str());
    if (m_hMutex == NULL) return AtlHresultFromLastError();
    std::vector<const _ATL_PERF_CATEGORY_INFO*> cats = _Categories();
    for (const _ATL_PERF_CATEGORY_INFO* p : cats) {
      _Mapping m;
      m.pInfo = p;
      ULONG nSlots = p->bInstanceless ? 1 : ATLPERF_DEFAULT_MAXINSTANCES;
      ULONG nSlotSize = _SlotSize(p);
      DWORD dwSize = sizeof(_Header) + nSlots * nSlotSize;
      m.hMap = ::CreateFileMappingW(INVALID_HANDLE_VALUE, _Security(), PAGE_READWRITE, 0, dwSize,
                                    _MapName(p->dwCategoryId).c_str());
      if (m.hMap == NULL) {
        HRESULT hr = AtlHresultFromLastError();
        UnInitialize();
        return hr;
      }
      bool bExisted = ::GetLastError() == ERROR_ALREADY_EXISTS;
      m.pHeader = (_Header*)::MapViewOfFile(m.hMap, FILE_MAP_ALL_ACCESS, 0, 0, 0);
      if (m.pHeader == NULL) {
        HRESULT hr = AtlHresultFromLastError();
        ::CloseHandle(m.hMap);
        UnInitialize();
        return hr;
      }
      if (!bExisted || m.pHeader->dwMagic != kMagic) {
        memset(m.pHeader, 0, dwSize);
        m.pHeader->dwMagic = kMagic;
        m.pHeader->nSlotSize = nSlotSize;
        m.pHeader->nSlots = nSlots;
      }
      m_maps.push_back(m);
      // An instanceless category has its one instance from the start.
      if (p->bInstanceless) {
        CPerfObject* pObj = _Slot(m, 0);
        if (pObj->m_nAllocSize == 0) _InitSlot(m, pObj, 0, L"");
      }
    }
    m_bInitialized = true;
    return S_OK;
  }
  void UnInitialize() {
    _CloseMaps();
    if (m_hMutex != NULL) ::CloseHandle(m_hMutex);
    m_hMutex = NULL;
    m_bInitialized = false;
  }

  // A new instance (or the one with this ID already, counted again).
  HRESULT CreateInstance(DWORD dwCategoryId, DWORD dwInstance, LPCWSTR szInstanceName,
                         CPerfObject** ppInstance) {
    if (ppInstance == NULL) return E_POINTER;
    *ppInstance = NULL;
    CPerfLock lock(this);
    if (FAILED(lock.GetStatus())) return lock.GetStatus();
    _Mapping* pMap = _FindMap(dwCategoryId);
    if (pMap == NULL) return E_INVALIDARG;
    if (pMap->pInfo->bInstanceless) {
      CPerfObject* p = _Slot(*pMap, 0);
      p->m_nRefCount++;
      *ppInstance = p;
      return S_OK;
    }
    if (szInstanceName == NULL) szInstanceName = L"";
    if (wcslen(szInstanceName) > pMap->pInfo->nMaxInstanceNameLen) return E_INVALIDARG;
    CPerfObject* pFree = NULL;
    for (ULONG i = 0; i < pMap->pHeader->nSlots; i++) {
      CPerfObject* p = _Slot(*pMap, i);
      if (p->m_nAllocSize == 0) {
        if (pFree == NULL) pFree = p;
        continue;
      }
      if (p->m_dwInstance == dwInstance) {
        p->m_nRefCount++;
        *ppInstance = p;
        return S_OK;
      }
    }
    if (pFree == NULL) return E_OUTOFMEMORY;
    _InitSlot(*pMap, pFree, dwInstance, szInstanceName);
    *ppInstance = pFree;
    return S_OK;
  }
  // The instance with this name, or a new one with an unused ID.
  HRESULT CreateInstanceByName(DWORD dwCategoryId, LPCWSTR szInstanceName, CPerfObject** ppInstance) {
    if (ppInstance == NULL) return E_POINTER;
    *ppInstance = NULL;
    if (szInstanceName == NULL) return E_INVALIDARG;
    DWORD dwInstance = 0;
    {
      CPerfLock lock(this);
      if (FAILED(lock.GetStatus())) return lock.GetStatus();
      _Mapping* pMap = _FindMap(dwCategoryId);
      if (pMap == NULL) return E_INVALIDARG;
      for (ULONG i = 0; i < pMap->pHeader->nSlots; i++) {
        CPerfObject* p = _Slot(*pMap, i);
        if (p->m_nAllocSize == 0) continue;
        if (wcscmp(p->GetInstanceName(), szInstanceName) == 0) {
          p->m_nRefCount++;
          *ppInstance = p;
          return S_OK;
        }
        if (p->m_dwInstance >= dwInstance) dwInstance = p->m_dwInstance + 1;
      }
    }
    return CreateInstance(dwCategoryId, dwInstance, szInstanceName, ppInstance);
  }
  template <class T>
  HRESULT CreateInstanceT(DWORD dwInstance, LPCWSTR szInstanceName, T** ppInstance) {
    return CreateInstance(T::kCategoryId, dwInstance, szInstanceName, (CPerfObject**)ppInstance);
  }
  template <class T>
  HRESULT CreateInstanceByNameT(LPCWSTR szInstanceName, T** ppInstance) {
    return CreateInstanceByName(T::kCategoryId, szInstanceName, (CPerfObject**)ppInstance);
  }
  HRESULT ReleaseInstance(CPerfObject* pInstance) {
    if (pInstance == NULL) return E_INVALIDARG;
    CPerfLock lock(this);
    if (FAILED(lock.GetStatus())) return lock.GetStatus();
    _Mapping* pMap = _FindMap(pInstance->m_dwCategoryId);
    if (pMap == NULL || pInstance->m_nAllocSize == 0 || pInstance->m_nRefCount == 0) return E_INVALIDARG;
    if (--pInstance->m_nRefCount == 0 && !pMap->pInfo->bInstanceless)
      memset(pInstance, 0, pMap->pHeader->nSlotSize);
    return S_OK;
  }

  HRESULT LockPerf(DWORD dwTimeout = INFINITE) {
    if (m_hMutex == NULL) {
      m_hMutex = ::OpenMutexW(SYNCHRONIZE, FALSE, _ObjectName(L"LOCK").c_str());
      if (m_hMutex == NULL) return S_OK;  // no application: nothing to agree with
    }
    DWORD dw = ::WaitForSingleObject(m_hMutex, dwTimeout);
    if (dw == WAIT_OBJECT_0 || dw == WAIT_ABANDONED) return S_OK;
    if (dw == WAIT_TIMEOUT) return HRESULT_FROM_WIN32(ERROR_TIMEOUT);
    return AtlHresultFromLastError();
  }
  void UnlockPerf() {
    if (m_hMutex != NULL) ::ReleaseMutex(m_hMutex);
  }

  // ----- Registration.

  HRESULT Register(LPCTSTR szOpenFunc, LPCTSTR szCollectFunc, LPCTSTR szCloseFunc,
                   HINSTANCE hDllInstance = NULL) {
    if (szOpenFunc == NULL || szCollectFunc == NULL || szCloseFunc == NULL) return E_INVALIDARG;
    WCHAR szModule[MAX_PATH];
    if (::GetModuleFileNameW(hDllInstance != NULL ? hDllInstance : _AtlBaseModule.GetModuleInstance(), szModule,
                             MAX_PATH) == 0)
      return AtlHresultFromLastError();
    HKEY hKey = NULL;
    LONG l = ::RegCreateKeyExW(HKEY_LOCAL_MACHINE, _PerfKey().c_str(), 0, NULL, 0, KEY_ALL_ACCESS, NULL, &hKey,
                               NULL);
    if (l != ERROR_SUCCESS) return HRESULT_FROM_WIN32(l);
    _SetString(hKey, L"Library", szModule);
    _SetString(hKey, L"Open", _Wide(szOpenFunc).c_str());
    _SetString(hKey, L"Collect", _Wide(szCollectFunc).c_str());
    _SetString(hKey, L"Close", _Wide(szCloseFunc).c_str());
    ::RegCloseKey(hKey);
    return RegisterAllStrings(hDllInstance);
  }
  // The names and help, at indexes Perflib gives out, for one language
  // (neutral is English, 009). Where Perflib's text is in the registry
  // (Windows 2000 and XP) they go there; where it is not (later Windows,
  // whose Perflib\009 is a view of the performance text), lodctr's
  // LoadPerfCounterTextStrings loads them from a .ini it is given.
  HRESULT RegisterStrings(LANGID wLanguage = MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL),
                          HINSTANCE hResInstance = NULL) {
    if (hResInstance == NULL) hResInstance = _AtlBaseModule.GetResourceInstance();
    // Names and help in order: each category, then its counters.
    std::vector<std::pair<std::wstring, std::wstring> > texts;
    std::vector<bool> isObject;
    for (const _ATL_PERF_CATEGORY_INFO* p : _Categories()) {
      texts.push_back(std::make_pair(_Text(p->szName, hResInstance), _Text(p->szHelp, hResInstance)));
      isObject.push_back(true);
      for (ULONG i = 0; i < p->nCounters; i++) {
        texts.push_back(std::make_pair(_Text(p->pCounters[i].szName, hResInstance),
                                       _Text(p->pCounters[i].szHelp, hResInstance)));
        isObject.push_back(false);
      }
    }
    if (texts.empty()) return E_UNEXPECTED;
    WORD wPrimary = PRIMARYLANGID(wLanguage);
    if (wPrimary == LANG_NEUTRAL) wPrimary = LANG_ENGLISH;
    WCHAR szLang[8];
    swprintf(szLang, 8, L"%03x", (unsigned)wPrimary);
    std::wstring strKey = std::wstring(kPerflib) + L"\\" + szLang;
    HKEY hKey = NULL;
    LONG l = ::RegCreateKeyExW(HKEY_LOCAL_MACHINE, strKey.c_str(), 0, NULL, 0, KEY_ALL_ACCESS, NULL, &hKey, NULL);
    if (l != ERROR_SUCCESS) return HRESULT_FROM_WIN32(l);
    if (_IsTextView(hKey)) {
      ::RegCloseKey(hKey);
      HRESULT hr = _LoadWithLodctr(texts, isObject, szLang);
      // Without lodctr's indexes (or with a lodctr that keeps nothing),
      // the counters still need theirs to be collected.
      DWORD dwFirst, dwLast, dwFirstHelp, dwLastHelp;
      if (FAILED(hr) || !_ReadIndexes(&dwFirst, &dwLast, &dwFirstHelp, &dwLastHelp))
        hr = _AllocateIndexes(&dwFirst, &dwFirstHelp);
      return hr;
    }
    DWORD dwFirst = 0, dwFirstHelp = 0;
    HRESULT hr = _AllocateIndexes(&dwFirst, &dwFirstHelp);
    if (FAILED(hr)) {
      ::RegCloseKey(hKey);
      return hr;
    }
    std::vector<std::pair<DWORD, std::wstring> > names, helps;
    for (size_t n = 0; n < texts.size(); n++) {
      names.push_back(std::make_pair(dwFirst + 2 * (DWORD)n, texts[n].first));
      helps.push_back(std::make_pair(dwFirstHelp + 2 * (DWORD)n, texts[n].second));
    }
    DWORD dwLast = dwFirst + 2 * (DWORD)(texts.size() - 1);
    DWORD dwLastHelp = dwFirstHelp + 2 * (DWORD)(texts.size() - 1);
    l = _ReplaceStrings(hKey, L"Counter", dwFirst, dwLast, names);
    if (l == ERROR_SUCCESS) l = _ReplaceStrings(hKey, L"Help", dwFirstHelp, dwLastHelp, helps);
    ::RegCloseKey(hKey);
    return HRESULT_FROM_WIN32(l);
  }
  HRESULT RegisterAllStrings(HINSTANCE hResInstance = NULL) {
    return RegisterStrings(MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL), hResInstance);
  }
  HRESULT Unregister() {
    if (_TextIsView()) {
      typedef DWORD(WINAPI * PFNUNLOAD)(LPWSTR, BOOL);
      HMODULE hLoadPerf = ::LoadLibraryW(L"loadperf.dll");
      PFNUNLOAD pfn = hLoadPerf != NULL ? (PFNUNLOAD)(void*)::GetProcAddress(hLoadPerf, "UnloadPerfCounterTextStringsW")
                                        : NULL;
      if (pfn != NULL) {
        std::wstring cmd = L"unlodctr " + _Wide(GetAppName());
        std::vector<WCHAR> buf(cmd.begin(), cmd.end());
        buf.push_back(0);
        pfn(&buf[0], TRUE);
      }
      if (hLoadPerf != NULL) ::FreeLibrary(hLoadPerf);
    }
    DWORD dwFirst, dwLast, dwFirstHelp, dwLastHelp;
    if (_ReadIndexes(&dwFirst, &dwLast, &dwFirstHelp, &dwLastHelp)) {
      HKEY hPerflib = NULL;
      if (::RegOpenKeyExW(HKEY_LOCAL_MACHINE, kPerflib, 0, KEY_ALL_ACCESS, &hPerflib) == ERROR_SUCCESS) {
        WCHAR szSub[64];
        for (DWORD i = 0; ::RegEnumKeyW(hPerflib, i, szSub, 64) == ERROR_SUCCESS; i++) {
          HKEY hLang = NULL;
          if (::RegOpenKeyExW(hPerflib, szSub, 0, KEY_ALL_ACCESS, &hLang) != ERROR_SUCCESS) continue;
          std::vector<std::pair<DWORD, std::wstring> > none;
          _ReplaceStrings(hLang, L"Counter", dwFirst, dwLast, none);
          _ReplaceStrings(hLang, L"Help", dwFirstHelp, dwLastHelp, none);
          ::RegCloseKey(hLang);
        }
        // The indexes go back when they were the last given out.
        if (_GetDword(hPerflib, L"Last Counter", 0) == dwLast) _SetDword(hPerflib, L"Last Counter", dwFirst - 2);
        if (_GetDword(hPerflib, L"Last Help", 0) == dwLastHelp) _SetDword(hPerflib, L"Last Help", dwFirstHelp - 2);
        ::RegCloseKey(hPerflib);
      }
    }
    std::wstring strService = std::wstring(L"SYSTEM\\CurrentControlSet\\Services\\") + _Wide(GetAppName());
    ::RegDeleteKeyW(HKEY_LOCAL_MACHINE, _PerfKey().c_str());
    ::RegDeleteKeyW(HKEY_LOCAL_MACHINE, strService.c_str());
    return S_OK;
  }

  // ----- In the performance DLL.

  DWORD Open(LPWSTR /*szDeviceNames*/) {
    DWORD dwLast, dwLastHelp;
    if (!_ReadIndexes(&m_dwFirstCounter, &dwLast, &m_dwFirstHelp, &dwLastHelp)) return ERROR_FILE_NOT_FOUND;
    m_bOpen = true;
    return ERROR_SUCCESS;
  }
  DWORD Close() {
    m_bOpen = false;
    if (!m_bInitialized) _CloseMaps();
    return ERROR_SUCCESS;
  }

  // The categories asked for ("Global", or their name indexes), with each
  // instance and its counters, written at *ppData.
  DWORD Collect(LPWSTR szValue, LPVOID* ppData, LPDWORD pcbBytes, LPDWORD pcObjectTypes) {
    if (ppData == NULL || pcbBytes == NULL || pcObjectTypes == NULL) return ERROR_INVALID_PARAMETER;
    DWORD cbMax = *pcbBytes;
    *pcObjectTypes = 0;
    if (!m_bOpen) {
      *pcbBytes = 0;
      return ERROR_SUCCESS;
    }
    bool bAll = szValue == NULL || *szValue == 0 || _wcsicmp(szValue, L"Global") == 0;
    if (!bAll && (_wcsnicmp(szValue, L"Foreign", 7) == 0 || _wcsicmp(szValue, L"Costly") == 0)) {
      *pcbBytes = 0;
      return ERROR_SUCCESS;
    }
    std::vector<DWORD> wanted;
    if (!bAll) {
      for (const WCHAR* p = szValue; *p;) {
        while (*p == L' ') p++;
        if (*p == 0) break;
        wanted.push_back((DWORD)wcstoul(p, (WCHAR**)&p, 10));
        while (*p && *p != L' ') p++;
      }
    }
    if (!m_bInitialized) _OpenMaps();
    BYTE* pStart = (BYTE*)*ppData;
    BYTE* pOut = pStart;
    LARGE_INTEGER liTime, liFreq;
    ::QueryPerformanceCounter(&liTime);
    ::QueryPerformanceFrequency(&liFreq);
    DWORD nIndex = 0, nObjects = 0;
    CPerfLock lock(this);
    for (const _ATL_PERF_CATEGORY_INFO* p : _Categories()) {
      DWORD dwNameIndex = m_dwFirstCounter + 2 * nIndex;
      DWORD dwHelpIndex = m_dwFirstHelp + 2 * nIndex;
      DWORD nFirstCounterPos = nIndex + 1;
      nIndex += 1 + p->nCounters;
      bool bWanted = bAll;
      for (DWORD w : wanted)
        if (w == dwNameIndex) bWanted = true;
      if (!bWanted) continue;
      DWORD cb = _WriteCategory(p, pOut, (DWORD)(cbMax - (pOut - pStart)), dwNameIndex, dwHelpIndex,
                                nFirstCounterPos, liTime, liFreq);
      if (cb == 0) {
        *pcbBytes = 0;
        *pcObjectTypes = 0;
        return ERROR_MORE_DATA;
      }
      pOut += cb;
      nObjects++;
    }
    *pcbBytes = (DWORD)(pOut - pStart);
    *pcObjectTypes = nObjects;
    *ppData = pOut;
    return ERROR_SUCCESS;
  }

  // The index a category's name has once registered (0 before Open).
  DWORD GetCategoryNameIndex(DWORD dwCategoryId) const {
    DWORD n = 0;
    for (const _ATL_PERF_CATEGORY_INFO* p : _Categories()) {
      if (p->dwCategoryId == dwCategoryId) return m_dwFirstCounter != 0 ? m_dwFirstCounter + 2 * n : 0;
      n += 1 + p->nCounters;
    }
    return 0;
  }

  class CPerfLock {
  public:
    explicit CPerfLock(CPerfMon* pPerfMon, DWORD dwTimeout = INFINITE) : m_pPerfMon(pPerfMon) {
      m_hrStatus = pPerfMon != NULL ? pPerfMon->LockPerf(dwTimeout) : E_INVALIDARG;
    }
    ~CPerfLock() {
      if (SUCCEEDED(m_hrStatus)) m_pPerfMon->UnlockPerf();
    }
    HRESULT GetStatus() const { return m_hrStatus; }

  private:
    CPerfMon* m_pPerfMon;
    HRESULT m_hrStatus;
    CPerfLock(const CPerfLock&);
    CPerfLock& operator=(const CPerfLock&);
  };

protected:
  enum { kMagic = 0x46524550 };  // "PERF"
  struct _Header {
    DWORD dwMagic;
    DWORD nSlotSize;
    DWORD nSlots;
    DWORD dwReserved;
  };
  struct _Mapping {
    const _ATL_PERF_CATEGORY_INFO* pInfo;
    HANDLE hMap;
    _Header* pHeader;
  };
  static constexpr const WCHAR* kPerflib = L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Perflib";

  HANDLE m_hMutex;
  bool m_bInitialized;
  std::vector<_Mapping> m_maps;
  DWORD m_dwFirstCounter, m_dwFirstHelp;
  bool m_bOpen;

  std::vector<const _ATL_PERF_CATEGORY_INFO*> _Categories() const {
    std::vector<const _ATL_PERF_CATEGORY_INFO*> v;
    for (const _ATL_PERF_GETCATEGORY* p = _GetPerfMap(); p != NULL && *p != NULL; p++) v.push_back((*p)());
    return v;
  }
  static std::wstring _Wide(LPCTSTR psz) {
    if (psz == NULL) return std::wstring();
#ifdef _UNICODE
    return std::wstring(psz);
#else
    int n = ::MultiByteToWideChar(CP_ACP, 0, psz, -1, NULL, 0);
    std::wstring s((size_t)(n > 0 ? n : 1), L'\0');
    ::MultiByteToWideChar(CP_ACP, 0, psz, -1, &s[0], n);
    s.resize(wcslen(s.c_str()));
    return s;
#endif
  }
  static std::wstring _Text(LPCTSTR psz, HINSTANCE hInst) {
    if (psz == NULL) return std::wstring();
    if (IS_INTRESOURCE(psz)) {
      WCHAR sz[1024] = L"";
      ::LoadStringW(hInst, (UINT)(UINT_PTR)psz, sz, 1024);
      return sz;
    }
    return _Wide(psz);
  }
  std::wstring _ObjectName(LPCWSTR szWhat) const {
    std::wstring s = L"Global\\ATLPERF_" + _Wide(GetAppName()) + L"_" + szWhat;
    for (WCHAR& ch : s)
      if (ch == L' ') ch = L'_';
    return s;
  }
  std::wstring _MapName(DWORD dwCategoryId) const {
    WCHAR sz[16];
    swprintf(sz, 16, L"%lu", (unsigned long)dwCategoryId);
    return _ObjectName(sz);
  }
  // Perflib\\009 that is a view of the performance text: its handle is
  // a predefined key (HKEY_PERFORMANCE_TEXT) and values cannot be set.
  static bool _IsTextView(HKEY hKey) { return ((ULONG_PTR)hKey & 0x80000000) != 0 && (LONG_PTR)hKey < 0; }
  static bool _TextIsView() {
    HKEY hKey = NULL;
    std::wstring strKey = std::wstring(kPerflib) + L"\\009";
    if (::RegOpenKeyExW(HKEY_LOCAL_MACHINE, strKey.c_str(), 0, KEY_READ, &hKey) != ERROR_SUCCESS) return false;
    bool b = _IsTextView(hKey);
    ::RegCloseKey(hKey);
    return b;
  }
  // lodctr's input: a .ini naming the service (the application), the
  // symbol file and each name and help, a symbol file of their offsets
  // (0, 2, 4, ... so that they land where Collect expects them).
  HRESULT _LoadWithLodctr(const std::vector<std::pair<std::wstring, std::wstring> >& texts,
                          const std::vector<bool>& isObject, LPCWSTR szLang) {
    typedef DWORD(WINAPI * PFNLOAD)(LPWSTR, BOOL);
    HMODULE hLoadPerf = ::LoadLibraryW(L"loadperf.dll");
    if (hLoadPerf == NULL) return AtlHresultFromLastError();
    PFNLOAD pfn = (PFNLOAD)(void*)::GetProcAddress(hLoadPerf, "LoadPerfCounterTextStringsW");
    if (pfn == NULL) {
      ::FreeLibrary(hLoadPerf);
      return E_NOTIMPL;
    }
    WCHAR szDir[MAX_PATH];
    if (::GetTempPathW(MAX_PATH, szDir) == 0) {
      ::FreeLibrary(hLoadPerf);
      return AtlHresultFromLastError();
    }
    std::wstring strApp = _Wide(GetAppName());
    WCHAR szId[32];
    swprintf(szId, 32, L"%lu", (unsigned long)::GetCurrentProcessId());
    std::wstring strBase = std::wstring(szDir) + L"atlperf_" + szId;
    std::wstring strIni = strBase + L".ini", strSym = strBase + L".h";
    std::wstring sym, ini = L"[info]\r\ndrivername=" + strApp + L"\r\nsymbolfile=" +
                            strSym.substr(strSym.find_last_of(L'\\') + 1) + L"\r\n\r\n[languages]\r\n" +
                            szLang + L"=Language\r\n\r\n[objects]\r\n";
    for (size_t n = 0; n < texts.size(); n++) {
      WCHAR sz[64];
      swprintf(sz, 64, L"ATLPERF_%lu", (unsigned long)n);
      if (isObject[n]) ini += std::wstring(sz) + L"_" + szLang + L"_NAME=" + texts[n].first + L"\r\n";
      WCHAR szDef[96];
      swprintf(szDef, 96, L"#define %ls %lu\r\n", sz, (unsigned long)(2 * n));
      sym += szDef;
    }
    ini += L"\r\n[text]\r\n";
    for (size_t n = 0; n < texts.size(); n++) {
      WCHAR sz[64];
      swprintf(sz, 64, L"ATLPERF_%lu_%ls_", (unsigned long)n, szLang);
      ini += std::wstring(sz) + L"NAME=" + texts[n].first + L"\r\n";
      ini += std::wstring(sz) + L"HELP=" + texts[n].second + L"\r\n";
    }
    HRESULT hr = _WriteUnicodeFile(strIni, ini);
    if (SUCCEEDED(hr)) hr = _WriteUnicodeFile(strSym, sym);
    if (SUCCEEDED(hr)) {
      std::wstring cmd = L"lodctr \"" + strIni + L"\"";
      std::vector<WCHAR> buf(cmd.begin(), cmd.end());
      buf.push_back(0);
      DWORD dw = pfn(&buf[0], TRUE);
      hr = HRESULT_FROM_WIN32(dw);
    }
    ::DeleteFileW(strIni.c_str());
    ::DeleteFileW(strSym.c_str());
    ::FreeLibrary(hLoadPerf);
    return hr;
  }
  static HRESULT _WriteUnicodeFile(const std::wstring& strPath, const std::wstring& strText) {
    HANDLE h = ::CreateFileW(strPath.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return AtlHresultFromLastError();
    DWORD cb;
    WCHAR bom = 0xFEFF;
    BOOL b = ::WriteFile(h, &bom, sizeof(bom), &cb, NULL) &&
             ::WriteFile(h, strText.c_str(), (DWORD)(strText.size() * sizeof(WCHAR)), &cb, NULL);
    ::CloseHandle(h);
    return b ? S_OK : AtlHresultFromLastError();
  }
  std::wstring _PerfKey() const {
    return std::wstring(L"SYSTEM\\CurrentControlSet\\Services\\") + _Wide(GetAppName()) + L"\\Performance";
  }
  // Every account may open the shared memory and the lock: the collector
  // runs in another process, often as another user.
  static LPSECURITY_ATTRIBUTES _Security() {
    static SECURITY_DESCRIPTOR s_sd;
    static SECURITY_ATTRIBUTES s_sa;
    static bool s_bInit = false;
    if (!s_bInit) {
      ::InitializeSecurityDescriptor(&s_sd, SECURITY_DESCRIPTOR_REVISION);
      ::SetSecurityDescriptorDacl(&s_sd, TRUE, NULL, FALSE);
      s_sa.nLength = sizeof(s_sa);
      s_sa.lpSecurityDescriptor = &s_sd;
      s_sa.bInheritHandle = FALSE;
      s_bInit = true;
    }
    return &s_sa;
  }

  static ULONG _Align8(ULONG n) { return (n + 7) & ~7u; }
  static ULONG _SlotSize(const _ATL_PERF_CATEGORY_INFO* p) {
    return _Align8(p->nStructSize) + _Align8((p->nMaxInstanceNameLen + 1) * (ULONG)sizeof(WCHAR));
  }
  static CPerfObject* _Slot(const _Mapping& m, ULONG i) {
    return (CPerfObject*)((BYTE*)(m.pHeader + 1) + i * m.pHeader->nSlotSize);
  }
  void _InitSlot(_Mapping& m, CPerfObject* p, DWORD dwInstance, LPCWSTR szName) {
    memset(p, 0, m.pHeader->nSlotSize);
    p->m_nAllocSize = m.pHeader->nSlotSize;
    p->m_dwCategoryId = m.pInfo->dwCategoryId;
    p->m_dwInstance = dwInstance;
    p->m_nRefCount = 1;
    p->m_nInstanceNameOffset = _Align8(m.pInfo->nStructSize);
    wcscpy(p->GetInstanceName(), szName);
  }
  _Mapping* _FindMap(DWORD dwCategoryId) {
    for (_Mapping& m : m_maps)
      if (m.pInfo->dwCategoryId == dwCategoryId) return &m;
    return NULL;
  }
  // The collector's view of what the application made.
  void _OpenMaps() {
    if (!m_maps.empty()) return;
    for (const _ATL_PERF_CATEGORY_INFO* p : _Categories()) {
      _Mapping m;
      m.pInfo = p;
      m.pHeader = NULL;
      m.hMap = ::OpenFileMappingW(FILE_MAP_READ, FALSE, _MapName(p->dwCategoryId).c_str());
      if (m.hMap != NULL) {
        m.pHeader = (_Header*)::MapViewOfFile(m.hMap, FILE_MAP_READ, 0, 0, 0);
        if (m.pHeader != NULL && m.pHeader->dwMagic != kMagic) {
          ::UnmapViewOfFile(m.pHeader);
          m.pHeader = NULL;
        }
      }
      m_maps.push_back(m);
    }
  }
  void _CloseMaps() {
    for (_Mapping& m : m_maps) {
      if (m.pHeader != NULL) ::UnmapViewOfFile(m.pHeader);
      if (m.hMap != NULL) ::CloseHandle(m.hMap);
    }
    m_maps.clear();
  }

  static DWORD _CounterSize(const _ATL_PERF_COUNTER_INFO& c) {
    switch (c.dwCounterType & PERF_SIZE_VARIABLE_LEN) {
      case PERF_SIZE_DWORD: return 4;
      case PERF_SIZE_LARGE: return 8;
      case PERF_SIZE_ZERO: return 0;
      default: return c.nMaxCounterSize;
    }
  }

  DWORD _WriteCategory(const _ATL_PERF_CATEGORY_INFO* p, BYTE* pOut, DWORD cbRoom, DWORD dwNameIndex,
                       DWORD dwHelpIndex, DWORD nFirstCounterPos, LARGE_INTEGER liTime, LARGE_INTEGER liFreq) {
    // The counter block: its header, then each counter, 8-aligned.
    std::vector<DWORD> offsets;
    DWORD cbBlock = sizeof(PERF_COUNTER_BLOCK);
    for (ULONG i = 0; i < p->nCounters; i++) {
      DWORD n = _CounterSize(p->pCounters[i]);
      if (n >= 8) cbBlock = _Align8(cbBlock);
      offsets.push_back(cbBlock);
      cbBlock += n;
    }
    cbBlock = _Align8(cbBlock);
    const _Mapping* pMap = NULL;
    for (const _Mapping& m : m_maps)
      if (m.pInfo == p && m.pHeader != NULL) pMap = &m;
    std::vector<CPerfObject*> instances;
    if (pMap != NULL)
      for (ULONG i = 0; i < pMap->pHeader->nSlots; i++) {
        CPerfObject* pObj = _Slot(*pMap, i);
        if (pObj->m_nAllocSize != 0) instances.push_back(pObj);
      }
    DWORD cbDef = sizeof(PERF_OBJECT_TYPE) + p->nCounters * sizeof(PERF_COUNTER_DEFINITION);
    DWORD cbTotal = cbDef;
    if (p->bInstanceless) {
      cbTotal += cbBlock;
    } else {
      for (CPerfObject* pObj : instances)
        cbTotal += _Align8(sizeof(PERF_INSTANCE_DEFINITION) +
                           (DWORD)(wcslen(pObj->GetInstanceName()) + 1) * sizeof(WCHAR)) +
                   cbBlock;
    }
    cbTotal = _Align8(cbTotal);
    if (cbTotal > cbRoom) return 0;
    memset(pOut, 0, cbTotal);
    PERF_OBJECT_TYPE* pType = (PERF_OBJECT_TYPE*)pOut;
    pType->TotalByteLength = cbTotal;
    pType->DefinitionLength = cbDef;
    pType->HeaderLength = sizeof(PERF_OBJECT_TYPE);
    pType->ObjectNameTitleIndex = dwNameIndex;
    pType->ObjectHelpTitleIndex = dwHelpIndex;
    pType->DetailLevel = p->dwDetailLevel;
    pType->NumCounters = p->nCounters;
    pType->DefaultCounter = p->nDefaultCounter;
    pType->NumInstances = p->bInstanceless ? PERF_NO_INSTANCES : (LONG)instances.size();
    pType->CodePage = 0;
    pType->PerfTime = liTime;
    pType->PerfFreq = liFreq;
    PERF_COUNTER_DEFINITION* pDef = (PERF_COUNTER_DEFINITION*)(pType + 1);
    for (ULONG i = 0; i < p->nCounters; i++) {
      const _ATL_PERF_COUNTER_INFO& c = p->pCounters[i];
      pDef[i].ByteLength = sizeof(PERF_COUNTER_DEFINITION);
      pDef[i].CounterNameTitleIndex = m_dwFirstCounter + 2 * (nFirstCounterPos + i);
      pDef[i].CounterHelpTitleIndex = m_dwFirstHelp + 2 * (nFirstCounterPos + i);
      pDef[i].DefaultScale = c.nDefaultScale;
      pDef[i].DetailLevel = c.dwDetailLevel;
      pDef[i].CounterType = c.dwCounterType;
      pDef[i].CounterSize = _CounterSize(c);
      pDef[i].CounterOffset = offsets[i];
    }
    BYTE* pNext = pOut + cbDef;
    auto writeBlock = [&](CPerfObject* pObj) {
      PERF_COUNTER_BLOCK* pBlock = (PERF_COUNTER_BLOCK*)pNext;
      pBlock->ByteLength = cbBlock;
      if (pObj != NULL)
        for (ULONG i = 0; i < p->nCounters; i++)
          memcpy(pNext + offsets[i], (BYTE*)pObj + p->pCounters[i].nDataOffset, _CounterSize(p->pCounters[i]));
      pNext += cbBlock;
    };
    if (p->bInstanceless) {
      writeBlock(instances.empty() ? NULL : instances[0]);
    } else {
      for (CPerfObject* pObj : instances) {
        PERF_INSTANCE_DEFINITION* pInst = (PERF_INSTANCE_DEFINITION*)pNext;
        DWORD cbName = (DWORD)(wcslen(pObj->GetInstanceName()) + 1) * sizeof(WCHAR);
        pInst->ByteLength = _Align8(sizeof(PERF_INSTANCE_DEFINITION) + cbName);
        pInst->UniqueID = PERF_NO_UNIQUE_ID;
        pInst->NameOffset = sizeof(PERF_INSTANCE_DEFINITION);
        pInst->NameLength = cbName;
        memcpy(pInst + 1, pObj->GetInstanceName(), cbName);
        pNext += pInst->ByteLength;
        writeBlock(pObj);
      }
    }
    return cbTotal;
  }

  // ----- The registry.

  static void _SetString(HKEY hKey, LPCWSTR szName, LPCWSTR szValue) {
    ::RegSetValueExW(hKey, szName, 0, REG_SZ, (const BYTE*)szValue, (DWORD)((wcslen(szValue) + 1) * sizeof(WCHAR)));
  }
  static DWORD _GetDword(HKEY hKey, LPCWSTR szName, DWORD dwDefault) {
    DWORD dw = 0, cb = sizeof(dw), type = 0;
    if (::RegQueryValueExW(hKey, szName, NULL, &type, (BYTE*)&dw, &cb) != ERROR_SUCCESS || type != REG_DWORD)
      return dwDefault;
    return dw;
  }
  static void _SetDword(HKEY hKey, LPCWSTR szName, DWORD dw) {
    ::RegSetValueExW(hKey, szName, 0, REG_DWORD, (const BYTE*)&dw, sizeof(dw));
  }
  bool _ReadIndexes(DWORD* pFirst, DWORD* pLast, DWORD* pFirstHelp, DWORD* pLastHelp) const {
    HKEY hKey = NULL;
    if (::RegOpenKeyExW(HKEY_LOCAL_MACHINE, _PerfKey().c_str(), 0, KEY_READ, &hKey) != ERROR_SUCCESS) return false;
    *pFirst = _GetDword(hKey, L"First Counter", 0);
    *pLast = _GetDword(hKey, L"Last Counter", 0);
    *pFirstHelp = _GetDword(hKey, L"First Help", 0);
    *pLastHelp = _GetDword(hKey, L"Last Help", 0);
    ::RegCloseKey(hKey);
    return *pFirst != 0 && *pFirstHelp != 0;
  }
  // The indexes this map has, kept from an earlier registration of the
  // same size or given out after Perflib's last.
  HRESULT _AllocateIndexes(DWORD* pFirst, DWORD* pFirstHelp) {
    DWORD n = 0;
    for (const _ATL_PERF_CATEGORY_INFO* p : _Categories()) n += 1 + p->nCounters;
    if (n == 0) return E_UNEXPECTED;
    DWORD dwFirst, dwLast, dwFirstHelp, dwLastHelp;
    if (_ReadIndexes(&dwFirst, &dwLast, &dwFirstHelp, &dwLastHelp) && dwLast - dwFirst == 2 * (n - 1) &&
        dwLastHelp - dwFirstHelp == 2 * (n - 1)) {
      *pFirst = dwFirst;
      *pFirstHelp = dwFirstHelp;
      return S_OK;
    }
    HKEY hPerflib = NULL;
    LONG l = ::RegCreateKeyExW(HKEY_LOCAL_MACHINE, kPerflib, 0, NULL, 0, KEY_ALL_ACCESS, NULL, &hPerflib, NULL);
    if (l != ERROR_SUCCESS) return HRESULT_FROM_WIN32(l);
    // Windows XP's own counters end at 1846 and 1847.
    DWORD dwLastCounter = _GetDword(hPerflib, L"Last Counter", 1846);
    DWORD dwLastHelpIdx = _GetDword(hPerflib, L"Last Help", dwLastCounter + 1);
    dwFirst = dwLastCounter + 2;
    dwFirstHelp = dwLastHelpIdx + 2;
    dwLast = dwFirst + 2 * (n - 1);
    dwLastHelp = dwFirstHelp + 2 * (n - 1);
    _SetDword(hPerflib, L"Last Counter", dwLast);
    _SetDword(hPerflib, L"Last Help", dwLastHelp);
    ::RegCloseKey(hPerflib);
    HKEY hKey = NULL;
    l = ::RegCreateKeyExW(HKEY_LOCAL_MACHINE, _PerfKey().c_str(), 0, NULL, 0, KEY_ALL_ACCESS, NULL, &hKey, NULL);
    if (l != ERROR_SUCCESS) return HRESULT_FROM_WIN32(l);
    _SetDword(hKey, L"First Counter", dwFirst);
    _SetDword(hKey, L"Last Counter", dwLast);
    _SetDword(hKey, L"First Help", dwFirstHelp);
    _SetDword(hKey, L"Last Help", dwLastHelp);
    ::RegCloseKey(hKey);
    *pFirst = dwFirst;
    *pFirstHelp = dwFirstHelp;
    return S_OK;
  }
  // A REG_MULTI_SZ of "index", "text" pairs: those from dwFirst to dwLast
  // replaced by add.
  static LONG _ReplaceStrings(HKEY hKey, LPCWSTR szValue, DWORD dwFirst, DWORD dwLast,
                              const std::vector<std::pair<DWORD, std::wstring> >& add) {
    std::vector<std::pair<DWORD, std::wstring> > all;
    DWORD cb = 0, type = 0;
    if (::RegQueryValueExW(hKey, szValue, NULL, &type, NULL, &cb) == ERROR_SUCCESS && type == REG_MULTI_SZ &&
        cb > 0) {
      std::vector<WCHAR> buf(cb / sizeof(WCHAR) + 2, 0);
      ::RegQueryValueExW(hKey, szValue, NULL, &type, (BYTE*)&buf[0], &cb);
      const WCHAR* p = &buf[0];
      while (*p) {
        DWORD idx = (DWORD)wcstoul(p, NULL, 10);
        p += wcslen(p) + 1;
        std::wstring text = p;
        p += wcslen(p) + (*p ? 1 : 0);
        if (idx >= dwFirst && idx <= dwLast) continue;
        all.push_back(std::make_pair(idx, text));
        if (*p == 0) break;
      }
    }
    for (const auto& e : add) all.push_back(e);
    std::vector<WCHAR> out;
    for (const auto& e : all) {
      WCHAR sz[16];
      swprintf(sz, 16, L"%lu", (unsigned long)e.first);
      out.insert(out.end(), sz, sz + wcslen(sz) + 1);
      out.insert(out.end(), e.second.c_str(), e.second.c_str() + e.second.size() + 1);
    }
    out.push_back(0);
    if (out.size() == 1) out.push_back(0);
    return ::RegSetValueExW(hKey, szValue, 0, REG_MULTI_SZ, (const BYTE*)&out[0], (DWORD)(out.size() * sizeof(WCHAR)));
  }

private:
  CPerfMon(const CPerfMon&);
  CPerfMon& operator=(const CPerfMon&);
};

typedef CPerfMon::CPerfLock CPerfLock;

// ---------------------------------------------------------------------
// The performance monitors of a module, for its exports and registration.

inline std::vector<CPerfMon*>& _AtlPerfMons() {
  static std::vector<CPerfMon*> s_list;
  return s_list;
}
template <class T>
struct _CAtlPerfRegEntry {
  _CAtlPerfRegEntry() { _AtlPerfMons().push_back(&m_perfmon); }
  T m_perfmon;
};
#define PERFREG_ENTRY(className) \
  static ATL::_CAtlPerfRegEntry<className> __atlperf_##className;

inline HRESULT AtlPerfRegister(HINSTANCE hDllInstance = NULL) {
  for (CPerfMon* p : _AtlPerfMons()) {
    HRESULT hr = p->Register(_T("OpenPerfMon"), _T("CollectPerfMon"), _T("ClosePerfMon"), hDllInstance);
    if (FAILED(hr)) return hr;
  }
  return S_OK;
}
inline HRESULT AtlPerfUnregister() {
  for (CPerfMon* p : _AtlPerfMons()) p->Unregister();
  return S_OK;
}
inline DWORD _AtlPerfOpen(LPWSTR lpDeviceNames) {
  DWORD dw = ERROR_SUCCESS;
  for (CPerfMon* p : _AtlPerfMons()) {
    DWORD d = p->Open(lpDeviceNames);
    if (d != ERROR_SUCCESS) dw = d;
  }
  return dw;
}
inline DWORD _AtlPerfCollect(LPWSTR lpValueName, LPVOID* lppData, LPDWORD lpcbTotalBytes, LPDWORD lpNumObjectTypes) {
  if (lppData == NULL || lpcbTotalBytes == NULL || lpNumObjectTypes == NULL) return ERROR_INVALID_PARAMETER;
  BYTE* pStart = (BYTE*)*lppData;
  DWORD cbRoom = *lpcbTotalBytes, cbUsed = 0, nObjects = 0;
  for (CPerfMon* p : _AtlPerfMons()) {
    LPVOID pv = pStart + cbUsed;
    DWORD cb = cbRoom - cbUsed, n = 0;
    DWORD dw = p->Collect(lpValueName, &pv, &cb, &n);
    if (dw != ERROR_SUCCESS) {
      *lpcbTotalBytes = 0;
      *lpNumObjectTypes = 0;
      return dw;
    }
    cbUsed += cb;
    nObjects += n;
  }
  *lppData = pStart + cbUsed;
  *lpcbTotalBytes = cbUsed;
  *lpNumObjectTypes = nObjects;
  return ERROR_SUCCESS;
}
inline DWORD _AtlPerfClose() {
  for (CPerfMon* p : _AtlPerfMons()) p->Close();
  return ERROR_SUCCESS;
}

}  // namespace ATL

#if !defined(_ATL_PERF_NOEXPORT) && (defined(_USRDLL) || defined(_WINDLL) || defined(_ATL_PERF_EXPORTS))
extern "C" __declspec(dllexport) inline DWORD WINAPI OpenPerfMon(LPWSTR lpDeviceNames) {
  return ATL::_AtlPerfOpen(lpDeviceNames);
}
extern "C" __declspec(dllexport) inline DWORD WINAPI CollectPerfMon(LPWSTR lpValueName, LPVOID* lppData,
                                                                   LPDWORD lpcbTotalBytes, LPDWORD lpNumObjectTypes) {
  return ATL::_AtlPerfCollect(lpValueName, lppData, lpcbTotalBytes, lpNumObjectTypes);
}
extern "C" __declspec(dllexport) inline DWORD WINAPI ClosePerfMon() { return ATL::_AtlPerfClose(); }
#endif

#endif  // __ATLPERF_H__
