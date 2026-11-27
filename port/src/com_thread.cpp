// com_thread.cpp - interfaces handed from one thread of the process to
// another: CoMarshalInterThreadInterfaceInStream with
// CoGetInterfaceAndReleaseStream, the global interface table
// (CLSID_StdGlobalInterfaceTable), and the free-threaded marshaler
// (CoCreateFreeThreadedMarshaler) that objects aggregate to say that any
// thread may call them.
//
// The layer has no apartments: every thread calls an object directly,
// as with free-threaded objects on Windows. So a stream carries the
// interface pointer itself (and the reference it holds), and the table
// keeps the pointers it is given.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include "winport.h"
#include "ole.h"
#include <map>
#include <new>
#include <mutex>

namespace {

// What a stream holds: a mark, then the pointer.
const DWORD kThreadMark = 0x54485244;  // 'THRD'

struct ThreadPacket {
  DWORD mark;
  IUnknown* punk;
};

class GlobalInterfaceTable : public IGlobalInterfaceTable {
public:
  STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
    if (ppv == NULL) return E_POINTER;
    if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_IGlobalInterfaceTable)) {
      *ppv = static_cast<IGlobalInterfaceTable*>(this);
      return S_OK;
    }
    *ppv = NULL;
    return E_NOINTERFACE;
  }
  // One table for the process, never freed.
  STDMETHODIMP_(ULONG) AddRef() override { return 2; }
  STDMETHODIMP_(ULONG) Release() override { return 1; }

  STDMETHODIMP RegisterInterfaceInGlobal(IUnknown* pUnk, REFIID riid, DWORD* pdwCookie) override {
    if (pdwCookie == NULL) return E_INVALIDARG;
    *pdwCookie = 0;
    if (pUnk == NULL) return E_INVALIDARG;
    // The interface it is registered as is the one kept.
    IUnknown* p = NULL;
    HRESULT hr = pUnk->QueryInterface(riid, (void**)&p);
    if (FAILED(hr)) return hr;
    std::lock_guard<std::mutex> g(m_lock);
    DWORD cookie = ++m_nNext;
    if (cookie == 0) cookie = ++m_nNext;
    m_entries[cookie] = Entry{p, riid};
    *pdwCookie = cookie;
    return S_OK;
  }
  STDMETHODIMP RevokeInterfaceFromGlobal(DWORD dwCookie) override {
    IUnknown* p = NULL;
    {
      std::lock_guard<std::mutex> g(m_lock);
      std::map<DWORD, Entry>::iterator it = m_entries.find(dwCookie);
      if (it == m_entries.end()) return E_INVALIDARG;
      p = it->second.punk;
      m_entries.erase(it);
    }
    p->Release();
    return S_OK;
  }
  STDMETHODIMP GetInterfaceFromGlobal(DWORD dwCookie, REFIID riid, void** ppv) override {
    if (ppv == NULL) return E_INVALIDARG;
    *ppv = NULL;
    IUnknown* p = NULL;
    {
      std::lock_guard<std::mutex> g(m_lock);
      std::map<DWORD, Entry>::iterator it = m_entries.find(dwCookie);
      if (it == m_entries.end()) return E_INVALIDARG;
      p = it->second.punk;
      p->AddRef();
    }
    HRESULT hr = p->QueryInterface(riid, ppv);
    p->Release();
    return hr;
  }

private:
  struct Entry {
    IUnknown* punk;
    IID iid;
  };
  std::mutex m_lock;
  std::map<DWORD, Entry> m_entries;
  DWORD m_nNext = 0;
};

