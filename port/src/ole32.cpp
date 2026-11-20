// ole32.cpp - the COM library: initialization, task memory, GUIDs as
// text, class objects registered by the program, and streams and byte
// arrays over global memory.
//
// CoCreateInstance finds the classes the program itself registered with
// CoRegisterClassObject, in-process servers (InprocServer32), and other
// programs' servers through com_remote.cpp.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include "winport.h"
#include "com.h"
#include "ole.h"

#include <objbase.h>
#include <ole2.h>
#include <fcntl.h>
#ifdef __APPLE__
#include <malloc/malloc.h>
#else
#include <malloc.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <mutex>
#include <vector>

using namespace winport;

extern "C" {

HRESULT WINAPI CoInitialize(LPVOID) { return S_OK; }
HRESULT WINAPI CoInitializeEx(LPVOID, DWORD) { return S_OK; }
void WINAPI CoUninitialize(void) {}
DWORD WINAPI CoBuildVersion(void) { return MAKELONG(0, 23); }
HRESULT WINAPI OleInitialize(LPVOID) { return S_OK; }
void WINAPI OleUninitialize(void) {}
DWORD WINAPI OleBuildVersion(void) { return MAKELONG(0, 23); }

LPVOID WINAPI CoTaskMemAlloc(SIZE_T cb) { return malloc(cb ? cb : 1); }
LPVOID WINAPI CoTaskMemRealloc(LPVOID pv, SIZE_T cb) { return realloc(pv, cb ? cb : 1); }
void WINAPI CoTaskMemFree(LPVOID pv) { free(pv); }

}  // extern "C"

namespace {

// The task allocator as an object; it lives as long as the process.
struct TaskMalloc : IMalloc {
  STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
    if (!ppv) return E_POINTER;
    if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_IMalloc)) {
      *ppv = this;
      return S_OK;
    }
    *ppv = NULL;
    return E_NOINTERFACE;
  }
  STDMETHODIMP_(ULONG) AddRef() override { return 2; }
  STDMETHODIMP_(ULONG) Release() override { return 1; }
  STDMETHODIMP_(void*) Alloc(SIZE_T cb) override { return CoTaskMemAlloc(cb); }
  STDMETHODIMP_(void*) Realloc(void* pv, SIZE_T cb) override {
    if (pv && cb == 0) {
      CoTaskMemFree(pv);
      return NULL;
    }
    return CoTaskMemRealloc(pv, cb);
  }
  STDMETHODIMP_(void) Free(void* pv) override { CoTaskMemFree(pv); }
  STDMETHODIMP_(SIZE_T) GetSize(void* pv) override {
    if (!pv) return (SIZE_T)-1;
#ifdef __APPLE__
    return malloc_size(pv);
#else
    return malloc_usable_size(pv);
#endif
  }
  // Any block could be the C library's: "don't know".
  STDMETHODIMP_(int) DidAlloc(void* pv) override { return pv ? -1 : 0; }
  STDMETHODIMP_(void) HeapMinimize() override {}
};
TaskMalloc g_taskMalloc;

}  // namespace

extern "C" {

HRESULT WINAPI CoGetMalloc(DWORD context, LPMALLOC* out) {
  if (!out) return E_INVALIDARG;
  *out = NULL;
  if (context != 1) return E_INVALIDARG;  // MEMCTX_TASK
  *out = &g_taskMalloc;
  return S_OK;
}

HRESULT WINAPI CoCreateGuid(GUID* g) {
  if (g == NULL) return E_INVALIDARG;
  int fd = open("/dev/urandom", O_RDONLY);
  bool ok = fd >= 0 && read(fd, g, sizeof(*g)) == (ssize_t)sizeof(*g);
  if (fd >= 0) close(fd);
  if (!ok) return E_FAIL;
  // Version 4, variant 1.
  g->Data3 = (WORD)((g->Data3 & 0x0fff) | 0x4000);
  g->Data4[0] = (BYTE)((g->Data4[0] & 0x3f) | 0x80);
  return S_OK;
}

int WINAPI StringFromGUID2(REFGUID g, LPOLESTR out, int cch) {
  char buf[40];
  snprintf(buf, sizeof(buf), "{%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}", (unsigned)g.Data1, g.Data2,
           g.Data3, g.Data4[0], g.Data4[1], g.Data4[2], g.Data4[3], g.Data4[4], g.Data4[5], g.Data4[6], g.Data4[7]);
  if (out == NULL || cch < 39) return 0;
  for (int i = 0; i < 39; i++) out[i] = (OLECHAR)(unsigned char)buf[i];
  return 39;
}

HRESULT WINAPI StringFromCLSID(REFCLSID g, LPOLESTR* out) {
  if (out == NULL) return E_INVALIDARG;
  *out = (LPOLESTR)CoTaskMemAlloc(39 * sizeof(OLECHAR));
  if (*out == NULL) return E_OUTOFMEMORY;
  StringFromGUID2(g, *out, 39);
  return S_OK;
}
HRESULT WINAPI StringFromIID(REFIID g, LPOLESTR* out) { return StringFromCLSID(g, out); }

}  // extern "C"

