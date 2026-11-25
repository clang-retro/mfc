// unknwn.h - IUnknown and IClassFactory.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#ifndef _WINPORT_UNKNWN_H
#define _WINPORT_UNKNWN_H

#include <wtypes.h>

__WINPORT_EXTERN_C const IID IID_IUnknown;
__WINPORT_EXTERN_C const IID IID_IClassFactory;

// __uuidof(I) is the interface's IID: each header names its interfaces'
// IIDs with WINPORT_UUIDOF.
#ifdef __cplusplus
template <class T>
struct __winport_uuidof;
#endif
#ifndef __cplusplus
// C has no __uuidof; its programs name the IIDs.
#define WINPORT_UUIDOF(I)
#define __CRT_UUID_DECL(I, l, w1, w2, b1, b2, b3, b4, b5, b6, b7, b8)
#else
#define __uuidof(T) (::__winport_uuidof<T>::iid())
#define WINPORT_UUIDOF(I)                              \
  template <>                                          \
  struct __winport_uuidof<I> {                         \
    static constexpr const IID& iid() { return IID_##I; } \
  };
// mingw-w64's form, for an interface whose IID has no IID_ name.
#define __CRT_UUID_DECL(I, l, w1, w2, b1, b2, b3, b4, b5, b6, b7, b8)       \
  template <>                                                             \
  struct __winport_uuidof<I> {                                            \
    static constexpr IID v = {l, w1, w2, {b1, b2, b3, b4, b5, b6, b7, b8}}; \
    static constexpr const IID& iid() { return v; }                       \
  };
#endif

#ifdef __cplusplus
interface IUnknown {
  virtual HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObject) = 0;
  virtual ULONG STDMETHODCALLTYPE AddRef() = 0;
  virtual ULONG STDMETHODCALLTYPE Release() = 0;
};
#else
typedef struct IUnknown IUnknown;
#endif
typedef IUnknown* LPUNKNOWN;

#ifdef __cplusplus
interface IClassFactory : public IUnknown {
  virtual HRESULT STDMETHODCALLTYPE CreateInstance(IUnknown* pUnkOuter, REFIID riid, void** ppvObject) = 0;
  virtual HRESULT STDMETHODCALLTYPE LockServer(BOOL fLock) = 0;
};
#else
typedef struct IClassFactory IClassFactory;
#endif
typedef IClassFactory* LPCLASSFACTORY;

WINPORT_UUIDOF(IUnknown)
WINPORT_UUIDOF(IClassFactory)

#endif