class GlobalInterfaceTableFactory : public IClassFactory {
public:
  STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
    if (ppv == NULL) return E_POINTER;
    if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_IClassFactory)) {
      *ppv = static_cast<IClassFactory*>(this);
      return S_OK;
    }
    *ppv = NULL;
    return E_NOINTERFACE;
  }
  STDMETHODIMP_(ULONG) AddRef() override { return 2; }
  STDMETHODIMP_(ULONG) Release() override { return 1; }
  STDMETHODIMP CreateInstance(IUnknown* pOuter, REFIID riid, void** ppv) override {
    if (ppv == NULL) return E_POINTER;
    *ppv = NULL;
    if (pOuter != NULL) return CLASS_E_NOAGGREGATION;
    static GlobalInterfaceTable table;
    return table.QueryInterface(riid, ppv);
  }
  STDMETHODIMP LockServer(BOOL) override { return S_OK; }
};

// The free-threaded marshaler: an inner object whose IMarshal puts the
// pointer itself in the stream. Its own IUnknown is the one handed back;
// its IMarshal goes to the outer object's.
class FreeThreadedMarshaler : public IMarshal {
public:
  explicit FreeThreadedMarshaler(IUnknown* pOuter) : m_inner(this), m_pOuter(pOuter) {
    if (m_pOuter == NULL) m_pOuter = &m_inner;
  }
  IUnknown* Inner() { return &m_inner; }

  STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
    return m_pOuter->QueryInterface(riid, ppv);
  }
  STDMETHODIMP_(ULONG) AddRef() override { return m_pOuter->AddRef(); }
  STDMETHODIMP_(ULONG) Release() override { return m_pOuter->Release(); }

  STDMETHODIMP GetUnmarshalClass(REFIID, void*, DWORD, void*, DWORD, CLSID* pCid) override {
    if (pCid == NULL) return E_POINTER;
    *pCid = kInProcFreeMarshaler;
    return S_OK;
  }
  STDMETHODIMP GetMarshalSizeMax(REFIID, void*, DWORD, void*, DWORD, DWORD* pSize) override {
    if (pSize == NULL) return E_POINTER;
    *pSize = sizeof(ThreadPacket);
    return S_OK;
  }
  STDMETHODIMP MarshalInterface(IStream* pStm, REFIID riid, void* pv, DWORD, void*, DWORD) override {
    if (pStm == NULL || pv == NULL) return E_INVALIDARG;
    ThreadPacket packet = {kThreadMark, NULL};
    HRESULT hr = ((IUnknown*)pv)->QueryInterface(riid, (void**)&packet.punk);
    if (FAILED(hr)) return hr;
    hr = pStm->Write(&packet, sizeof(packet), NULL);
    if (FAILED(hr)) packet.punk->Release();
    return hr;
  }
  STDMETHODIMP UnmarshalInterface(IStream* pStm, REFIID riid, void** ppv) override {
    if (ppv == NULL) return E_POINTER;
    *ppv = NULL;
    ThreadPacket packet = {0, NULL};
    ULONG cb = 0;
    HRESULT hr = pStm != NULL ? pStm->Read(&packet, sizeof(packet), &cb) : E_INVALIDARG;
    if (FAILED(hr)) return hr;
    if (cb != sizeof(packet) || packet.mark != kThreadMark || packet.punk == NULL)
      return CO_E_OBJNOTCONNECTED;
    hr = packet.punk->QueryInterface(riid, ppv);
    packet.punk->Release();
    return hr;
  }
  STDMETHODIMP ReleaseMarshalData(IStream* pStm) override {
    ThreadPacket packet = {0, NULL};
    ULONG cb = 0;
    HRESULT hr = pStm != NULL ? pStm->Read(&packet, sizeof(packet), &cb) : E_INVALIDARG;
    if (FAILED(hr)) return hr;
    if (cb == sizeof(packet) && packet.mark == kThreadMark && packet.punk != NULL)
      packet.punk->Release();
    return S_OK;
  }
  STDMETHODIMP DisconnectObject(DWORD) override { return S_OK; }