namespace {

int HexDigit(OLECHAR c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

// "{xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx}"
bool ParseGuid(LPCOLESTR s, GUID* g) {
  static const int groups[] = {8, 4, 4, 4, 12};
  if (s == NULL || s[0] != '{') return false;
  BYTE bytes[16];
  int nb = 0;
  const OLECHAR* p = s + 1;
  for (int k = 0; k < 5; k++) {
    for (int i = 0; i < groups[k]; i += 2) {
      int hi = HexDigit(p[0]), lo = hi < 0 ? -1 : HexDigit(p[1]);
      if (hi < 0 || lo < 0) return false;
      bytes[nb++] = (BYTE)(hi * 16 + lo);
      p += 2;
    }
    if (*p++ != (k < 4 ? '-' : '}')) return false;
  }
  if (*p != 0) return false;
  g->Data1 = ((DWORD)bytes[0] << 24) | ((DWORD)bytes[1] << 16) | ((DWORD)bytes[2] << 8) | bytes[3];
  g->Data2 = (WORD)((bytes[4] << 8) | bytes[5]);
  g->Data3 = (WORD)((bytes[6] << 8) | bytes[7]);
  memcpy(g->Data4, bytes + 8, 8);
  return true;
}

// Class objects registered by the program.
struct ClassEntry {
  CLSID clsid;
  IUnknown* unk;
  DWORD context;
  DWORD flags;
  DWORD cookie;
  bool used;  // REGCLS_SINGLEUSE objects go after one use
};
std::mutex g_classLock;
std::vector<ClassEntry> g_classes;
DWORD g_nextCookie = 1;

}  // namespace

extern "C" {

HRESULT WINAPI CLSIDFromString(LPCOLESTR s, LPCLSID out) {
  if (out == NULL) return E_INVALIDARG;
  if (s == NULL || *s == 0) {
    *out = IID_NULL;
    return S_OK;
  }
  if (ParseGuid(s, out)) return S_OK;
  // A program ID.
  return CLSIDFromProgID(s, out) == S_OK ? S_OK : CO_E_CLASSSTRING;
}
HRESULT WINAPI IIDFromString(LPCOLESTR s, LPIID out) {
  if (out == NULL) return E_INVALIDARG;
  return ParseGuid(s, out) ? S_OK : E_INVALIDARG;
}

// Program IDs come from the registry: HKEY_CLASSES_ROOT\<progid>\CLSID.
HRESULT WINAPI CLSIDFromProgID(LPCOLESTR progid, LPCLSID out) {
  if (progid == NULL || out == NULL) return E_INVALIDARG;
  std::wstring key = std::wstring(progid) + L"\\CLSID";
  WCHAR value[64];
  LONG cb = sizeof(value);
  if (RegQueryValueW(HKEY_CLASSES_ROOT, key.c_str(), value, &cb) != ERROR_SUCCESS || !ParseGuid(value, out))
    return CO_E_CLASSSTRING;
  return S_OK;
}

HRESULT WINAPI ProgIDFromCLSID(REFCLSID clsid, LPOLESTR* out) {
  if (out == NULL) return E_INVALIDARG;
  WCHAR guid[40], value[256];
  StringFromGUID2(clsid, guid, 40);
  std::wstring key = std::wstring(L"CLSID\\") + guid + L"\\ProgID";
  LONG cb = sizeof(value);
  if (RegQueryValueW(HKEY_CLASSES_ROOT, key.c_str(), value, &cb) != ERROR_SUCCESS) return REGDB_E_CLASSNOTREG;
  size_t n = wcslen(value) + 1;
  *out = (LPOLESTR)CoTaskMemAlloc(n * sizeof(OLECHAR));
  if (*out == NULL) return E_OUTOFMEMORY;
  wmemcpy(*out, value, n);
  return S_OK;
}

HRESULT WINAPI CoRegisterClassObject(REFCLSID clsid, LPUNKNOWN unk, DWORD context, DWORD flags, LPDWORD cookie) {
  if (unk == NULL || cookie == NULL) return E_INVALIDARG;
  unk->AddRef();
  std::lock_guard<std::mutex> g(g_classLock);
  ClassEntry e = {clsid, unk, context, flags, g_nextCookie++, false};
  g_classes.push_back(e);
  *cookie = e.cookie;
  if (context & CLSCTX_LOCAL_SERVER) winport::com::PublishClass(clsid);
  return S_OK;
}

HRESULT WINAPI CoRevokeClassObject(DWORD cookie) {
  IUnknown* unk = NULL;
  CLSID clsid = CLSID_NULL;
  bool others = false, local = false;
  {
    std::lock_guard<std::mutex> g(g_classLock);
    for (size_t i = 0; i < g_classes.size(); i++) {
      if (g_classes[i].cookie == cookie) {
        unk = g_classes[i].unk;
        clsid = g_classes[i].clsid;
        local = (g_classes[i].context & CLSCTX_LOCAL_SERVER) != 0;
        g_classes.erase(g_classes.begin() + i);
        break;
      }
    }
    for (const ClassEntry& e : g_classes)
      if (IsEqualGUID(e.clsid, clsid) && (e.context & CLSCTX_LOCAL_SERVER) && !e.used) others = true;
  }
  if (unk == NULL) return E_INVALIDARG;
  if (local && !others) winport::com::UnpublishClass(clsid);
  unk->Release();
  return S_OK;
}

HRESULT WINAPI CoResumeClassObjects(void) { return S_OK; }
HRESULT WINAPI CoSuspendClassObjects(void) { return S_OK; }
ULONG WINAPI CoAddRefServerProcess(void) { return 1; }
ULONG WINAPI CoReleaseServerProcess(void) { return 0; }
HRESULT WINAPI CoDisconnectObject(LPUNKNOWN unk, DWORD) {
  winport::com::DisconnectObject(unk);
  return S_OK;
}
HRESULT WINAPI CoLockObjectExternal(LPUNKNOWN unk, BOOL lock, BOOL) {
  if (unk == NULL) return E_INVALIDARG;
  if (lock) unk->AddRef();
  else unk->Release();
  return S_OK;
}
BOOL WINAPI CoIsHandlerConnected(LPUNKNOWN) { return TRUE; }

// The in-process servers COM has loaded (InprocServer32), until they say
// they can go (DllCanUnloadNow).
static std::mutex g_inprocLock;
static std::vector<HMODULE>& InprocServers() {
  static auto* v = new std::vector<HMODULE>;
  return *v;
}

typedef HRESULT(STDAPICALLTYPE* DllGetClassObjectProc)(REFCLSID, REFIID, LPVOID*);
typedef HRESULT(STDAPICALLTYPE* DllCanUnloadNowProc)(void);

static HRESULT GetInprocClassObject(REFCLSID clsid, REFIID riid, LPVOID* out) {
  WCHAR guid[40], path[1024];
  StringFromGUID2(clsid, guid, 40);
  std::wstring key = std::wstring(L"CLSID\\") + guid + L"\\InprocServer32";
  LONG cb = sizeof(path);
  if (RegQueryValueW(HKEY_CLASSES_ROOT, key.c_str(), path, &cb) != ERROR_SUCCESS || !path[0])
    return REGDB_E_CLASSNOTREG;
  HMODULE h = LoadLibraryW(path);
  if (h == NULL) return CO_E_DLLNOTFOUND;
  DllGetClassObjectProc get = (DllGetClassObjectProc)GetProcAddress(h, "DllGetClassObject");
  if (get == NULL) {
    FreeLibrary(h);
    return CO_E_ERRORINDLL;
  }
  HRESULT hr = get(clsid, riid, out);
  if (FAILED(hr)) {
    FreeLibrary(h);
    return hr;
  }
  std::lock_guard<std::mutex> g(g_inprocLock);
  for (HMODULE m : InprocServers())
    if (m == h) {
      FreeLibrary(h);  // loaded already: one load is enough
      return hr;
    }
  InprocServers().push_back(h);
  return hr;
}

void WINAPI CoFreeUnusedLibraries(void) {
  std::vector<HMODULE> unload;
  {
    std::lock_guard<std::mutex> g(g_inprocLock);
    auto& v = InprocServers();
    for (size_t i = 0; i < v.size();) {
      DllCanUnloadNowProc can = (DllCanUnloadNowProc)GetProcAddress(v[i], "DllCanUnloadNow");
      if (can != NULL && can() == S_OK) {
        unload.push_back(v[i]);
        v.erase(v.begin() + i);
      } else {
        i++;
      }
    }
  }
  for (HMODULE h : unload) FreeLibrary(h);
}
void WINAPI CoFreeUnusedLibrariesEx(DWORD, DWORD) { CoFreeUnusedLibraries(); }

// Emulation: HKEY_CLASSES_ROOT\CLSID\<old>\TreatAs names the class that
// stands in for an old one.
HRESULT WINAPI CoGetTreatAsClass(REFCLSID old, LPCLSID out) {
  if (out == NULL) return E_INVALIDARG;
  *out = old;
  WCHAR guid[40], value[64];
  StringFromGUID2(old, guid, 40);
  std::wstring key = std::wstring(L"CLSID\\") + guid + L"\\TreatAs";
  LONG cb = sizeof(value);
  CLSID as;
  if (RegQueryValueW(HKEY_CLASSES_ROOT, key.c_str(), value, &cb) != ERROR_SUCCESS || !ParseGuid(value, &as))
    return S_FALSE;
  *out = as;
  return S_OK;
}
HRESULT WINAPI CoTreatAsClass(REFCLSID old, REFCLSID as) {
  WCHAR guid[40], value[40];
  StringFromGUID2(old, guid, 40);
  std::wstring key = std::wstring(L"CLSID\\") + guid;
  HKEY k;
  if (RegCreateKeyW(HKEY_CLASSES_ROOT, key.c_str(), &k) != ERROR_SUCCESS) return REGDB_E_WRITEREGDB;
  LONG r;
  if (IsEqualCLSID(as, CLSID_NULL) || IsEqualCLSID(as, old)) {
    r = RegDeleteKeyW(k, L"TreatAs");
    if (r == ERROR_FILE_NOT_FOUND) r = ERROR_SUCCESS;
  } else {
    StringFromGUID2(as, value, 40);
    r = RegSetValueW(k, L"TreatAs", REG_SZ, value, (DWORD)(wcslen(value) * sizeof(WCHAR)));
  }
  RegCloseKey(k);
  return r == ERROR_SUCCESS ? S_OK : REGDB_E_WRITEREGDB;
}

}  // extern "C"

HRESULT winport::com::TakeClassObject(REFCLSID clsid, IUnknown** out, bool* last) {
  *out = NULL;
  *last = true;
  std::lock_guard<std::mutex> g(g_classLock);
  HRESULT hr = (HRESULT)0x80080008L;  // CO_E_SERVER_STOPPING
  for (ClassEntry& e : g_classes) {
    if (!IsEqualGUID(e.clsid, clsid) || !(e.context & CLSCTX_LOCAL_SERVER) || e.used) continue;
    if (*out == NULL) {
      *out = e.unk;
      e.unk->AddRef();
      if (e.flags == REGCLS_SINGLEUSE) e.used = true;
      hr = S_OK;
      if (!e.used) *last = false;
    } else {
      *last = false;
    }
  }
  return hr;
}

extern "C" {

HRESULT WINAPI CoGetClassObject(REFCLSID requested, DWORD context, LPVOID, REFIID riid, LPVOID* out) {
  if (out == NULL) return E_INVALIDARG;
  *out = NULL;
  CLSID clsid;
  CoGetTreatAsClass(requested, &clsid);  // the class emulating it, if one does
  IUnknown* unk = NULL;
  {
    std::lock_guard<std::mutex> g(g_classLock);
    for (size_t i = 0; i < g_classes.size(); i++) {
      ClassEntry& e = g_classes[i];
      // A class this program registered serves it in any context: a
      // local server in this process is as near as an in-process one
      // (Wine's CoGetClassObject hands it out the same way).
      if (IsEqualGUID(e.clsid, clsid) && (context & (e.context | CLSCTX_INPROC)) != 0 && !e.used) {
        unk = e.unk;
        unk->AddRef();
        if (e.flags == REGCLS_SINGLEUSE) e.used = true;
        break;
      }
    }
  }
  if (unk == NULL) {
    HRESULT hr = REGDB_E_CLASSNOTREG;
    if (context & CLSCTX_INPROC_SERVER) hr = GetInprocClassObject(clsid, riid, out);
    if (hr == REGDB_E_CLASSNOTREG && (context & CLSCTX_INPROC_SERVER))
      hr = winport::ole::GetBuiltinClassObject(clsid, riid, out);
    if (FAILED(hr) && (context & CLSCTX_LOCAL_SERVER)) hr = winport::com::GetRemoteClassObject(clsid, riid, out);
    return hr;
  }
  HRESULT hr = unk->QueryInterface(riid, out);
  unk->Release();
  return hr;
}

HRESULT WINAPI CoCreateInstance(REFCLSID clsid, LPUNKNOWN outer, DWORD context, REFIID riid, LPVOID* out) {
  if (out == NULL) return E_INVALIDARG;
  *out = NULL;
  IClassFactory* cf = NULL;
  HRESULT hr = CoGetClassObject(clsid, context, NULL, IID_IClassFactory, (void**)&cf);
  if (FAILED(hr)) return hr;
  hr = cf->CreateInstance(outer, riid, out);
  cf->Release();
  return hr;
}

HRESULT WINAPI CoCreateInstanceEx(REFCLSID clsid, IUnknown* outer, DWORD context, COSERVERINFO*, DWORD count,
                                  MULTI_QI* results) {
  if (count == 0 || results == NULL) return E_INVALIDARG;
  IUnknown* unk = NULL;
  HRESULT hr = CoCreateInstance(clsid, outer, context, IID_IUnknown, (void**)&unk);
  if (FAILED(hr)) {
    for (DWORD i = 0; i < count; i++) results[i].pItf = NULL, results[i].hr = hr;
    return hr;
  }
  DWORD got = 0;
  for (DWORD i = 0; i < count; i++) {
    results[i].hr = unk->QueryInterface(*results[i].pIID, (void**)&results[i].pItf);
    if (SUCCEEDED(results[i].hr)) got++;
  }
  unk->Release();
  return got == count ? S_OK : got ? CO_S_NOTALLINTERFACES : E_NOINTERFACE;
}

void WINAPI ReleaseStgMedium(LPSTGMEDIUM m) {
  if (m == NULL) return;
  switch (m->tymed) {
    case TYMED_HGLOBAL:
      if (m->pUnkForRelease == NULL && m->hGlobal != NULL) GlobalFree(m->hGlobal);
      break;
    case TYMED_FILE:
      if (m->lpszFileName != NULL) {
        if (m->pUnkForRelease == NULL) DeleteFileW(m->lpszFileName);
        CoTaskMemFree(m->lpszFileName);
      }
      break;
    case TYMED_ISTREAM:
      if (m->pstm != NULL) m->pstm->Release();
      break;
    case TYMED_ISTORAGE:
      if (m->pstg != NULL) m->pstg->Release();
      break;
    case TYMED_GDI:
      if (m->pUnkForRelease == NULL && m->hBitmap != NULL) DeleteObject(m->hBitmap);
      break;
    case TYMED_MFPICT:
      if (m->pUnkForRelease == NULL && m->hMetaFilePict != NULL) {
        METAFILEPICT* mfp = (METAFILEPICT*)GlobalLock(m->hMetaFilePict);
        if (mfp != NULL) {
          DeleteMetaFile(mfp->hMF);
          GlobalUnlock(m->hMetaFilePict);
        }
        GlobalFree(m->hMetaFilePict);
      }
      break;
    case TYMED_ENHMF:
      if (m->pUnkForRelease == NULL && m->hEnhMetaFile != NULL) DeleteEnhMetaFile(m->hEnhMetaFile);
      break;
    default:
      break;
  }
  if (m->pUnkForRelease != NULL) m->pUnkForRelease->Release();
  m->tymed = TYMED_NULL;
  m->pUnkForRelease = NULL;
}

}  // extern "C"

// Global memory shared by streams and byte arrays: the HGLOBAL, freed
// with the last of them when asked to.
namespace {

struct SharedGlobal {
  std::atomic<int> refs{1};
  HGLOBAL h;
  bool freeOnRelease;
  ULONGLONG size;  // the stream's logical size, at most the block's
  void Release() {
    if (--refs == 0) {
      if (freeOnRelease) GlobalFree(h);
      delete this;
    }
  }
  bool Grow(ULONGLONG need) {
    if (need > 0xffffffffULL) return false;
    SIZE_T have = GlobalSize(h);
    if (need <= have) return true;
    SIZE_T want = have * 2 > need ? have * 2 : (SIZE_T)need;
    HGLOBAL n = GlobalReAlloc(h, want, GMEM_MOVEABLE);
    if (n == NULL) return false;
    h = n;
    return true;
  }
};

class HGlobalStream final : public IStream {
 public:
  HGlobalStream(SharedGlobal* g) : g_(g) {}
  ~HGlobalStream() { g_->Release(); }
  STDMETHOD(QueryInterface)(REFIID riid, void** out) override {
    if (out == NULL) return E_POINTER;
    if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_ISequentialStream) || IsEqualIID(riid, IID_IStream)) {
      *out = static_cast<IStream*>(this);
      AddRef();
      return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
  }
  STDMETHOD_(ULONG, AddRef)() override { return ++refs_; }
  STDMETHOD_(ULONG, Release)() override {
    ULONG n = --refs_;
    if (n == 0) delete this;
    return n;
  }
  STDMETHOD(Read)(void* pv, ULONG cb, ULONG* read) override {
    if (pv == NULL) return STG_E_INVALIDPOINTER;
    ULONG n = pos_ >= g_->size ? 0 : (ULONG)(g_->size - pos_ < cb ? g_->size - pos_ : cb);
    if (n) {
      BYTE* p = (BYTE*)GlobalLock(g_->h);
      memcpy(pv, p + pos_, n);
      GlobalUnlock(g_->h);
    }
    pos_ += n;
    if (read) *read = n;
    return S_OK;
  }
  STDMETHOD(Write)(const void* pv, ULONG cb, ULONG* written) override {
    if (pv == NULL) return STG_E_INVALIDPOINTER;
    if (written) *written = 0;
    if (cb == 0) return S_OK;
    if (!g_->Grow(pos_ + cb)) return STG_E_MEDIUMFULL;
    BYTE* p = (BYTE*)GlobalLock(g_->h);
    memcpy(p + pos_, pv, cb);
    GlobalUnlock(g_->h);
    pos_ += cb;
    if (pos_ > g_->size) g_->size = pos_;
    if (written) *written = cb;
    return S_OK;
  }
  STDMETHOD(Seek)(LARGE_INTEGER move, DWORD origin, ULARGE_INTEGER* newPos) override {
    LONGLONG base = origin == STREAM_SEEK_SET ? 0 : origin == STREAM_SEEK_CUR ? (LONGLONG)pos_
                  : origin == STREAM_SEEK_END ? (LONGLONG)g_->size : -1;
    if (base < 0 && origin > STREAM_SEEK_END) return STG_E_INVALIDFUNCTION;
    LONGLONG p = base + move.QuadPart;
    if (p < 0) return STG_E_INVALIDFUNCTION;
    pos_ = (ULONGLONG)p;
    if (newPos) newPos->QuadPart = pos_;
    return S_OK;
  }
  STDMETHOD(SetSize)(ULARGE_INTEGER size) override {
    if (!g_->Grow(size.QuadPart)) return STG_E_MEDIUMFULL;
    g_->size = size.QuadPart;
    return S_OK;
  }
  STDMETHOD(CopyTo)(IStream* dst, ULARGE_INTEGER cb, ULARGE_INTEGER* read, ULARGE_INTEGER* written) override {
    if (dst == NULL) return STG_E_INVALIDPOINTER;
    ULONGLONG n = pos_ >= g_->size ? 0 : g_->size - pos_;
    if (cb.QuadPart < n) n = cb.QuadPart;
    ULONG done = 0;
    HRESULT hr = S_OK;
    if (n) {
      BYTE* p = (BYTE*)GlobalLock(g_->h);
      hr = dst->Write(p + pos_, (ULONG)n, &done);
      GlobalUnlock(g_->h);
    }
    pos_ += n;
    if (read) read->QuadPart = n;
    if (written) written->QuadPart = done;
    return hr;
  }
  STDMETHOD(Commit)(DWORD) override { return S_OK; }
  STDMETHOD(Revert)() override { return S_OK; }
  STDMETHOD(LockRegion)(ULARGE_INTEGER, ULARGE_INTEGER, DWORD) override { return STG_E_INVALIDFUNCTION; }
  STDMETHOD(UnlockRegion)(ULARGE_INTEGER, ULARGE_INTEGER, DWORD) override { return STG_E_INVALIDFUNCTION; }
  STDMETHOD(Stat)(STATSTG* st, DWORD) override {
    if (st == NULL) return STG_E_INVALIDPOINTER;
    memset(st, 0, sizeof(*st));
    st->type = STGTY_STREAM;
    st->cbSize.QuadPart = g_->size;
    return S_OK;
  }
  STDMETHOD(Clone)(IStream** out) override {
    if (out == NULL) return STG_E_INVALIDPOINTER;
    g_->refs++;
    HGlobalStream* s = new HGlobalStream(g_);
    s->pos_ = pos_;
    *out = s;
    return S_OK;
  }
  HGLOBAL Global() const { return g_->h; }

