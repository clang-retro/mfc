// com_wire.h - how calls between processes are put on the wire, for
// com_remote.cpp (the transport) and com_proxy.cpp (the interfaces'
// proxies and stubs).
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "winport.h"

#include <oaidl.h>
#include <objbase.h>

#include <stdint.h>
#include <string.h>

#include <atomic>
#include <mutex>
#include <string>
#include <vector>

namespace winport {
namespace com {

// Both ends are the same build of the layer, so values go as they are
// in memory.
struct Writer {
  std::vector<BYTE> b;
  void Put(const void* p, size_t n) { b.insert(b.end(), (const BYTE*)p, (const BYTE*)p + n); }
  void U16(uint16_t v) { Put(&v, sizeof v); }
  void U32(uint32_t v) { Put(&v, sizeof v); }
  void U64(uint64_t v) { Put(&v, sizeof v); }
  void Guid(const GUID& g) { Put(&g, sizeof g); }
  void Bytes(const void* p, uint32_t n) {
    U32(n);
    Put(p, n);
  }
  // A string that may be NULL.
  void Str(LPCOLESTR s) {
    if (s == NULL) {
      U32(0xffffffffu);
      return;
    }
    Bytes(s, (uint32_t)(wcslen(s) * sizeof(OLECHAR)));
  }
  void Bstr(BSTR s);  // its length, embedded zeros and all
};

struct Reader {
  const BYTE* p;
  size_t n, pos = 0;
  bool bad = false;
  explicit Reader(const std::vector<BYTE>& v) : p(v.data()), n(v.size()) {}
  bool Get(void* out, size_t k) {
    if (bad || n - pos < k) {
      bad = true;
      memset(out, 0, k);
      return false;
    }
    memcpy(out, p + pos, k);
    pos += k;
    return true;
  }
  uint16_t U16() {
    uint16_t v;
    Get(&v, sizeof v);
    return v;
  }
  uint32_t U32() {
    uint32_t v;
    Get(&v, sizeof v);
    return v;
  }
  uint64_t U64() {
    uint64_t v;
    Get(&v, sizeof v);
    return v;
  }
  GUID Guid() {
    GUID g;
    Get(&g, sizeof g);
    return g;
  }
  std::vector<BYTE> Bytes() {
    uint32_t k = U32();
    std::vector<BYTE> v;
    if (bad || n - pos < k) {
      bad = true;
      return v;
    }
    v.assign(p + pos, p + pos + k);
    pos += k;
    return v;
  }
  // A string, or NULL: *isNull says which.
  std::wstring Str(bool* isNull = NULL);
  // A string in task memory, or NULL.
  LPOLESTR TaskStr();
  BSTR Bstr();
};

class ProxyManager;

// One call to a stub: the object's interface, the method (its place in
// the vtable), the arguments, and the reply's values.
struct StubCall {
  IUnknown* itf;
  uint32_t method;
  Reader* in;
  Writer* out;
  DWORD peer;  // the caller
};

// An interface the layer can call between processes.
struct ProxyBase {
  ProxyManager* pm;
  explicit ProxyBase(ProxyManager* p) : pm(p) {}
  virtual ~ProxyBase() {}
};

struct ProxyInfo {
  const IID* iid;
  ProxyBase* (*make)(ProxyManager* pm, IUnknown** itf);
  HRESULT (*stub)(StubCall& call);
};
const ProxyInfo* FindProxyInfo(REFIID iid);     // com_proxy.cpp
const ProxyInfo* FindOleProxyInfo(REFIID iid);  // com_ole.cpp: OLE documents' interfaces

// The local stand-in for another program's object: its identity
// (IUnknown), with a proxy for each interface asked for.
class ProxyManager : public IUnknown {
public:
  DWORD owner;
  uint64_t oid;
  std::atomic<LONG> refs{0};
  LONG remoteRefs = 0;  // the owner's references held for this program

  ProxyManager(DWORD owner, uint64_t oid);
  virtual ~ProxyManager();
  STDMETHOD(QueryInterface)(REFIID riid, void** out) override;
  STDMETHOD_(ULONG, AddRef)() override;
  STDMETHOD_(ULONG, Release)() override;

  // An interface the owner says the object has.
  HRESULT AddInterface(REFIID riid, void** out);
  // A call to the object; *reply has what the stub wrote after the result.
  HRESULT Invoke(REFIID iid, uint32_t method, const Writer& args, std::vector<BYTE>* reply);

private:
  struct Itf {
    IID iid;
    ProxyBase* proxy;
    IUnknown* itf;
  };
  std::mutex lock;
  std::vector<Itf> itfs;
};

HRESULT CallObject(DWORD owner, uint64_t oid, REFIID iid, uint32_t method, const Writer& args,
                   std::vector<BYTE>* reply);

// Interface pointers: an object handed to the program `to`, and one
// received (AddRef'd: a proxy, or this program's own object come back).
void PutInterface(Writer& w, IUnknown* p, REFIID iid, DWORD to);
HRESULT GetInterface(Reader& r, void** out);
// An [out] pointer a stub got: written, then released.
void PutInterfaceOut(Writer& w, HRESULT hr, IUnknown* p, REFIID iid, DWORD to);

// VARIANTs (com_proxy.cpp). PutVariant says false for a type it cannot
// send.
bool PutVariant(Writer& w, const VARIANT* v, DWORD to);
HRESULT GetVariant(Reader& r, VARIANT* v);

// What a call that came back garbled fails with: RPC_E_SERVERFAULT.
const HRESULT kBadReply = (HRESULT)0x80010105L;

// Type information, by value (typelib.cpp): the library it is from and
// which of its types. WriteTypeInfo says false (and writes none) for
// type information the layer did not make.
bool WriteTypeInfo(Writer& w, ITypeInfo* ti);
HRESULT ReadTypeInfo(Reader& r, ITypeInfo** out);

// Runs the calls waiting for this thread, when it is the apartment.
void Drain();

}  // namespace com
}  // namespace winport
