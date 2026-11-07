// unknwn.h - IUnknown and IClassFactory.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#ifndef _WINPORT_UNKNWN_H
#define _WINPORT_UNKNWN_H

#include <wtypes.h>

extern "C" const IID IID_IUnknown;
extern "C" const IID IID_IClassFactory;

// __uuidof(I) is the interface's IID: each header names its interfaces'
// IIDs with WINPORT_UUIDOF.
template <class T>
struct __winport_uuidof;
#define __uuidof(T) (::__winport_uuidof<T>::iid())
#define WINPORT_UUIDOF(I)                              \
  template <>                                          \
  struct __winport_uuidof<I> {                         \
    static const IID& iid() { return IID_##I; }        \
  };

interface IUnknown {
  virtual HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObject) = 0;
  virtual ULONG STDMETHODCALLTYPE AddRef() = 0;
  virtual ULONG STDMETHODCALLTYPE Release() = 0;
};
typedef IUnknown* LPUNKNOWN;

interface IClassFactory : public IUnknown {
  virtual HRESULT STDMETHODCALLTYPE CreateInstance(IUnknown* pUnkOuter, REFIID riid, void** ppvObject) = 0;
  virtual HRESULT STDMETHODCALLTYPE LockServer(BOOL fLock) = 0;
};
typedef IClassFactory* LPCLASSFACTORY;

WINPORT_UUIDOF(IUnknown)
WINPORT_UUIDOF(IClassFactory)

#endif