 private:
  std::atomic<ULONG> refs_{1};
  SharedGlobal* g_;
  ULONGLONG pos_ = 0;
};

class HGlobalLockBytes final : public ILockBytes {
 public:
  HGlobalLockBytes(SharedGlobal* g) : g_(g) {}
  ~HGlobalLockBytes() { g_->Release(); }
  STDMETHOD(QueryInterface)(REFIID riid, void** out) override {
    if (out == NULL) return E_POINTER;
    if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_ILockBytes)) {
      *out = static_cast<ILockBytes*>(this);
      AddRef();
      return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
  }
  STDMETHOD_(ULONG, AddRef)() override { return ++refs_; }
  STDMETHOD_(ULONG, Release)() override {
    ULONG n = --refs_;
    if (n == 0) delete this;
    return n;
  }
  STDMETHOD(ReadAt)(ULARGE_INTEGER off, void* pv, ULONG cb, ULONG* read) override {
    if (pv == NULL) return STG_E_INVALIDPOINTER;
    ULONGLONG o = off.QuadPart;
    ULONG n = o >= g_->size ? 0 : (ULONG)(g_->size - o < cb ? g_->size - o : cb);
    if (n) {
      BYTE* p = (BYTE*)GlobalLock(g_->h);
      memcpy(pv, p + o, n);
      GlobalUnlock(g_->h);
    }
    if (read) *read = n;
    return S_OK;
  }
  STDMETHOD(WriteAt)(ULARGE_INTEGER off, const void* pv, ULONG cb, ULONG* written) override {
    if (pv == NULL) return STG_E_INVALIDPOINTER;
    if (written) *written = 0;
    if (cb == 0) return S_OK;
    ULONGLONG o = off.QuadPart;
    if (!g_->Grow(o + cb)) return STG_E_MEDIUMFULL;
    BYTE* p = (BYTE*)GlobalLock(g_->h);
    if (o > g_->size) memset(p + g_->size, 0, (size_t)(o - g_->size));
    memcpy(p + o, pv, cb);
    GlobalUnlock(g_->h);
    if (o + cb > g_->size) g_->size = o + cb;
    if (written) *written = cb;
    return S_OK;
  }
  STDMETHOD(Flush)() override { return S_OK; }
  STDMETHOD(SetSize)(ULARGE_INTEGER size) override {
    if (!g_->Grow(size.QuadPart)) return STG_E_MEDIUMFULL;
    g_->size = size.QuadPart;
    return S_OK;
  }
  STDMETHOD(LockRegion)(ULARGE_INTEGER, ULARGE_INTEGER, DWORD) override { return STG_E_INVALIDFUNCTION; }
  STDMETHOD(UnlockRegion)(ULARGE_INTEGER, ULARGE_INTEGER, DWORD) override { return STG_E_INVALIDFUNCTION; }
  STDMETHOD(Stat)(STATSTG* st, DWORD) override {
    if (st == NULL) return STG_E_INVALIDPOINTER;
    memset(st, 0, sizeof(*st));
    st->type = STGTY_LOCKBYTES;
    st->cbSize.QuadPart = g_->size;
    return S_OK;
  }
  HGLOBAL Global() const { return g_->h; }