private:
  // {0000033A-0000-0000-C000-000000000046}
  static constexpr CLSID kInProcFreeMarshaler = {
      0x0000033a, 0x0000, 0x0000, {0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};

  class InnerUnknown : public IUnknown {
  public:
    explicit InnerUnknown(FreeThreadedMarshaler* p) : m_p(p), m_ref(1) {}
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
      if (ppv == NULL) return E_POINTER;
      if (IsEqualIID(riid, IID_IUnknown)) {
        *ppv = static_cast<IUnknown*>(this);
      } else if (IsEqualIID(riid, IID_IMarshal)) {
        *ppv = static_cast<IMarshal*>(m_p);
      } else {
        *ppv = NULL;
        return E_NOINTERFACE;
      }
      ((IUnknown*)*ppv)->AddRef();
      return S_OK;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return (ULONG)InterlockedIncrement(&m_ref); }
    STDMETHODIMP_(ULONG) Release() override {
      LONG n = InterlockedDecrement(&m_ref);
      if (n == 0) delete m_p;
      return (ULONG)n;
    }

  private:
    FreeThreadedMarshaler* m_p;
    LONG m_ref;
  };

  InnerUnknown m_inner;
  IUnknown* m_pOuter;
};

}  // namespace

namespace winport {
namespace ole {

HRESULT GetGlobalInterfaceTableClassObject(REFIID riid, void** out) {
  static GlobalInterfaceTableFactory factory;
  return factory.QueryInterface(riid, out);
}

}  // namespace ole
}  // namespace winport

extern "C" {

HRESULT WINAPI CoMarshalInterThreadInterfaceInStream(REFIID riid, LPUNKNOWN pUnk, LPSTREAM* ppStm) {
  if (ppStm == NULL) return E_INVALIDARG;
  *ppStm = NULL;
  if (pUnk == NULL) return E_INVALIDARG;
  ThreadPacket packet = {kThreadMark, NULL};
  HRESULT hr = pUnk->QueryInterface(riid, (void**)&packet.punk);
  if (FAILED(hr)) return hr;
  IStream* pStm = NULL;
  hr = CreateStreamOnHGlobal(NULL, TRUE, &pStm);
  if (SUCCEEDED(hr)) hr = pStm->Write(&packet, sizeof(packet), NULL);
  if (SUCCEEDED(hr)) {
    LARGE_INTEGER zero;
    zero.QuadPart = 0;
    hr = pStm->Seek(zero, STREAM_SEEK_SET, NULL);
  }
  if (FAILED(hr)) {
    packet.punk->Release();
    if (pStm != NULL) pStm->Release();
    return hr;
  }
  *ppStm = pStm;
  return S_OK;
}

HRESULT WINAPI CoGetInterfaceAndReleaseStream(LPSTREAM pStm, REFIID riid, LPVOID* ppv) {
  if (ppv == NULL) {
    if (pStm != NULL) pStm->Release();
    return E_INVALIDARG;
  }
  *ppv = NULL;
  if (pStm == NULL) return E_INVALIDARG;
  ThreadPacket packet = {0, NULL};
  ULONG cb = 0;
  HRESULT hr = pStm->Read(&packet, sizeof(packet), &cb);
  pStm->Release();
  if (FAILED(hr)) return hr;
  if (cb != sizeof(packet) || packet.mark != kThreadMark || packet.punk == NULL)
    return CO_E_OBJNOTCONNECTED;
  // The stream's reference goes; the caller's is the one asked for.
  hr = packet.punk->QueryInterface(riid, ppv);
  packet.punk->Release();
  return hr;
}

HRESULT WINAPI CoCreateFreeThreadedMarshaler(LPUNKNOWN punkOuter, LPUNKNOWN* ppunkMarshal) {
  if (ppunkMarshal == NULL) return E_INVALIDARG;
  FreeThreadedMarshaler* p = new (std::nothrow) FreeThreadedMarshaler(punkOuter);
  if (p == NULL) {
    *ppunkMarshal = NULL;
    return E_OUTOFMEMORY;
  }
  *ppunkMarshal = p->Inner();  // its count of 1 is the caller's
  return S_OK;
}

}  // extern "C"