 private:
  std::atomic<ULONG> refs_{1};
  SharedGlobal* g_;
};

SharedGlobal* NewShared(HGLOBAL h, BOOL freeOnRelease) {
  SharedGlobal* g = new SharedGlobal;
  if (h == NULL) {
    h = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, 0);
    if (h == NULL) {
      delete g;
      return NULL;
    }
    g->size = 0;
  } else {
    g->size = GlobalSize(h);
  }
  g->h = h;
  g->freeOnRelease = freeOnRelease != FALSE;
  return g;
}

}  // namespace

extern "C" {

HRESULT WINAPI CreateStreamOnHGlobal(HGLOBAL h, BOOL deleteOnRelease, LPSTREAM* out) {
  if (out == NULL) return E_INVALIDARG;
  SharedGlobal* g = NewShared(h, deleteOnRelease);
  if (g == NULL) return E_OUTOFMEMORY;
  *out = new HGlobalStream(g);
  return S_OK;
}

HRESULT WINAPI GetHGlobalFromStream(LPSTREAM stm, HGLOBAL* out) {
  if (stm == NULL || out == NULL) return E_INVALIDARG;
  // Only the streams made here have one.
  HGlobalStream* s = dynamic_cast<HGlobalStream*>(stm);
  if (s == NULL) return E_INVALIDARG;
  *out = s->Global();
  return S_OK;
}

HRESULT WINAPI CreateILockBytesOnHGlobal(HGLOBAL h, BOOL deleteOnRelease, ILockBytes** out) {
  if (out == NULL) return E_INVALIDARG;
  SharedGlobal* g = NewShared(h, deleteOnRelease);
  if (g == NULL) return E_OUTOFMEMORY;
  *out = new HGlobalLockBytes(g);
  return S_OK;
}

HRESULT WINAPI GetHGlobalFromILockBytes(ILockBytes* lb, HGLOBAL* out) {
  if (lb == NULL || out == NULL) return E_INVALIDARG;
  HGlobalLockBytes* l = dynamic_cast<HGlobalLockBytes*>(lb);
  if (l == NULL) return E_INVALIDARG;
  *out = l->Global();
  return S_OK;
}

// Class IDs in streams: the GUID's bytes as they are in memory on x86,
// which is the stored format.
HRESULT WINAPI WriteClassStm(LPSTREAM stm, REFCLSID clsid) {
  if (stm == NULL) return E_INVALIDARG;
  BYTE b[16];
  b[0] = (BYTE)clsid.Data1, b[1] = (BYTE)(clsid.Data1 >> 8), b[2] = (BYTE)(clsid.Data1 >> 16);
  b[3] = (BYTE)(clsid.Data1 >> 24), b[4] = (BYTE)clsid.Data2, b[5] = (BYTE)(clsid.Data2 >> 8);
  b[6] = (BYTE)clsid.Data3, b[7] = (BYTE)(clsid.Data3 >> 8);
  memcpy(b + 8, clsid.Data4, 8);
  return stm->Write(b, 16, NULL);
}

HRESULT WINAPI ReadClassStm(LPSTREAM stm, CLSID* clsid) {
  if (stm == NULL || clsid == NULL) return E_INVALIDARG;
  BYTE b[16];
  ULONG n = 0;
  HRESULT hr = stm->Read(b, 16, &n);
  if (FAILED(hr)) return hr;
  if (n != 16) return STG_E_READFAULT;
  clsid->Data1 = b[0] | (b[1] << 8) | (b[2] << 16) | ((DWORD)b[3] << 24);
  clsid->Data2 = (WORD)(b[4] | (b[5] << 8));
  clsid->Data3 = (WORD)(b[6] | (b[7] << 8));
  memcpy(clsid->Data4, b + 8, 8);
  return S_OK;
}

}  // extern "C"
