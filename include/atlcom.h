// atlcom.h - ATL's COM objects: the object classes that give a class its
// IUnknown (CComObject and the aggregated, stack, global and tear-off
// forms), the interface map that answers QueryInterface (BEGIN_COM_MAP),
// creators and class factories, CComCoClass, the object map of a server,
// IDispatchImpl over a type library, rich error information, connection
// points and enumerators.
//
//   class ATL_NO_VTABLE CThing : public CComObjectRootEx<CComSingleThreadModel>,
//                                public CComCoClass<CThing, &CLSID_Thing>,
//                                public IDispatchImpl<IThing, &IID_IThing, &LIBID_ThingLib> {
//   public:
//     DECLARE_REGISTRY_RESOURCEID(IDR_THING)
//     BEGIN_COM_MAP(CThing)
//       COM_INTERFACE_ENTRY(IThing)
//       COM_INTERFACE_ENTRY(IDispatch)
//     END_COM_MAP()
//     STDMETHOD(get_Name)(BSTR* p);
//   };
//   OBJECT_ENTRY_AUTO(CLSID_Thing, CThing)
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#ifndef __ATLCOM_H__
#define __ATLCOM_H__

#include "atlbase.h"
#include <objsafe.h>
#include <stdlib.h>
#include <new>

// mingw-w64's objsafe.h gives IObjectSafety no __uuidof.
#ifndef _ATL_NO_OBJSAFE_UUID
__CRT_UUID_DECL(IObjectSafety, 0xcb5bdc81, 0x93c1, 0x11cf, 0x8f, 0x20, 0x00, 0x80, 0x5f, 0x2c,
                0xd0, 0x64)
#endif

namespace ATL {

// The GUID of nothing, so that a server needs no GUID_NULL from libuuid.
inline const GUID _ATL_GUID_NULL = {0, 0, 0, {0, 0, 0, 0, 0, 0, 0, 0}};

#define _ATL_IIDOF(x) __uuidof(x)

// ---------------------------------------------------------------------
// Rich error information: the error object a failing method leaves for
// its caller (ISupportErrorInfo, IErrorInfo).

inline HRESULT WINAPI AtlSetErrorInfo(const CLSID& clsid, LPCOLESTR lpszDesc, DWORD dwHelpID,
                                      LPCOLESTR lpszHelpFile, const IID& iid, HRESULT hRes,
                                      HINSTANCE hInst) {
  WCHAR szDesc[1024];
  szDesc[0] = 0;
  // A string resource when the description is an ID.
  if (IS_INTRESOURCE(lpszDesc)) {
    UINT nID = LOWORD((DWORD_PTR)lpszDesc);
    ::LoadStringW(hInst, nID, szDesc, 1024);
    if (hRes == 0) hRes = MAKE_HRESULT(SEVERITY_ERROR, FACILITY_ITF, nID);
    lpszDesc = szDesc;
  }
  if (hRes == 0) hRes = DISP_E_EXCEPTION;
  ICreateErrorInfo* pICEI = NULL;
  if (SUCCEEDED(::CreateErrorInfo(&pICEI))) {
    pICEI->SetGUID(iid);
    LPOLESTR lpszProgID = NULL;
    if (SUCCEEDED(::ProgIDFromCLSID(clsid, &lpszProgID)) && lpszProgID != NULL) {
      pICEI->SetSource(lpszProgID);
      ::CoTaskMemFree(lpszProgID);
    }
    if (dwHelpID != 0 && lpszHelpFile != NULL) {
      pICEI->SetHelpContext(dwHelpID);
      pICEI->SetHelpFile(const_cast<LPOLESTR>(lpszHelpFile));
    }
    pICEI->SetDescription(const_cast<LPOLESTR>(lpszDesc));
    IErrorInfo* pErrorInfo = NULL;
    if (SUCCEEDED(pICEI->QueryInterface(IID_IErrorInfo, (void**)&pErrorInfo))) {
      ::SetErrorInfo(0, pErrorInfo);
      pErrorInfo->Release();
    }
    pICEI->Release();
  }
  return hRes;
}

inline HRESULT WINAPI AtlReportError(const CLSID& clsid, LPCOLESTR lpszDesc,
                                     const IID& iid = _ATL_GUID_NULL, HRESULT hRes = 0) {
  return AtlSetErrorInfo(clsid, lpszDesc, 0, NULL, iid, hRes, NULL);
}
inline HRESULT WINAPI AtlReportError(const CLSID& clsid, LPCOLESTR lpszDesc, DWORD dwHelpID,
                                     LPCOLESTR lpszHelpFile, const IID& iid = _ATL_GUID_NULL,
                                     HRESULT hRes = 0) {
  return AtlSetErrorInfo(clsid, lpszDesc, dwHelpID, lpszHelpFile, iid, hRes, NULL);
}
inline HRESULT WINAPI AtlReportError(const CLSID& clsid, LPCSTR lpszDesc,
                                     const IID& iid = _ATL_GUID_NULL, HRESULT hRes = 0) {
  USES_CONVERSION;
  return AtlSetErrorInfo(clsid, A2COLE(lpszDesc), 0, NULL, iid, hRes, NULL);
}
inline HRESULT WINAPI AtlReportError(const CLSID& clsid, LPCSTR lpszDesc, DWORD dwHelpID,
                                     LPCSTR lpszHelpFile, const IID& iid = _ATL_GUID_NULL,
                                     HRESULT hRes = 0) {
  USES_CONVERSION;
  LPCOLESTR lpszDescW = A2COLE(lpszDesc);
  LPCOLESTR lpszHelpFileW = lpszHelpFile != NULL ? A2COLE(lpszHelpFile) : NULL;
  return AtlSetErrorInfo(clsid, lpszDescW, dwHelpID, lpszHelpFileW, iid, hRes, NULL);
}
inline HRESULT WINAPI AtlReportError(const CLSID& clsid, UINT nID, const IID& iid = _ATL_GUID_NULL,
                                     HRESULT hRes = 0,
                                     HINSTANCE hInst = _AtlBaseModule.GetResourceInstance()) {
  return AtlSetErrorInfo(clsid, (LPCOLESTR)MAKEINTRESOURCEW(nID), 0, NULL, iid, hRes, hInst);
}
inline HRESULT WINAPI AtlReportError(const CLSID& clsid, UINT nID, DWORD dwHelpID,
                                     LPCOLESTR lpszHelpFile, const IID& iid = _ATL_GUID_NULL,
                                     HRESULT hRes = 0,
                                     HINSTANCE hInst = _AtlBaseModule.GetResourceInstance()) {
  return AtlSetErrorInfo(clsid, (LPCOLESTR)MAKEINTRESOURCEW(nID), dwHelpID, lpszHelpFile, iid,
                         hRes, hInst);
}

// ---------------------------------------------------------------------
// The data the special interface map entries point at.

struct _ATL_CHAINDATA {
  DWORD_PTR dwOffset;
  const _ATL_INTMAP_ENTRY* (WINAPI* pFunc)();
};
template <class base, class derived>
class _CComChainData {
public:
  static _ATL_CHAINDATA data;
};
template <class base, class derived>
_ATL_CHAINDATA _CComChainData<base, derived>::data = {offsetofclass(base, derived),
                                                      base::_GetEntries};

struct _ATL_CACHEDATA {
  DWORD dwOffsetVar;
  _ATL_CREATORFUNC* pFunc;
};
template <class Creator, DWORD dwVar>
class _CComCacheData {
public:
  static _ATL_CACHEDATA data;
};
template <class Creator, DWORD dwVar>
_ATL_CACHEDATA _CComCacheData<Creator, dwVar>::data = {dwVar, Creator::CreateInstance};

struct _ATL_CREATORDATA {
  _ATL_CREATORFUNC* pFunc;
};
template <class Creator>
class _CComCreatorData {
public:
  static _ATL_CREATORDATA data;
};
template <class Creator>
_ATL_CREATORDATA _CComCreatorData<Creator>::data = {Creator::CreateInstance};

// ---------------------------------------------------------------------
// The root of every object: its reference count (or, for an object
// aggregated in another, the outer object's IUnknown), and the functions
// of the interface map's special entries.

class CComObjectRootBase {
public:
  union {
    LONG m_dwRef;
    IUnknown* m_pOuterUnknown;
  };

  CComObjectRootBase() : m_dwRef(0) {}

  HRESULT FinalConstruct() { return S_OK; }
  void FinalRelease() {}
  HRESULT _AtlInitialConstruct() { return S_OK; }
  HRESULT _AtlFinalConstruct() { return S_OK; }
  void _AtlFinalRelease() {}
  void SetVoid(void*) {}
  void InternalFinalConstructAddRef() {}
  void InternalFinalConstructRelease() {}
  static void WINAPI ObjectMain(bool) {}
  static const _ATL_CATMAP_ENTRY* GetCategoryMap() { return NULL; }

  ULONG OuterAddRef() { return m_pOuterUnknown->AddRef(); }
  ULONG OuterRelease() { return m_pOuterUnknown->Release(); }
  HRESULT OuterQueryInterface(REFIID iid, void** ppvObject) {
    return m_pOuterUnknown->QueryInterface(iid, ppvObject);
  }

  static HRESULT WINAPI InternalQueryInterface(void* pThis, const _ATL_INTMAP_ENTRY* pEntries,
                                               REFIID iid, void** ppvObject) {
    return AtlInternalQueryInterface(pThis, pEntries, iid, ppvObject);
  }

  // COM_INTERFACE_ENTRY_AGGREGATE: the inner object at offset dw.
  static HRESULT WINAPI _Delegate(void* pv, REFIID iid, void** ppvObject, DWORD_PTR dw) {
    IUnknown* p = *(IUnknown**)((DWORD_PTR)pv + dw);
    if (p == NULL) return E_NOINTERFACE;
    return p->QueryInterface(iid, ppvObject);
  }
  // COM_INTERFACE_ENTRY_CHAIN: a base class's map.
  static HRESULT WINAPI _Chain(void* pv, REFIID iid, void** ppvObject, DWORD_PTR dw) {
    _ATL_CHAINDATA* pcd = (_ATL_CHAINDATA*)dw;
    void* p = (void*)((DWORD_PTR)pv + pcd->dwOffset);
    HRESULT hr = InternalQueryInterface(p, pcd->pFunc(), iid, ppvObject);
    // A chained map that has no answer lets the walk go on.
    return hr == E_NOINTERFACE ? S_FALSE : hr;
  }
  // COM_INTERFACE_ENTRY_CACHED_TEAR_OFF and _AUTOAGGREGATE: made the
  // first time, then kept in the member at dwOffsetVar.
  static HRESULT WINAPI _Cache(void* pv, REFIID iid, void** ppvObject, DWORD_PTR dw) {
    _ATL_CACHEDATA* pcd = (_ATL_CACHEDATA*)dw;
    IUnknown** pp = (IUnknown**)((DWORD_PTR)pv + pcd->dwOffsetVar);
    HRESULT hr = E_NOINTERFACE;
    if (*pp == NULL) hr = pcd->pFunc(pv, IID_IUnknown, (void**)pp);
    if (*pp != NULL) hr = (*pp)->QueryInterface(iid, ppvObject);
    return hr;
  }
  // COM_INTERFACE_ENTRY_TEAR_OFF: a new object each time.
  static HRESULT WINAPI _Creator(void* pv, REFIID iid, void** ppvObject, DWORD_PTR dw) {
    _ATL_CREATORDATA* pcd = (_ATL_CREATORDATA*)dw;
    return pcd->pFunc(pv, iid, ppvObject);
  }
  static HRESULT WINAPI _NoInterface(void*, REFIID, void**, DWORD_PTR) { return E_NOINTERFACE; }
  static HRESULT WINAPI _Break(void*, REFIID, void**, DWORD_PTR) {
    ::DebugBreak();
    return S_FALSE;
  }
};

template <class ThreadModel>
class CComObjectLockT {
public:
  CComObjectLockT(ThreadModel* p) : m_p(p) {
    if (p != NULL) p->Lock();
  }
  ~CComObjectLockT() {
    if (m_p != NULL) m_p->Unlock();
  }
  ThreadModel* m_p;
};

template <class ThreadModel>
class CComObjectRootEx : public CComObjectRootBase {
public:
  typedef ThreadModel _ThreadModel;
  typedef typename ThreadModel::AutoCriticalSection _CritSec;
  typedef typename ThreadModel::AutoDeleteCriticalSection _AutoDelCritSec;
  typedef CComObjectLockT<CComObjectRootEx<ThreadModel> > ObjectLock;

  ULONG InternalAddRef() { return ThreadModel::Increment(&m_dwRef); }
  ULONG InternalRelease() { return ThreadModel::Decrement(&m_dwRef); }
  void Lock() { m_critsec.Lock(); }
  void Unlock() { m_critsec.Unlock(); }

private:
  _CritSec m_critsec;
};

typedef CComObjectRootEx<CComObjectThreadModel> CComObjectRoot;

#define DECLARE_PROTECT_FINAL_CONSTRUCT()                  \
  void InternalFinalConstructAddRef() { InternalAddRef(); } \
  void InternalFinalConstructRelease() { InternalRelease(); }

#define DECLARE_GET_CONTROLLING_UNKNOWN() \
public:                                   \
  virtual IUnknown* GetControllingUnknown() { return GetUnknown(); }

#define DECLARE_OBJECT_DESCRIPTION(x) \
  static LPCTSTR WINAPI GetObjectDescription() { return _T(x); }

// ---------------------------------------------------------------------
// The interface map: the interfaces QueryInterface answers for, the
// first of them the object's IUnknown.
//
//   BEGIN_COM_MAP(CThing)
//     COM_INTERFACE_ENTRY(IThing)
//     COM_INTERFACE_ENTRY2(IDispatch, IThing)
//     COM_INTERFACE_ENTRY_TEAR_OFF(IID_IRare, CRareTearOff)
//     COM_INTERFACE_ENTRY_AGGREGATE(IID_IInner, m_punkInner.p)
//     COM_INTERFACE_ENTRY_CHAIN(CBase)
//   END_COM_MAP()

#define BEGIN_COM_MAP(x)                                                                  \
public:                                                                                   \
  typedef x _ComMapClass;                                                                 \
  IUnknown* _GetRawUnknown() { return (IUnknown*)((DWORD_PTR)this + _GetEntries()->dw); } \
  IUnknown* GetUnknown() { return _GetRawUnknown(); }                                     \
  HRESULT _InternalQueryInterface(REFIID iid, void** ppvObject) {                         \
    return this->InternalQueryInterface(this, _GetEntries(), iid, ppvObject);             \
  }                                                                                       \
  static const ATL::_ATL_INTMAP_ENTRY* WINAPI _GetEntries() {                             \
    static const ATL::_ATL_INTMAP_ENTRY _entries[] = {

// AddRef, Release and QueryInterface are named here so that the class's
// own code calls them without ambiguity; CComObject defines them.
#define END_COM_MAP()                                   \
  {NULL, 0, 0}                                          \
  }                                                     \
  ;                                                     \
  return _entries;                                      \
  }                                                     \
  virtual ULONG STDMETHODCALLTYPE AddRef() override = 0;  \
  virtual ULONG STDMETHODCALLTYPE Release() override = 0; \
  STDMETHOD(QueryInterface)(REFIID, void**) override = 0;

// The place of a member (offsetof, which a class with virtual functions
// does not allow).
#define _ATL_MEMBER_OFFSET(c, m) \
  ((DWORD_PTR)&(((c*)_ATL_OFFSET_BASE)->m) - _ATL_OFFSET_BASE)
// The same as a constant, for a template argument: offsetof, which clang
// computes for any class, with its warning off.
#define _ATL_MEMBER_OFFSET_CONST(c, m)                              \
  _Pragma("clang diagnostic push")                                  \
  _Pragma("clang diagnostic ignored \"-Winvalid-offsetof\"")         \
  offsetof(c, m)                                                     \
  _Pragma("clang diagnostic pop")

#define COM_INTERFACE_ENTRY(x) \
  {&_ATL_IIDOF(x), offsetofclass(x, _ComMapClass), _ATL_SIMPLEMAPENTRY},
#define COM_INTERFACE_ENTRY_IID(iid, x) {&iid, offsetofclass(x, _ComMapClass), _ATL_SIMPLEMAPENTRY},
#define COM_INTERFACE_ENTRY2(x, x2)                                                   \
  {&_ATL_IIDOF(x), (DWORD_PTR)((x*)(x2*)((_ComMapClass*)_ATL_OFFSET_BASE)) - _ATL_OFFSET_BASE, \
   _ATL_SIMPLEMAPENTRY},
#define COM_INTERFACE_ENTRY2_IID(iid, x, x2)                                               \
  {&iid, (DWORD_PTR)((x*)(x2*)((_ComMapClass*)_ATL_OFFSET_BASE)) - _ATL_OFFSET_BASE, \
   _ATL_SIMPLEMAPENTRY},
#define COM_INTERFACE_ENTRY_IMPL(x) COM_INTERFACE_ENTRY_IID(_ATL_IIDOF(x), x##Impl<_ComMapClass>)
#define COM_INTERFACE_ENTRY_IMPL_IID(iid, x) COM_INTERFACE_ENTRY_IID(iid, x##Impl<_ComMapClass>)
#define COM_INTERFACE_ENTRY_FUNC(iid, dw, func) {&iid, dw, func},
#define COM_INTERFACE_ENTRY_FUNC_BLIND(dw, func) {NULL, dw, func},
#define COM_INTERFACE_ENTRY_TEAR_OFF(iid, x)                                                \
  {&iid,                                                                                    \
   (DWORD_PTR)&ATL::_CComCreatorData<ATL::CComInternalCreator<ATL::CComTearOffObject<x> > >::data, \
   _Creator},
#define COM_INTERFACE_ENTRY_CACHED_TEAR_OFF(iid, x, punk)                                 \
  {&iid,                                                                                  \
   (DWORD_PTR)&ATL::_CComCacheData<ATL::CComCreator<ATL::CComCachedTearOffObject<x> >,   \
                                   (DWORD)_ATL_MEMBER_OFFSET_CONST(_ComMapClass, punk)>::data,            \
   _Cache},
#define COM_INTERFACE_ENTRY_AGGREGATE(iid, punk) \
  {&iid, (DWORD_PTR)_ATL_MEMBER_OFFSET(_ComMapClass, punk), _Delegate},
#define COM_INTERFACE_ENTRY_AGGREGATE_BLIND(punk) \
  {NULL, (DWORD_PTR)_ATL_MEMBER_OFFSET(_ComMapClass, punk), _Delegate},
#define COM_INTERFACE_ENTRY_AUTOAGGREGATE(iid, punk, clsid)                          \
  {&iid,                                                                             \
   (DWORD_PTR)&ATL::_CComCacheData<ATL::CComAggregateCreator<_ComMapClass, &clsid>, \
                                   (DWORD)_ATL_MEMBER_OFFSET_CONST(_ComMapClass, punk)>::data,       \
   _Cache},
#define COM_INTERFACE_ENTRY_AUTOAGGREGATE_BLIND(punk, clsid)                         \
  {NULL,                                                                             \
   (DWORD_PTR)&ATL::_CComCacheData<ATL::CComAggregateCreator<_ComMapClass, &clsid>, \
                                   (DWORD)_ATL_MEMBER_OFFSET_CONST(_ComMapClass, punk)>::data,       \
   _Cache},
#define COM_INTERFACE_ENTRY_CHAIN(classname) \
  {NULL, (DWORD_PTR)&ATL::_CComChainData<classname, _ComMapClass>::data, _Chain},
#define COM_INTERFACE_ENTRY_NOINTERFACE(x) {&_ATL_IIDOF(x), 0, _NoInterface},
#define COM_INTERFACE_ENTRY_BREAK(x) {&_ATL_IIDOF(x), 0, _Break},

// ---------------------------------------------------------------------
// The objects: a class with a COM map becomes a COM object as the Base of
// one of these, which give it AddRef, Release and QueryInterface.

// The usual object, on the heap; it keeps the module loaded.
template <class Base>
class CComObject : public Base {
public:
  typedef Base _BaseClass;
  CComObject(void* = NULL) { _pAtlModule->Lock(); }
  virtual ~CComObject() {
    this->m_dwRef = -(LONG_MAX / 2);
    this->FinalRelease();
    this->_AtlFinalRelease();
    _pAtlModule->Unlock();
  }
  STDMETHOD_(ULONG, AddRef)() override { return this->InternalAddRef(); }
  STDMETHOD_(ULONG, Release)() override {
    ULONG l = this->InternalRelease();
    if (l == 0) delete this;
    return l;
  }
  STDMETHOD(QueryInterface)(REFIID iid, void** ppvObject) override {
    return this->_InternalQueryInterface(iid, ppvObject);
  }
  template <class Q>
  HRESULT STDMETHODCALLTYPE QueryInterface(Q** pp) {
    return QueryInterface(__uuidof(Q), (void**)pp);
  }

  // A new object, its reference count 0.
  static HRESULT WINAPI CreateInstance(CComObject<Base>** pp);
};

template <class Base>
HRESULT WINAPI CComObject<Base>::CreateInstance(CComObject<Base>** pp) {
  if (pp == NULL) return E_POINTER;
  *pp = NULL;
  HRESULT hr = E_OUTOFMEMORY;
  CComObject<Base>* p = new (std::nothrow) CComObject<Base>();
  if (p != NULL) {
    p->SetVoid(NULL);
    p->InternalFinalConstructAddRef();
    hr = p->_AtlInitialConstruct();
    if (SUCCEEDED(hr)) hr = p->FinalConstruct();
    if (SUCCEEDED(hr)) hr = p->_AtlFinalConstruct();
    p->InternalFinalConstructRelease();
    if (hr != S_OK) {
      delete p;
      p = NULL;
    }
  }
  *pp = p;
  return hr;
}

// One that does not keep the module loaded (a class factory of an EXE
// server, say).
template <class Base>
class CComObjectNoLock : public Base {
public:
  typedef Base _BaseClass;
  CComObjectNoLock(void* = NULL) {}
  virtual ~CComObjectNoLock() {
    this->m_dwRef = -(LONG_MAX / 2);
    this->FinalRelease();
    this->_AtlFinalRelease();
  }
  STDMETHOD_(ULONG, AddRef)() override { return this->InternalAddRef(); }
  STDMETHOD_(ULONG, Release)() override {
    ULONG l = this->InternalRelease();
    if (l == 0) delete this;
    return l;
  }
  STDMETHOD(QueryInterface)(REFIID iid, void** ppvObject) override {
    return this->_InternalQueryInterface(iid, ppvObject);
  }
};

// A class factory: the module is locked while someone else holds it.
template <class Base>
class CComObjectCached : public Base {
public:
  typedef Base _BaseClass;
  CComObjectCached(void* = NULL) {}
  virtual ~CComObjectCached() {
    this->m_dwRef = -(LONG_MAX / 2);
    this->FinalRelease();
    this->_AtlFinalRelease();
  }
  STDMETHOD_(ULONG, AddRef)() override {
    ULONG l = this->InternalAddRef();
    if (l == 2) _pAtlModule->Lock();
    return l;
  }
  STDMETHOD_(ULONG, Release)() override {
    ULONG l = this->InternalRelease();
    if (l == 0)
      delete this;
    else if (l == 1)
      _pAtlModule->Unlock();
    return l;
  }
  STDMETHOD(QueryInterface)(REFIID iid, void** ppvObject) override {
    return this->_InternalQueryInterface(iid, ppvObject);
  }
};

// An object that lives as long as the module: its references are locks
// on the module.
template <class Base>
class CComObjectGlobal : public Base {
public:
  typedef Base _BaseClass;
  HRESULT m_hResFinalConstruct;
  CComObjectGlobal(void* = NULL) {
    m_hResFinalConstruct = this->_AtlInitialConstruct();
    if (SUCCEEDED(m_hResFinalConstruct)) m_hResFinalConstruct = this->FinalConstruct();
    if (SUCCEEDED(m_hResFinalConstruct)) m_hResFinalConstruct = this->_AtlFinalConstruct();
  }
  virtual ~CComObjectGlobal() {
    this->FinalRelease();
    this->_AtlFinalRelease();
  }
  STDMETHOD_(ULONG, AddRef)() override { return (ULONG)_pAtlModule->Lock(); }
  STDMETHOD_(ULONG, Release)() override { return (ULONG)_pAtlModule->Unlock(); }
  STDMETHOD(QueryInterface)(REFIID iid, void** ppvObject) override {
    return this->_InternalQueryInterface(iid, ppvObject);
  }
};

// An object on the stack, whose references do not count; it must not
// outlive the block.
template <class Base>
class CComObjectStack : public Base {
public:
  typedef Base _BaseClass;
  HRESULT m_hResFinalConstruct;
  CComObjectStack(void* = NULL) {
    m_hResFinalConstruct = this->_AtlInitialConstruct();
    if (SUCCEEDED(m_hResFinalConstruct)) m_hResFinalConstruct = this->FinalConstruct();
    if (SUCCEEDED(m_hResFinalConstruct)) m_hResFinalConstruct = this->_AtlFinalConstruct();
  }
  virtual ~CComObjectStack() {
    this->FinalRelease();
    this->_AtlFinalRelease();
  }
  STDMETHOD_(ULONG, AddRef)() override { return 0; }
  STDMETHOD_(ULONG, Release)() override { return 0; }
  STDMETHOD(QueryInterface)(REFIID iid, void** ppvObject) override {
    return this->_InternalQueryInterface(iid, ppvObject);
  }
};

// The same, but its references count, so that a leak shows (the count is
// checked to be 0 at the end in a debug build).
template <class Base>
class CComObjectStackEx : public CComObjectStack<Base> {
public:
  STDMETHOD_(ULONG, AddRef)() override { return this->InternalAddRef(); }
  STDMETHOD_(ULONG, Release)() override { return this->InternalRelease(); }
  ~CComObjectStackEx() { ATLASSERT(this->m_dwRef == 0); }
};

// The object inside an aggregate: its IUnknown is the outer object's.
template <class Base>
class CComContainedObject : public Base {
public:
  typedef Base _BaseClass;
  CComContainedObject(void* pv) { this->m_pOuterUnknown = (IUnknown*)pv; }
  STDMETHOD_(ULONG, AddRef)() override { return this->OuterAddRef(); }
  STDMETHOD_(ULONG, Release)() override { return this->OuterRelease(); }
  STDMETHOD(QueryInterface)(REFIID iid, void** ppvObject) override {
    return this->OuterQueryInterface(iid, ppvObject);
  }
  template <class Q>
  HRESULT STDMETHODCALLTYPE QueryInterface(Q** pp) {
    return QueryInterface(__uuidof(Q), (void**)pp);
  }
  // It overrides the class's own when that is virtual
  // (DECLARE_GET_CONTROLLING_UNKNOWN).
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Winconsistent-missing-override"
  IUnknown* GetControllingUnknown() { return this->m_pOuterUnknown; }
#pragma clang diagnostic pop
};

// An object made inside another (aggregated): this is its own,
// non-delegating IUnknown; the contained object's interfaces delegate to
// the outer object.
template <class contained>
class CComAggObject : public IUnknown,
                      public CComObjectRootEx<typename contained::_ThreadModel::ThreadModelNoCS> {
public:
  typedef contained _BaseClass;
  CComContainedObject<contained> m_contained;

  CComAggObject(void* pv) : m_contained(pv) { _pAtlModule->Lock(); }
  virtual ~CComAggObject() {
    this->m_dwRef = -(LONG_MAX / 2);
    FinalRelease();
    _pAtlModule->Unlock();
  }
  HRESULT _AtlInitialConstruct() {
    HRESULT hr = m_contained._AtlInitialConstruct();
    if (SUCCEEDED(hr)) hr = CComObjectRootEx<typename contained::_ThreadModel::ThreadModelNoCS>::_AtlInitialConstruct();
    return hr;
  }
  HRESULT FinalConstruct() {
    CComObjectRootEx<typename contained::_ThreadModel::ThreadModelNoCS>::FinalConstruct();
    return m_contained.FinalConstruct();
  }
  void FinalRelease() {
    CComObjectRootEx<typename contained::_ThreadModel::ThreadModelNoCS>::FinalRelease();
    m_contained.FinalRelease();
    m_contained._AtlFinalRelease();
  }
  STDMETHOD_(ULONG, AddRef)() override { return this->InternalAddRef(); }
  STDMETHOD_(ULONG, Release)() override {
    ULONG l = this->InternalRelease();
    if (l == 0) delete this;
    return l;
  }
  STDMETHOD(QueryInterface)(REFIID iid, void** ppvObject) override {
    if (ppvObject == NULL) return E_POINTER;
    *ppvObject = NULL;
    if (InlineIsEqualGUID(iid, IID_IUnknown)) {
      *ppvObject = (void*)(IUnknown*)this;
      AddRef();
      return S_OK;
    }
    return m_contained._InternalQueryInterface(iid, ppvObject);
  }
  template <class Q>
  HRESULT STDMETHODCALLTYPE QueryInterface(Q** pp) {
    return QueryInterface(__uuidof(Q), (void**)pp);
  }
  static HRESULT WINAPI CreateInstance(LPUNKNOWN pUnkOuter, CComAggObject<contained>** pp) {
    if (pp == NULL) return E_POINTER;
    *pp = NULL;
    CComAggObject<contained>* p = new (std::nothrow) CComAggObject<contained>(pUnkOuter);
    if (p == NULL) return E_OUTOFMEMORY;
    p->SetVoid(NULL);
    p->InternalFinalConstructAddRef();
    HRESULT hr = p->_AtlInitialConstruct();
    if (SUCCEEDED(hr)) hr = p->FinalConstruct();
    if (SUCCEEDED(hr)) hr = p->_AtlFinalConstruct();
    p->InternalFinalConstructRelease();
    if (hr != S_OK) {
      delete p;
      return hr;
    }
    *pp = p;
    return hr;
  }
};

// An object that may be aggregated or not: without an outer object, it
// is its own.
template <class contained>
class CComPolyObject : public IUnknown,
                       public CComObjectRootEx<typename contained::_ThreadModel::ThreadModelNoCS> {
public:
  typedef contained _BaseClass;
  CComContainedObject<contained> m_contained;

  CComPolyObject(void* pv) : m_contained(pv != NULL ? pv : (void*)static_cast<IUnknown*>(this)) { _pAtlModule->Lock(); }
  virtual ~CComPolyObject() {
    this->m_dwRef = -(LONG_MAX / 2);
    FinalRelease();
    _pAtlModule->Unlock();
  }
  HRESULT _AtlInitialConstruct() { return m_contained._AtlInitialConstruct(); }
  HRESULT FinalConstruct() {
    // The contained object's FinalConstruct may AddRef and Release its
    // outer object, this one when it has no other.
    this->InternalAddRef();
    HRESULT hr = m_contained.FinalConstruct();
    this->InternalRelease();
    return hr;
  }
  void FinalRelease() {
    m_contained.FinalRelease();
    m_contained._AtlFinalRelease();
  }
  STDMETHOD_(ULONG, AddRef)() override { return this->InternalAddRef(); }
  STDMETHOD_(ULONG, Release)() override {
    ULONG l = this->InternalRelease();
    if (l == 0) delete this;
    return l;
  }
  STDMETHOD(QueryInterface)(REFIID iid, void** ppvObject) override {
    if (ppvObject == NULL) return E_POINTER;
    *ppvObject = NULL;
    if (InlineIsEqualGUID(iid, IID_IUnknown)) {
      *ppvObject = (void*)(IUnknown*)this;
      AddRef();
      return S_OK;
    }
    return m_contained._InternalQueryInterface(iid, ppvObject);
  }
  template <class Q>
  HRESULT STDMETHODCALLTYPE QueryInterface(Q** pp) {
    return QueryInterface(__uuidof(Q), (void**)pp);
  }
  static HRESULT WINAPI CreateInstance(LPUNKNOWN pUnkOuter, CComPolyObject<contained>** pp) {
    if (pp == NULL) return E_POINTER;
    *pp = NULL;
    CComPolyObject<contained>* p = new (std::nothrow) CComPolyObject<contained>(pUnkOuter);
    if (p == NULL) return E_OUTOFMEMORY;
    p->SetVoid(NULL);
    p->InternalFinalConstructAddRef();
    HRESULT hr = p->_AtlInitialConstruct();
    if (SUCCEEDED(hr)) hr = p->FinalConstruct();
    if (SUCCEEDED(hr)) hr = p->_AtlFinalConstruct();
    p->InternalFinalConstructRelease();
    if (hr != S_OK) {
      delete p;
      return hr;
    }
    *pp = p;
    return hr;
  }
};

// ---------------------------------------------------------------------
// Tear-offs: an interface whose object is made only when it is asked for,
// so that the owner does not carry its vtable.
//
//   class ATL_NO_VTABLE CRare : public CComTearOffObjectBase<CThing>, public IRare {
//     BEGIN_COM_MAP(CRare) COM_INTERFACE_ENTRY(IRare) END_COM_MAP()
//     STDMETHOD(Method)() { m_pOwner->...; }
//   };

template <class Owner, class ThreadModel = CComObjectThreadModel>
class CComTearOffObjectBase : public CComObjectRootEx<ThreadModel> {
public:
  typedef Owner _OwnerClass;
  Owner* m_pOwner;
  CComTearOffObjectBase() : m_pOwner(NULL) {}
};

template <class Base>
class CComTearOffObject : public Base {
public:
  CComTearOffObject(void* pv) {
    this->m_pOwner = reinterpret_cast<typename Base::_OwnerClass*>(pv);
    this->m_pOwner->GetUnknown()->AddRef();
  }
  virtual ~CComTearOffObject() {
    this->m_dwRef = -(LONG_MAX / 2);
    this->FinalRelease();
    this->_AtlFinalRelease();
    this->m_pOwner->GetUnknown()->Release();
  }
  STDMETHOD_(ULONG, AddRef)() override { return this->InternalAddRef(); }
  STDMETHOD_(ULONG, Release)() override {
    ULONG l = this->InternalRelease();
    if (l == 0) delete this;
    return l;
  }
  // The tear-off's own interfaces, and the owner's for the rest (its
  // IUnknown is the owner's).
  STDMETHOD(QueryInterface)(REFIID iid, void** ppvObject) override {
    if (ppvObject == NULL) return E_POINTER;
    if (!InlineIsEqualGUID(iid, IID_IUnknown) &&
        SUCCEEDED(this->_InternalQueryInterface(iid, ppvObject)))
      return S_OK;
    return this->m_pOwner->GetUnknown()->QueryInterface(iid, ppvObject);
  }
};

// A tear-off made once and kept: aggregated in its owner, which holds its
// IUnknown in a member (and releases it in FinalRelease).
template <class contained>
class CComCachedTearOffObject
    : public IUnknown,
      public CComObjectRootEx<typename contained::_ThreadModel::ThreadModelNoCS> {
public:
  typedef contained _BaseClass;
  CComContainedObject<contained> m_contained;

  CComCachedTearOffObject(void* pv)
      : m_contained(reinterpret_cast<typename contained::_OwnerClass*>(pv)->GetUnknown()) {
    m_contained.m_pOwner = reinterpret_cast<typename contained::_OwnerClass*>(pv);
  }
  virtual ~CComCachedTearOffObject() {
    this->m_dwRef = -(LONG_MAX / 2);
    m_contained.FinalRelease();
    m_contained._AtlFinalRelease();
  }
  HRESULT _AtlInitialConstruct() { return m_contained._AtlInitialConstruct(); }
  HRESULT FinalConstruct() { return m_contained.FinalConstruct(); }
  STDMETHOD_(ULONG, AddRef)() override { return this->InternalAddRef(); }
  STDMETHOD_(ULONG, Release)() override {
    ULONG l = this->InternalRelease();
    if (l == 0) delete this;
    return l;
  }
  STDMETHOD(QueryInterface)(REFIID iid, void** ppvObject) override {
    if (ppvObject == NULL) return E_POINTER;
    *ppvObject = NULL;
    if (InlineIsEqualGUID(iid, IID_IUnknown)) {
      *ppvObject = (void*)(IUnknown*)this;
      AddRef();
      return S_OK;
    }
    return m_contained._InternalQueryInterface(iid, ppvObject);
  }
};

// ---------------------------------------------------------------------
// Creators: what a class factory or a map entry calls to make an object.

template <class T1>
class CComCreator {
public:
  static HRESULT WINAPI CreateInstance(void* pv, REFIID riid, LPVOID* ppv) {
    if (ppv == NULL) return E_POINTER;
    *ppv = NULL;
    T1* p = new (std::nothrow) T1(pv);
    if (p == NULL) return E_OUTOFMEMORY;
    p->SetVoid(pv);
    p->InternalFinalConstructAddRef();
    HRESULT hr = p->_AtlInitialConstruct();
    if (SUCCEEDED(hr)) hr = p->FinalConstruct();
    if (SUCCEEDED(hr)) hr = p->_AtlFinalConstruct();
    p->InternalFinalConstructRelease();
    if (SUCCEEDED(hr)) hr = p->QueryInterface(riid, ppv);
    if (FAILED(hr)) delete p;
    return hr;
  }
};

// The same, asking the object's own map (for a tear-off, whose
// QueryInterface would ask its owner).
template <class T1>
class CComInternalCreator {
public:
  static HRESULT WINAPI CreateInstance(void* pv, REFIID riid, LPVOID* ppv) {
    if (ppv == NULL) return E_POINTER;
    *ppv = NULL;
    T1* p = new (std::nothrow) T1(pv);
    if (p == NULL) return E_OUTOFMEMORY;
    p->SetVoid(pv);
    p->InternalFinalConstructAddRef();
    HRESULT hr = p->_AtlInitialConstruct();
    if (SUCCEEDED(hr)) hr = p->FinalConstruct();
    if (SUCCEEDED(hr)) hr = p->_AtlFinalConstruct();
    p->InternalFinalConstructRelease();
    if (SUCCEEDED(hr)) hr = p->_InternalQueryInterface(riid, ppv);
    if (FAILED(hr)) delete p;
    return hr;
  }
};

template <HRESULT hr>
class CComFailCreator {
public:
  static HRESULT WINAPI CreateInstance(void*, REFIID, LPVOID* ppv) {
    if (ppv != NULL) *ppv = NULL;
    return hr;
  }
};

// T1 when there is no outer object, T2 when there is.
template <class T1, class T2>
class CComCreator2 {
public:
  static HRESULT WINAPI CreateInstance(void* pv, REFIID riid, LPVOID* ppv) {
    return pv == NULL ? T1::CreateInstance(NULL, riid, ppv) : T2::CreateInstance(pv, riid, ppv);
  }
};

// COM_INTERFACE_ENTRY_AUTOAGGREGATE: the inner object of class clsid,
// aggregated in T.
template <class T, const CLSID* pclsid>
class CComAggregateCreator {
public:
  static HRESULT WINAPI CreateInstance(void* pv, REFIID, LPVOID* ppv) {
    T* p = (T*)pv;
    return ::CoCreateInstance(*pclsid, p->GetUnknown(), CLSCTX_ALL, IID_IUnknown, ppv);
  }
};

#define DECLARE_NOT_AGGREGATABLE(x)                                               \
public:                                                                           \
  typedef ATL::CComCreator2<ATL::CComCreator<ATL::CComObject<x> >,                \
                            ATL::CComFailCreator<CLASS_E_NOAGGREGATION> >         \
      _CreatorClass;
#define DECLARE_AGGREGATABLE(x)                                                                 \
public:                                                                                         \
  typedef ATL::CComCreator2<ATL::CComCreator<ATL::CComObject<x> >,                              \
                            ATL::CComCreator<ATL::CComAggObject<x> > >                          \
      _CreatorClass;
#define DECLARE_ONLY_AGGREGATABLE(x)                                                            \
public:                                                                                         \
  typedef ATL::CComCreator2<ATL::CComFailCreator<E_FAIL>, ATL::CComCreator<ATL::CComAggObject<x> > > \
      _CreatorClass;
#define DECLARE_POLY_AGGREGATABLE(x) \
public:                              \
  typedef ATL::CComCreator<ATL::CComPolyObject<x> > _CreatorClass;

// ---------------------------------------------------------------------
// Class factories.

class CComClassFactory : public IClassFactory, public CComObjectRootEx<CComGlobalsThreadModel> {
public:
  _ATL_CREATORFUNC* m_pfnCreateInstance;

  BEGIN_COM_MAP(CComClassFactory)
    COM_INTERFACE_ENTRY(IClassFactory)
  END_COM_MAP()

  CComClassFactory() : m_pfnCreateInstance(NULL) {}
  STDMETHOD(CreateInstance)(LPUNKNOWN pUnkOuter, REFIID riid, void** ppvObj) override {
    if (ppvObj == NULL) return E_POINTER;
    *ppvObj = NULL;
    // An outer object asks for the inner one's IUnknown.
    if (pUnkOuter != NULL && !InlineIsEqualGUID(riid, IID_IUnknown)) return CLASS_E_NOAGGREGATION;
    if (m_pfnCreateInstance == NULL) return E_UNEXPECTED;
    return m_pfnCreateInstance(pUnkOuter, riid, ppvObj);
  }
  STDMETHOD(LockServer)(BOOL fLock) override {
    if (fLock)
      _pAtlModule->Lock();
    else
      _pAtlModule->Unlock();
    return S_OK;
  }
  void SetVoid(void* pv) { m_pfnCreateInstance = (_ATL_CREATORFUNC*)pv; }
};

// A factory that runs each object in a thread of its own: here, as in an
// in-process server, the object is made in the caller's.
typedef CComClassFactory CComClassFactoryAutoThread;

// One object for every client.
template <class T>
class CComClassFactorySingleton : public CComClassFactory {
public:
  IUnknown* m_spObj;
  HRESULT m_hrCreate;
  CComClassFactorySingleton() : m_spObj(NULL), m_hrCreate(S_OK) {}
  ~CComClassFactorySingleton() {
    if (m_spObj != NULL) m_spObj->Release();
  }
  void FinalRelease() {
    if (m_spObj != NULL) {
      m_spObj->Release();
      m_spObj = NULL;
    }
  }
  STDMETHOD(CreateInstance)(LPUNKNOWN pUnkOuter, REFIID riid, void** ppvObj) override {
    if (ppvObj == NULL) return E_POINTER;
    *ppvObj = NULL;
    if (pUnkOuter != NULL) return CLASS_E_NOAGGREGATION;
    if (m_spObj == NULL) {
      this->Lock();
      if (m_spObj == NULL) {
        // The object does not keep the module loaded; the factory does.
        CComObjectCached<T>* p = new (std::nothrow) CComObjectCached<T>();
        if (p == NULL) {
          m_hrCreate = E_OUTOFMEMORY;
        } else {
          p->SetVoid(NULL);
          p->InternalFinalConstructAddRef();
          m_hrCreate = p->_AtlInitialConstruct();
          if (SUCCEEDED(m_hrCreate)) m_hrCreate = p->FinalConstruct();
          if (SUCCEEDED(m_hrCreate)) m_hrCreate = p->_AtlFinalConstruct();
          p->InternalFinalConstructRelease();
          if (SUCCEEDED(m_hrCreate))
            m_hrCreate = p->QueryInterface(IID_IUnknown, (void**)&m_spObj);
          if (FAILED(m_hrCreate)) delete p;
        }
      }
      this->Unlock();
    }
    if (m_spObj == NULL) return FAILED(m_hrCreate) ? m_hrCreate : E_UNEXPECTED;
    return m_spObj->QueryInterface(riid, ppvObj);
  }
};

// IClassFactory2: objects only for a licensed machine or a caller with a
// key. The license class says which:
//   static BOOL VerifyLicenseKey(BSTR bstr);
//   static BOOL GetLicenseKey(DWORD dwReserved, BSTR* pBstr);
//   static BOOL IsLicenseValid();
template <class license>
class CComClassFactory2 : public IClassFactory2,
                          public CComObjectRootEx<CComGlobalsThreadModel>,
                          public license {
public:
  typedef license _LicenseClass;
  _ATL_CREATORFUNC* m_pfnCreateInstance;

  BEGIN_COM_MAP(CComClassFactory2<license>)
    COM_INTERFACE_ENTRY(IClassFactory)
    COM_INTERFACE_ENTRY(IClassFactory2)
  END_COM_MAP()

  CComClassFactory2() : m_pfnCreateInstance(NULL) {}
  STDMETHOD(LockServer)(BOOL fLock) override {
    if (fLock)
      _pAtlModule->Lock();
    else
      _pAtlModule->Unlock();
    return S_OK;
  }
  STDMETHOD(CreateInstance)(LPUNKNOWN pUnkOuter, REFIID riid, void** ppvObj) override {
    if (ppvObj == NULL) return E_POINTER;
    *ppvObj = NULL;
    if (!license::IsLicenseValid()) return CLASS_E_NOTLICENSED;
    if (pUnkOuter != NULL && !InlineIsEqualGUID(riid, IID_IUnknown)) return CLASS_E_NOAGGREGATION;
    return m_pfnCreateInstance(pUnkOuter, riid, ppvObj);
  }
  STDMETHOD(CreateInstanceLic)(IUnknown* pUnkOuter, IUnknown* /*pUnkReserved*/, REFIID riid,
                               BSTR bstrKey, void** ppvObject) override {
    if (ppvObject == NULL) return E_POINTER;
    *ppvObject = NULL;
    if ((bstrKey != NULL && !license::VerifyLicenseKey(bstrKey)) ||
        (bstrKey == NULL && !license::IsLicenseValid()))
      return CLASS_E_NOTLICENSED;
    if (pUnkOuter != NULL && !InlineIsEqualGUID(riid, IID_IUnknown)) return CLASS_E_NOAGGREGATION;
    return m_pfnCreateInstance(pUnkOuter, riid, ppvObject);
  }
  STDMETHOD(RequestLicKey)(DWORD dwReserved, BSTR* pbstrKey) override {
    if (pbstrKey == NULL) return E_POINTER;
    *pbstrKey = NULL;
    if (!license::IsLicenseValid()) return CLASS_E_NOTLICENSED;
    return license::GetLicenseKey(dwReserved, pbstrKey) ? S_OK : E_FAIL;
  }
  STDMETHOD(GetLicInfo)(LICINFO* pLicInfo) override {
    if (pLicInfo == NULL) return E_POINTER;
    pLicInfo->cbLicInfo = sizeof(LICINFO);
    pLicInfo->fLicVerified = license::IsLicenseValid();
    BSTR bstr = NULL;
    pLicInfo->fRuntimeKeyAvail = license::GetLicenseKey(0, &bstr);
    ::SysFreeString(bstr);
    return S_OK;
  }
  void SetVoid(void* pv) { m_pfnCreateInstance = (_ATL_CREATORFUNC*)pv; }
};

// The license of DECLARE_CLASSFACTORY2's simplest use: a key that is a
// string resource.
class CComLicense {};

#define DECLARE_CLASSFACTORY_EX(cf) \
  typedef ATL::CComCreator<ATL::CComObjectCached<cf> > _ClassFactoryCreatorClass;
#define DECLARE_CLASSFACTORY() DECLARE_CLASSFACTORY_EX(ATL::CComClassFactory)
#define DECLARE_CLASSFACTORY2(lic) DECLARE_CLASSFACTORY_EX(ATL::CComClassFactory2<lic>)
#define DECLARE_CLASSFACTORY_AUTO_THREAD() DECLARE_CLASSFACTORY_EX(ATL::CComClassFactoryAutoThread)
#define DECLARE_CLASSFACTORY_SINGLETON(obj) \
  DECLARE_CLASSFACTORY_EX(ATL::CComClassFactorySingleton<obj>)

// ---------------------------------------------------------------------
// Registration of an object.

#define DECLARE_REGISTRY_RESOURCEID(x)                                     \
  static HRESULT WINAPI UpdateRegistry(BOOL bRegister) {                   \
    return ATL::_pAtlModule->UpdateRegistryFromResource(x, bRegister);     \
  }
#define DECLARE_REGISTRY_RESOURCE(x)                                         \
  static HRESULT WINAPI UpdateRegistry(BOOL bRegister) {                     \
    return ATL::_pAtlModule->UpdateRegistryFromResource(_T(#x), bRegister);  \
  }
#define DECLARE_NO_REGISTRY() \
  static HRESULT WINAPI UpdateRegistry(BOOL) { return S_OK; }
#define DECLARE_REGISTRY(class, pid, vpid, nid, flags)                                          \
  static HRESULT WINAPI UpdateRegistry(BOOL bRegister) {                                        \
    return static_cast<ATL::CComModule*>(ATL::_pAtlModule)                                      \
        ->UpdateRegistryClass(GetObjectCLSID(), pid, vpid, nid, flags, bRegister);              \
  }

// The component categories an object belongs to.
#define BEGIN_CATEGORY_MAP(x)                                 \
  static const ATL::_ATL_CATMAP_ENTRY* GetCategoryMap() {     \
    static const ATL::_ATL_CATMAP_ENTRY pMap[] = {
#define IMPLEMENTED_CATEGORY(catid) {_ATL_CATMAP_ENTRY_IMPLEMENTED, &catid},
#define REQUIRED_CATEGORY(catid) {_ATL_CATMAP_ENTRY_REQUIRED, &catid},
#define END_CATEGORY_MAP()          \
  {_ATL_CATMAP_ENTRY_END, NULL}     \
  }                                 \
  ;                                 \
  return pMap;                      \
  }

// ---------------------------------------------------------------------
// The class of a creatable object: its class factory, how it aggregates,
// its CLSID, and Error to report a failure.

template <class T, const CLSID* pclsid = &_ATL_GUID_NULL>
class CComCoClass {
public:
  DECLARE_CLASSFACTORY()
  DECLARE_AGGREGATABLE(T)
  typedef T _CoClass;

  static const CLSID& WINAPI GetObjectCLSID() { return *pclsid; }
  static LPCTSTR WINAPI GetObjectDescription() { return NULL; }

  static HRESULT WINAPI Error(LPCOLESTR lpszDesc, const IID& iid = _ATL_GUID_NULL,
                              HRESULT hRes = 0) {
    return AtlReportError(GetObjectCLSID(), lpszDesc, iid, hRes);
  }
  static HRESULT WINAPI Error(LPCOLESTR lpszDesc, DWORD dwHelpID, LPCOLESTR lpszHelpFile,
                              const IID& iid = _ATL_GUID_NULL, HRESULT hRes = 0) {
    return AtlReportError(GetObjectCLSID(), lpszDesc, dwHelpID, lpszHelpFile, iid, hRes);
  }
  static HRESULT WINAPI Error(LPCSTR lpszDesc, const IID& iid = _ATL_GUID_NULL,
                              HRESULT hRes = 0) {
    return AtlReportError(GetObjectCLSID(), lpszDesc, iid, hRes);
  }
  static HRESULT WINAPI Error(LPCSTR lpszDesc, DWORD dwHelpID, LPCSTR lpszHelpFile,
                              const IID& iid = _ATL_GUID_NULL, HRESULT hRes = 0) {
    return AtlReportError(GetObjectCLSID(), lpszDesc, dwHelpID, lpszHelpFile, iid, hRes);
  }
  static HRESULT WINAPI Error(UINT nID, const IID& iid = _ATL_GUID_NULL, HRESULT hRes = 0,
                              HINSTANCE hInst = _AtlBaseModule.GetResourceInstance()) {
    return AtlReportError(GetObjectCLSID(), nID, iid, hRes, hInst);
  }
  static HRESULT WINAPI Error(UINT nID, DWORD dwHelpID, LPCOLESTR lpszHelpFile,
                              const IID& iid = _ATL_GUID_NULL, HRESULT hRes = 0,
                              HINSTANCE hInst = _AtlBaseModule.GetResourceInstance()) {
    return AtlReportError(GetObjectCLSID(), nID, dwHelpID, lpszHelpFile, iid, hRes, hInst);
  }

  // A new object of this class, for the program that holds the class.
  template <class Q>
  static HRESULT CreateInstance(IUnknown* punkOuter, Q** pp) {
    return T::_CreatorClass::CreateInstance(punkOuter, __uuidof(Q), (void**)pp);
  }
  template <class Q>
  static HRESULT CreateInstance(Q** pp) {
    return T::_CreatorClass::CreateInstance(NULL, __uuidof(Q), (void**)pp);
  }
};

// ---------------------------------------------------------------------
// The object map of a server: the classes it makes.
//
// ATL 3:  BEGIN_OBJECT_MAP(ObjectMap) OBJECT_ENTRY(CLSID_Thing, CThing) END_OBJECT_MAP()
//         _Module.Init(ObjectMap, hInstance, &LIBID_ThingLib);
// ATL 7:  OBJECT_ENTRY_AUTO(__uuidof(Thing), CThing) beside the class.

#define BEGIN_OBJECT_MAP(x) static ATL::_ATL_OBJMAP_ENTRY x[] = {
#define END_OBJECT_MAP() {NULL, NULL, NULL, NULL, NULL, 0, NULL, NULL, NULL}};
#define OBJECT_ENTRY(clsid, class)                                                         \
  {&clsid, class ::UpdateRegistry, class ::_ClassFactoryCreatorClass::CreateInstance,      \
   class ::_CreatorClass::CreateInstance, NULL, 0, class ::GetObjectDescription,          \
   class ::GetCategoryMap, class ::ObjectMain},
// A class registered but not creatable from outside.
#define OBJECT_ENTRY_NON_CREATEABLE(class)                                                 \
  {&ATL::_ATL_GUID_NULL, class ::UpdateRegistry, NULL, NULL, NULL, 0, NULL, class ::GetCategoryMap, \
   class ::ObjectMain},
#define OBJECT_ENTRY_NON_CREATEABLE_EX(clsid, class)                                    \
  {&clsid, class ::UpdateRegistry, NULL, NULL, NULL, 0, NULL, class ::GetCategoryMap, \
   class ::ObjectMain},

// OBJECT_ENTRY_AUTO adds the class to the module's list as the program
// starts.
template <class T>
struct _AtlAutoObjectEntry {
  _ATL_AUTO_OBJMAP node;
  _AtlAutoObjectEntry(const CLSID* pclsid, _ATL_CREATORFUNC* pfnGetClassObject,
                      _ATL_CREATORFUNC* pfnCreateInstance) {
    node.entry.pclsid = pclsid;
    node.entry.pfnUpdateRegistry = T::UpdateRegistry;
    node.entry.pfnGetClassObject = pfnGetClassObject;
    node.entry.pfnCreateInstance = pfnCreateInstance;
    node.entry.pCF = NULL;
    node.entry.dwRegister = 0;
    node.entry.pfnGetObjectDescription = T::GetObjectDescription;
    node.entry.pfnGetCategoryMap = T::GetCategoryMap;
    node.entry.pfnObjectMain = T::ObjectMain;
    // At the end, so that the map is in the order of the sources.
    node.pNext = NULL;
    _ATL_AUTO_OBJMAP** pp = &_AtlAutoObjectMap;
    while (*pp != NULL) pp = &(*pp)->pNext;
    *pp = &node;
  }
};

#define _ATL_OBJENTRY_CAT2(a, b) a##b
#define _ATL_OBJENTRY_CAT(a, b) _ATL_OBJENTRY_CAT2(a, b)
#define OBJECT_ENTRY_AUTO(clsid, class)                                                      \
  static ATL::_AtlAutoObjectEntry<class> _ATL_OBJENTRY_CAT(__objMap_, __COUNTER__)(          \
      &clsid, class ::_ClassFactoryCreatorClass::CreateInstance,                            \
      class ::_CreatorClass::CreateInstance);
#define OBJECT_ENTRY_NON_CREATEABLE_EX_AUTO(clsid, class) \
  static ATL::_AtlAutoObjectEntry<class> _ATL_OBJENTRY_CAT(__objMap_, __COUNTER__)(&clsid, NULL, NULL);

// ---------------------------------------------------------------------
// Automation: IDispatch over the type library that describes the
// interface.

class CComTypeInfoHolder {
public:
  // Initialised as an aggregate: {&iid, &libid, major, minor}.
  const GUID* m_pguid;
  const GUID* m_plibid;
  WORD m_wMajor;
  WORD m_wMinor;
  ITypeInfo* m_pInfo;
  LONG m_dwRef;

  // The type information, loaded the first time: the registered type
  // library, else the one in the module's resources.
  HRESULT GetTI(LCID lcid) {
    if (m_pInfo != NULL) return S_OK;
    const GUID* plibid = m_plibid;
    if (plibid == NULL || InlineIsEqualGUID(*plibid, _ATL_GUID_NULL)) {
      if (_pAtlModule != NULL && _pAtlModule->m_pLibID != NULL)
        plibid = _pAtlModule->m_pLibID;
      else
        plibid = &CAtlModule::m_libid;
    }
    _pAtlModule->m_csStaticDataInitAndTypeInfo.Lock();
    HRESULT hr = S_OK;
    if (m_pInfo == NULL) {
      ITypeLib* pTypeLib = NULL;
      WORD wMajor = m_wMajor == 0xFFFF ? 1 : m_wMajor;
      WORD wMinor = m_wMinor == 0xFFFF ? 0 : m_wMinor;
      hr = ::LoadRegTypeLib(*plibid, wMajor, wMinor, lcid, &pTypeLib);
      if (FAILED(hr)) {
        BSTR bstrPath = NULL;
        hr = AtlLoadTypeLib(_AtlBaseModule.GetModuleInstance(), NULL, &bstrPath, &pTypeLib);
        ::SysFreeString(bstrPath);
      }
      if (SUCCEEDED(hr)) {
        ITypeInfo* pTypeInfo = NULL;
        hr = pTypeLib->GetTypeInfoOfGuid(*m_pguid, &pTypeInfo);
        if (SUCCEEDED(hr)) {
          // A dual interface's dispatch half calls through IDispatch; its
          // vtable half (impltype -1) calls the methods.
          TYPEATTR* pAttr = NULL;
          if (SUCCEEDED(pTypeInfo->GetTypeAttr(&pAttr))) {
            bool bDual = pAttr->typekind == TKIND_DISPATCH && (pAttr->wTypeFlags & TYPEFLAG_FDUAL);
            pTypeInfo->ReleaseTypeAttr(pAttr);
            HREFTYPE hRef;
            ITypeInfo* pVtbl = NULL;
            if (bDual && SUCCEEDED(pTypeInfo->GetRefTypeOfImplType((UINT)-1, &hRef)) &&
                SUCCEEDED(pTypeInfo->GetRefTypeInfo(hRef, &pVtbl))) {
              pTypeInfo->Release();
              pTypeInfo = pVtbl;
            }
          }
          m_pInfo = pTypeInfo;
          if (_pAtlModule != NULL) _pAtlModule->AddTermFunc(Cleanup, (DWORD_PTR)this);
        }
        pTypeLib->Release();
      }
    }
    _pAtlModule->m_csStaticDataInitAndTypeInfo.Unlock();
    return hr;
  }
  HRESULT EnsureTI(LCID lcid) { return m_pInfo != NULL ? S_OK : GetTI(lcid); }
  HRESULT GetTI(LCID lcid, ITypeInfo** ppInfo) {
    if (ppInfo == NULL) return E_POINTER;
    *ppInfo = NULL;
    HRESULT hr = EnsureTI(lcid);
    if (SUCCEEDED(hr)) {
      *ppInfo = m_pInfo;
      m_pInfo->AddRef();
    }
    return hr;
  }

  HRESULT GetTypeInfo(UINT itinfo, LCID lcid, ITypeInfo** pptinfo) {
    if (pptinfo == NULL) return E_POINTER;
    *pptinfo = NULL;
    if (itinfo != 0) return DISP_E_BADINDEX;
    return GetTI(lcid, pptinfo);
  }
  HRESULT GetIDsOfNames(REFIID, LPOLESTR* rgszNames, UINT cNames, LCID lcid, DISPID* rgdispid) {
    HRESULT hr = EnsureTI(lcid);
    if (FAILED(hr)) return hr;
    return m_pInfo->GetIDsOfNames(rgszNames, cNames, rgdispid);
  }
  HRESULT Invoke(IDispatch* p, DISPID dispidMember, REFIID, LCID lcid, WORD wFlags,
                 DISPPARAMS* pdispparams, VARIANT* pvarResult, EXCEPINFO* pexcepinfo,
                 UINT* puArgErr) {
    HRESULT hr = EnsureTI(lcid);
    if (FAILED(hr)) return hr;
    return m_pInfo->Invoke(p, dispidMember, wFlags, pdispparams, pvarResult, pexcepinfo,
                           puArgErr);
  }

  static void __stdcall Cleanup(DWORD_PTR dw) {
    CComTypeInfoHolder* p = (CComTypeInfoHolder*)dw;
    if (p->m_pInfo != NULL) p->m_pInfo->Release();
    p->m_pInfo = NULL;
  }
};

// IDispatch for a dual interface T, whose methods the type library
// describes.
template <class T, const IID* piid = &__uuidof(T), const GUID* plibid = &CAtlModule::m_libid,
          WORD wMajor = 1, WORD wMinor = 0, class tihclass = CComTypeInfoHolder>
class ATL_NO_VTABLE IDispatchImpl : public T {
public:
  typedef tihclass _tihclass;

  STDMETHOD(GetTypeInfoCount)(UINT* pctinfo) override {
    if (pctinfo == NULL) return E_POINTER;
    *pctinfo = 1;
    return S_OK;
  }
  STDMETHOD(GetTypeInfo)(UINT itinfo, LCID lcid, ITypeInfo** pptinfo) override {
    return _tih.GetTypeInfo(itinfo, lcid, pptinfo);
  }
  STDMETHOD(GetIDsOfNames)(REFIID riid, LPOLESTR* rgszNames, UINT cNames, LCID lcid,
                           DISPID* rgdispid) override {
    return _tih.GetIDsOfNames(riid, rgszNames, cNames, lcid, rgdispid);
  }
  STDMETHOD(Invoke)(DISPID dispidMember, REFIID riid, LCID lcid, WORD wFlags,
                    DISPPARAMS* pdispparams, VARIANT* pvarResult, EXCEPINFO* pexcepinfo,
                    UINT* puArgErr) override {
    return _tih.Invoke((IDispatch*)this, dispidMember, riid, lcid, wFlags, pdispparams,
                       pvarResult, pexcepinfo, puArgErr);
  }

protected:
  static tihclass _tih;
  static HRESULT GetTI(LCID lcid, ITypeInfo** ppInfo) { return _tih.GetTI(lcid, ppInfo); }
};

template <class T, const IID* piid, const GUID* plibid, WORD wMajor, WORD wMinor, class tihclass>
tihclass IDispatchImpl<T, piid, plibid, wMajor, wMinor, tihclass>::_tih = {
    piid, plibid, wMajor, wMinor, NULL, 0};

// ISupportErrorInfo: the interface whose methods leave error objects.
template <const IID* piid>
class ATL_NO_VTABLE ISupportErrorInfoImpl : public ISupportErrorInfo {
public:
  STDMETHOD(InterfaceSupportsErrorInfo)(REFIID riid) override {
    return InlineIsEqualGUID(riid, *piid) ? S_OK : S_FALSE;
  }
};

// IProvideClassInfo2: the coclass's type information and its outgoing
// (event) interface.
template <const CLSID* pcoclsid, const IID* psrcid, const GUID* plibid = &CAtlModule::m_libid,
          WORD wMajor = 1, WORD wMinor = 0, class tihclass = CComTypeInfoHolder>
class ATL_NO_VTABLE IProvideClassInfo2Impl : public IProvideClassInfo2 {
public:
  STDMETHOD(GetClassInfo)(ITypeInfo** pptinfo) override { return _tih.GetTypeInfo(0, LANG_NEUTRAL, pptinfo); }
  STDMETHOD(GetGUID)(DWORD dwGuidKind, GUID* pGUID) override {
    if (pGUID == NULL) return E_POINTER;
    *pGUID = _ATL_GUID_NULL;
    if (dwGuidKind == GUIDKIND_DEFAULT_SOURCE_DISP_IID && psrcid != NULL) {
      *pGUID = *psrcid;
      return S_OK;
    }
    return E_FAIL;
  }

protected:
  static tihclass _tih;
};

template <const CLSID* pcoclsid, const IID* psrcid, const GUID* plibid, WORD wMajor, WORD wMinor,
          class tihclass>
tihclass IProvideClassInfo2Impl<pcoclsid, psrcid, plibid, wMajor, wMinor, tihclass>::_tih = {
    pcoclsid, plibid, wMajor, wMinor, NULL, 0};

// ---------------------------------------------------------------------
// Enumerators.

// How an enumerator copies its items out (copy), and makes (init) and
// frees (destroy) them.
template <class T>
class _Copy {
public:
  static HRESULT copy(T* p1, const T* p2) {
    memcpy((void*)p1, (const void*)p2, sizeof(T));
    return S_OK;
  }
  static void init(T*) {}
  static void destroy(T*) {}
};

template <>
class _Copy<VARIANT> {
public:
  static HRESULT copy(VARIANT* p1, const VARIANT* p2) {
    p1->vt = VT_EMPTY;
    return ::VariantCopy(p1, const_cast<VARIANT*>(p2));
  }
  static void init(VARIANT* p) { p->vt = VT_EMPTY; }
  static void destroy(VARIANT* p) { ::VariantClear(p); }
};

template <>
class _Copy<LPOLESTR> {
public:
  static HRESULT copy(LPOLESTR* p1, const LPOLESTR* p2) {
    if (*p2 == NULL) {
      *p1 = NULL;
      return S_OK;
    }
    size_t n = (wcslen(*p2) + 1) * sizeof(OLECHAR);
    *p1 = (LPOLESTR)::CoTaskMemAlloc(n);
    if (*p1 == NULL) return E_OUTOFMEMORY;
    memcpy(*p1, *p2, n);
    return S_OK;
  }
  static void init(LPOLESTR* p) { *p = NULL; }
  static void destroy(LPOLESTR* p) { ::CoTaskMemFree(*p); }
};

template <>
class _Copy<OLEVERB> {
public:
  static HRESULT copy(OLEVERB* p1, const OLEVERB* p2) {
    *p1 = *p2;
    if (p2->lpszVerbName == NULL) return S_OK;
    size_t n = (wcslen(p2->lpszVerbName) + 1) * sizeof(OLECHAR);
    p1->lpszVerbName = (LPOLESTR)::CoTaskMemAlloc(n);
    if (p1->lpszVerbName == NULL) return E_OUTOFMEMORY;
    memcpy(p1->lpszVerbName, p2->lpszVerbName, n);
    return S_OK;
  }
  static void init(OLEVERB* p) { p->lpszVerbName = NULL; }
  static void destroy(OLEVERB* p) { ::CoTaskMemFree(p->lpszVerbName); }
};

template <>
class _Copy<CONNECTDATA> {
public:
  static HRESULT copy(CONNECTDATA* p1, const CONNECTDATA* p2) {
    *p1 = *p2;
    if (p1->pUnk != NULL) p1->pUnk->AddRef();
    return S_OK;
  }
  static void init(CONNECTDATA*) {}
  static void destroy(CONNECTDATA* p) {
    if (p->pUnk != NULL) p->pUnk->Release();
  }
};

template <class T>
class _CopyInterface {
public:
  static HRESULT copy(T** p1, T* const* p2) {
    *p1 = *p2;
    if (*p1 != NULL) (*p1)->AddRef();
    return S_OK;
  }
  static void init(T**) {}
  static void destroy(T** p) {
    if (*p != NULL) (*p)->Release();
  }
};

enum CComEnumFlags {
  AtlFlagNoCopy = 0,         // the items belong to the caller (or pUnk)
  AtlFlagTakeOwnership = 2,  // the enumerator frees the array (new[])
  AtlFlagCopy = 3            // the enumerator copies the items
};

template <class Base, const IID* piid, class T, class Copy, class ThreadModel = CComObjectThreadModel>
class CComEnum;

template <class Base, const IID* piid, class T, class Copy>
class ATL_NO_VTABLE CComEnumImpl : public Base {
public:
  T* m_begin;
  T* m_end;
  T* m_iter;
  DWORD m_dwFlags;
  CComPtr<IUnknown> m_spUnk;  // what keeps the items alive

  enum FlagBits { BitCopy = 1, BitOwn = 2 };

  CComEnumImpl() : m_begin(NULL), m_end(NULL), m_iter(NULL), m_dwFlags(0) {}
  virtual ~CComEnumImpl() {
    if (m_dwFlags & BitOwn) {
      for (T* p = m_begin; p != m_end; p++) Copy::destroy(p);
      delete[] m_begin;
    }
  }

  HRESULT Init(T* begin, T* end, IUnknown* pUnk, CComEnumFlags flags = AtlFlagNoCopy) {
    if (flags == AtlFlagCopy) {
      ptrdiff_t n = end - begin;
      m_begin = new (std::nothrow) T[n > 0 ? n : 1];
      if (m_begin == NULL) return E_OUTOFMEMORY;
      m_iter = m_begin;
      m_dwFlags = flags;
      for (T* p = begin; p != end; p++) {
        Copy::init(m_iter);
        HRESULT hr = Copy::copy(m_iter, p);
        if (FAILED(hr)) {
          T* pEnd = m_iter;
          for (m_iter = m_begin; m_iter < pEnd; m_iter++) Copy::destroy(m_iter);
          delete[] m_begin;
          m_begin = m_end = m_iter = NULL;
          m_dwFlags = 0;
          return hr;
        }
        m_iter++;
      }
      m_end = m_iter;
    } else {
      m_begin = begin;
      m_end = end;
      m_dwFlags = flags;
    }
    m_iter = m_begin;
    m_spUnk = pUnk;
    return S_OK;
  }

  STDMETHOD(Next)(ULONG celt, T* rgelt, ULONG* pceltFetched) override {
    if (pceltFetched != NULL) *pceltFetched = 0;
    if (rgelt == NULL || (celt > 1 && pceltFetched == NULL)) return E_POINTER;
    if (m_begin == NULL || m_end == NULL || m_iter == NULL) return E_FAIL;
    ULONG nRem = (ULONG)(m_end - m_iter);
    HRESULT hRes = S_OK;
    if (nRem < celt) hRes = S_FALSE;
    ULONG nMin = celt < nRem ? celt : nRem;
    ULONG i;
    for (i = 0; i < nMin; i++) {
      Copy::init(rgelt + i);
      HRESULT hr = Copy::copy(rgelt + i, m_iter);
      if (FAILED(hr)) {
        while (i > 0) Copy::destroy(rgelt + --i);
        return hr;
      }
      m_iter++;
    }
    if (pceltFetched != NULL) *pceltFetched = nMin;
    return hRes;
  }
  STDMETHOD(Skip)(ULONG celt) override {
    if (celt == 0) return E_INVALIDARG;
    ULONG nRem = (ULONG)(m_end - m_iter);
    ULONG nSkip = celt < nRem ? celt : nRem;
    m_iter += nSkip;
    return celt == nSkip ? S_OK : S_FALSE;
  }
  STDMETHOD(Reset)() override {
    m_iter = m_begin;
    return S_OK;
  }
  STDMETHOD(Clone)(Base** ppEnum) override {
    typedef CComObject<CComEnum<Base, piid, T, Copy> > _class;
    if (ppEnum == NULL) return E_POINTER;
    *ppEnum = NULL;
    _class* p = NULL;
    HRESULT hRes = _class::CreateInstance(&p);
    if (FAILED(hRes)) return hRes;
    // The clone reads the same items: those this one owns stay alive with
    // it.
    hRes = p->Init(m_begin, m_end,
                   (m_dwFlags & BitOwn) ? static_cast<IUnknown*>(static_cast<Base*>(this))
                                        : m_spUnk.p);
    if (SUCCEEDED(hRes)) {
      p->m_iter = m_iter;
      hRes = p->_InternalQueryInterface(*piid, (void**)ppEnum);
    }
    if (FAILED(hRes)) delete p;
    return hRes;
  }
};

template <class Base, const IID* piid, class T, class Copy, class ThreadModel>
class ATL_NO_VTABLE CComEnum : public CComEnumImpl<Base, piid, T, Copy>,
                               public CComObjectRootEx<ThreadModel> {
public:
  typedef CComEnum<Base, piid, T, Copy, ThreadModel> _CComEnum;
  typedef CComEnumImpl<Base, piid, T, Copy> _CComEnumBase;
  BEGIN_COM_MAP(_CComEnum)
    COM_INTERFACE_ENTRY_IID(*piid, _CComEnumBase)
  END_COM_MAP()
};


template <class Base, const IID* piid, class T, class Copy, class CollType,
          class ThreadModel = CComObjectThreadModel>
class CComEnumOnSTL;

// Enumerating an STL-style collection that the object pUnk holds.
template <class Base, const IID* piid, class T, class Copy, class CollType>
class ATL_NO_VTABLE IEnumOnSTLImpl : public Base {
public:
  CComPtr<IUnknown> m_spUnk;
  CollType* m_pcollection;
  typename CollType::const_iterator m_iter;

  HRESULT Init(IUnknown* pUnkForRelease, CollType& collection) {
    m_spUnk = pUnkForRelease;
    m_pcollection = &collection;
    m_iter = m_pcollection->begin();
    return S_OK;
  }
  STDMETHOD(Next)(ULONG celt, T* rgelt, ULONG* pceltFetched) override {
    if (pceltFetched != NULL) *pceltFetched = 0;
    if (rgelt == NULL || (celt > 1 && pceltFetched == NULL)) return E_POINTER;
    if (m_pcollection == NULL) return E_FAIL;
    ULONG nActual = 0;
    HRESULT hr = S_OK;
    while (nActual < celt && m_iter != m_pcollection->end()) {
      Copy::init(rgelt + nActual);
      hr = Copy::copy(rgelt + nActual, &*m_iter);
      if (FAILED(hr)) {
        while (nActual > 0) Copy::destroy(rgelt + --nActual);
        return hr;
      }
      ++m_iter;
      nActual++;
    }
    if (pceltFetched != NULL) *pceltFetched = nActual;
    return nActual < celt ? S_FALSE : S_OK;
  }
  STDMETHOD(Skip)(ULONG celt) override {
    ULONG n = 0;
    while (n < celt && m_iter != m_pcollection->end()) {
      ++m_iter;
      n++;
    }
    return n == celt ? S_OK : S_FALSE;
  }
  STDMETHOD(Reset)() override {
    m_iter = m_pcollection->begin();
    return S_OK;
  }
  STDMETHOD(Clone)(Base** ppEnum) override {
    typedef CComObject<CComEnumOnSTL<Base, piid, T, Copy, CollType> > _class;
    if (ppEnum == NULL) return E_POINTER;
    *ppEnum = NULL;
    _class* p = NULL;
    HRESULT hRes = _class::CreateInstance(&p);
    if (FAILED(hRes)) return hRes;
    hRes = p->Init(m_spUnk, *m_pcollection);
    if (SUCCEEDED(hRes)) {
      p->m_iter = m_iter;
      hRes = p->_InternalQueryInterface(*piid, (void**)ppEnum);
    }
    if (FAILED(hRes)) delete p;
    return hRes;
  }
};

template <class Base, const IID* piid, class T, class Copy, class CollType, class ThreadModel>
class ATL_NO_VTABLE CComEnumOnSTL : public IEnumOnSTLImpl<Base, piid, T, Copy, CollType>,
                                    public CComObjectRootEx<ThreadModel> {
public:
  typedef CComEnumOnSTL<Base, piid, T, Copy, CollType, ThreadModel> _CComEnum;
  typedef IEnumOnSTLImpl<Base, piid, T, Copy, CollType> _CComEnumBase;
  BEGIN_COM_MAP(_CComEnum)
    COM_INTERFACE_ENTRY_IID(*piid, _CComEnumBase)
  END_COM_MAP()
};


// ---------------------------------------------------------------------
// Connection points: the outgoing interfaces whose sinks an object calls.
//
//   class CThing : ..., public IConnectionPointContainerImpl<CThing>,
//                       public IConnectionPointImpl<CThing, &DIID__IThingEvents> {
//     BEGIN_COM_MAP(CThing) ... COM_INTERFACE_ENTRY(IConnectionPointContainer) END_COM_MAP()
//     BEGIN_CONNECTION_POINT_MAP(CThing)
//       CONNECTION_POINT_ENTRY(DIID__IThingEvents)
//     END_CONNECTION_POINT_MAP()
//   };

// The sinks of a connection point; a cookie is the place in the array
// plus one.
class CComDynamicUnkArray {
public:
  int m_nSize;
  IUnknown** m_ppUnk;

  CComDynamicUnkArray() : m_nSize(0), m_ppUnk(NULL) {}
  ~CComDynamicUnkArray() { free(m_ppUnk); }

  IUnknown** begin() { return m_ppUnk; }
  IUnknown** end() { return m_ppUnk + m_nSize; }
  int GetSize() const { return m_nSize; }
  IUnknown* GetAt(int nIndex) {
    return nIndex >= 0 && nIndex < m_nSize ? m_ppUnk[nIndex] : NULL;
  }
  DWORD Add(IUnknown* pUnk) {
    for (int i = 0; i < m_nSize; i++) {
      if (m_ppUnk[i] == NULL) {
        m_ppUnk[i] = pUnk;
        return (DWORD)(i + 1);
      }
    }
    int nNew = m_nSize == 0 ? 4 : m_nSize * 2;
    IUnknown** pp = (IUnknown**)realloc(m_ppUnk, nNew * sizeof(IUnknown*));
    if (pp == NULL) return 0;
    m_ppUnk = pp;
    memset(m_ppUnk + m_nSize, 0, (nNew - m_nSize) * sizeof(IUnknown*));
    m_ppUnk[m_nSize] = pUnk;
    DWORD dwCookie = (DWORD)(m_nSize + 1);
    m_nSize = nNew;
    return dwCookie;
  }
  BOOL Remove(DWORD dwCookie) {
    int i = (int)dwCookie - 1;
    if (i < 0 || i >= m_nSize || m_ppUnk[i] == NULL) return FALSE;
    m_ppUnk[i] = NULL;
    return TRUE;
  }
  IUnknown* GetUnknownByCookie(DWORD dwCookie) { return GetAt((int)dwCookie - 1); }
  DWORD CookieOf(IUnknown** pp) { return (DWORD)(pp - m_ppUnk + 1); }
};

// At most nMaxSize sinks (CComUnkArray<1>: a single one).
template <unsigned int nMaxSize>
class CComUnkArray {
public:
  IUnknown* m_arr[nMaxSize];
  CComUnkArray() { memset(m_arr, 0, sizeof(m_arr)); }
  IUnknown** begin() { return &m_arr[0]; }
  IUnknown** end() { return &m_arr[nMaxSize]; }
  int GetSize() const { return (int)nMaxSize; }
  IUnknown* GetAt(int nIndex) { return nIndex >= 0 && nIndex < (int)nMaxSize ? m_arr[nIndex] : NULL; }
  DWORD Add(IUnknown* pUnk) {
    for (unsigned int i = 0; i < nMaxSize; i++) {
      if (m_arr[i] == NULL) {
        m_arr[i] = pUnk;
        return i + 1;
      }
    }
    return 0;
  }
  BOOL Remove(DWORD dwCookie) {
    if (dwCookie == 0 || dwCookie > nMaxSize || m_arr[dwCookie - 1] == NULL) return FALSE;
    m_arr[dwCookie - 1] = NULL;
    return TRUE;
  }
  IUnknown* GetUnknownByCookie(DWORD dwCookie) { return GetAt((int)dwCookie - 1); }
  DWORD CookieOf(IUnknown** pp) { return (DWORD)(pp - m_arr + 1); }
};

typedef CComEnum<IEnumConnections, &IID_IEnumConnections, CONNECTDATA, _Copy<CONNECTDATA> >
    CComEnumConnections;
typedef CComEnum<IEnumConnectionPoints, &IID_IEnumConnectionPoints, IConnectionPoint*,
                 _CopyInterface<IConnectionPoint> >
    CComEnumConnectionPoints;

// The place of a connection point in its object: a base class of its own
// for each outgoing interface, whose vtable starts like IUnknown's.
template <const IID* piid>
class ATL_NO_VTABLE _ICPLocator {
public:
  STDMETHOD(_LocCPQueryInterface)(REFIID riid, void** ppvObject) = 0;
  virtual ULONG STDMETHODCALLTYPE AddRef() = 0;
  virtual ULONG STDMETHODCALLTYPE Release() = 0;
};

struct _ATL_CONNMAP_ENTRY {
  DWORD_PTR dwOffset;
};

// One connection point; its vtable continues _ICPLocator's as
// IConnectionPoint's does IUnknown's, so the object's own AddRef and
// Release serve it.
template <class T, const IID* piid, class CDV = CComDynamicUnkArray>
class ATL_NO_VTABLE IConnectionPointImpl : public _ICPLocator<piid> {
public:
  typedef CComEnum<IEnumConnections, &IID_IEnumConnections, CONNECTDATA, _Copy<CONNECTDATA> >
      CComEnumConnections;
  typedef CDV _CDV;
  CDV m_vec;

  ~IConnectionPointImpl() {
    for (IUnknown** pp = m_vec.begin(); pp < m_vec.end(); pp++)
      if (*pp != NULL) (*pp)->Release();
  }

  STDMETHOD(_LocCPQueryInterface)(REFIID riid, void** ppvObject) override {
    if (ppvObject == NULL) return E_POINTER;
    *ppvObject = NULL;
    if (InlineIsEqualGUID(riid, IID_IConnectionPoint) || InlineIsEqualGUID(riid, IID_IUnknown)) {
      *ppvObject = this;
      this->AddRef();
      return S_OK;
    }
    return E_NOINTERFACE;
  }

  STDMETHOD(GetConnectionInterface)(IID* piid2) {
    if (piid2 == NULL) return E_POINTER;
    *piid2 = *piid;
    return S_OK;
  }
  STDMETHOD(GetConnectionPointContainer)(IConnectionPointContainer** ppCPC) {
    if (ppCPC == NULL) return E_POINTER;
    T* pT = static_cast<T*>(this);
    return pT->GetUnknown()->QueryInterface(IID_IConnectionPointContainer, (void**)ppCPC);
  }
  STDMETHOD(Advise)(IUnknown* pUnkSink, DWORD* pdwCookie) {
    if (pdwCookie == NULL) return E_POINTER;
    *pdwCookie = 0;
    if (pUnkSink == NULL) return E_POINTER;
    T* pT = static_cast<T*>(this);
    IUnknown* p = NULL;
    if (FAILED(pUnkSink->QueryInterface(*piid, (void**)&p))) return CONNECT_E_CANNOTCONNECT;
    pT->Lock();
    *pdwCookie = m_vec.Add(p);
    pT->Unlock();
    if (*pdwCookie == 0) {
      p->Release();
      return CONNECT_E_ADVISELIMIT;
    }
    return S_OK;
  }
  STDMETHOD(Unadvise)(DWORD dwCookie) {
    T* pT = static_cast<T*>(this);
    pT->Lock();
    IUnknown* p = m_vec.GetUnknownByCookie(dwCookie);
    BOOL b = p != NULL && m_vec.Remove(dwCookie);
    pT->Unlock();
    if (!b) return CONNECT_E_NOCONNECTION;
    p->Release();
    return S_OK;
  }
  STDMETHOD(EnumConnections)(IEnumConnections** ppEnum) {
    if (ppEnum == NULL) return E_POINTER;
    *ppEnum = NULL;
    CComObject<CComEnumConnections>* pEnum = NULL;
    HRESULT hr = CComObject<CComEnumConnections>::CreateInstance(&pEnum);
    if (FAILED(hr)) return hr;
    T* pT = static_cast<T*>(this);
    pT->Lock();
    int n = 0;
    for (IUnknown** pp = m_vec.begin(); pp < m_vec.end(); pp++)
      if (*pp != NULL) n++;
    CONNECTDATA* pcd = new (std::nothrow) CONNECTDATA[n > 0 ? n : 1];
    if (pcd == NULL) {
      pT->Unlock();
      delete pEnum;
      return E_OUTOFMEMORY;
    }
    CONNECTDATA* pend = pcd;
    for (IUnknown** pp = m_vec.begin(); pp < m_vec.end(); pp++) {
      if (*pp != NULL) {
        (*pp)->AddRef();
        pend->pUnk = *pp;
        pend->dwCookie = m_vec.CookieOf(pp);
        pend++;
      }
    }
    pT->Unlock();
    // The enumerator owns the array and the references.
    hr = pEnum->Init(pcd, pend, NULL, AtlFlagTakeOwnership);
    if (SUCCEEDED(hr)) hr = pEnum->_InternalQueryInterface(IID_IEnumConnections, (void**)ppEnum);
    if (FAILED(hr)) delete pEnum;
    return hr;
  }
};

#define BEGIN_CONNECTION_POINT_MAP(x)                                          \
  typedef x _atl_conn_classtype;                                               \
  static const ATL::_ATL_CONNMAP_ENTRY* GetConnMap(int* pnEntries) {           \
    static const ATL::_ATL_CONNMAP_ENTRY _entries[] = {
#define CONNECTION_POINT_ENTRY(iid)                                                       \
  {offsetofclass(ATL::_ICPLocator<&iid>, _atl_conn_classtype) -                           \
   offsetofclass(ATL::IConnectionPointContainerImpl<_atl_conn_classtype>,                 \
                 _atl_conn_classtype)},
#define END_CONNECTION_POINT_MAP()                                       \
  {(DWORD_PTR)-1}                                                        \
  }                                                                      \
  ;                                                                      \
  if (pnEntries != NULL)                                                 \
    *pnEntries = (int)(sizeof(_entries) / sizeof(ATL::_ATL_CONNMAP_ENTRY) - 1); \
  return _entries;                                                       \
  }

template <class T>
class ATL_NO_VTABLE IConnectionPointContainerImpl : public IConnectionPointContainer {
public:
  STDMETHOD(EnumConnectionPoints)(IEnumConnectionPoints** ppEnum) override {
    if (ppEnum == NULL) return E_POINTER;
    *ppEnum = NULL;
    int nCPCount = 0;
    const _ATL_CONNMAP_ENTRY* pEntry = T::GetConnMap(&nCPCount);
    IConnectionPoint** ppCP = new (std::nothrow) IConnectionPoint*[nCPCount > 0 ? nCPCount : 1];
    if (ppCP == NULL) return E_OUTOFMEMORY;
    int i = 0;
    for (; pEntry->dwOffset != (DWORD_PTR)-1; pEntry++)
      ppCP[i++] = (IConnectionPoint*)((DWORD_PTR)this + pEntry->dwOffset);
    CComObject<CComEnumConnectionPoints>* pEnum = NULL;
    HRESULT hr = CComObject<CComEnumConnectionPoints>::CreateInstance(&pEnum);
    if (SUCCEEDED(hr)) {
      // The enumerator holds a reference to each, through this object.
      hr = pEnum->Init(ppCP, ppCP + i, NULL, AtlFlagCopy);
      if (SUCCEEDED(hr)) hr = pEnum->_InternalQueryInterface(IID_IEnumConnectionPoints, (void**)ppEnum);
      if (FAILED(hr)) delete pEnum;
    }
    delete[] ppCP;
    return hr;
  }
  STDMETHOD(FindConnectionPoint)(REFIID riid, IConnectionPoint** ppCP) override {
    if (ppCP == NULL) return E_POINTER;
    *ppCP = NULL;
    const _ATL_CONNMAP_ENTRY* pEntry = T::GetConnMap(NULL);
    for (; pEntry->dwOffset != (DWORD_PTR)-1; pEntry++) {
      IConnectionPoint* pCP = (IConnectionPoint*)((DWORD_PTR)this + pEntry->dwOffset);
      IID iid;
      if (SUCCEEDED(pCP->GetConnectionInterface(&iid)) && InlineIsEqualGUID(riid, iid)) {
        pCP->AddRef();
        *ppCP = pCP;
        return S_OK;
      }
    }
    return CONNECT_E_NOCONNECTION;
  }
};

// ---------------------------------------------------------------------
// The other small implementations an object may take.

// IObjectWithSite: the site its container gives it.
template <class T>
class ATL_NO_VTABLE IObjectWithSiteImpl : public IObjectWithSite {
public:
  CComPtr<IUnknown> m_spUnkSite;
  STDMETHOD(SetSite)(IUnknown* pUnkSite) override {
    m_spUnkSite = pUnkSite;
    return S_OK;
  }
  STDMETHOD(GetSite)(REFIID riid, void** ppvSite) override {
    if (ppvSite == NULL) return E_POINTER;
    *ppvSite = NULL;
    if (m_spUnkSite == NULL) return E_FAIL;
    return m_spUnkSite->QueryInterface(riid, ppvSite);
  }
};

// IObjectSafety: the object says it is safe for scripting and data.
template <class T, DWORD dwSupportedSafety>
class ATL_NO_VTABLE IObjectSafetyImpl : public IObjectSafety {
public:
  DWORD m_dwCurrentSafety;
  IObjectSafetyImpl() : m_dwCurrentSafety(0) {}
  STDMETHOD(GetInterfaceSafetyOptions)(REFIID riid, DWORD* pdwSupportedOptions,
                                       DWORD* pdwEnabledOptions) override {
    if (pdwSupportedOptions == NULL || pdwEnabledOptions == NULL) return E_POINTER;
    T* pT = static_cast<T*>(this);
    IUnknown* pUnk = NULL;
    HRESULT hr = pT->GetUnknown()->QueryInterface(riid, (void**)&pUnk);
    if (FAILED(hr)) {
      *pdwSupportedOptions = *pdwEnabledOptions = 0;
      return hr;
    }
    pUnk->Release();
    *pdwSupportedOptions = dwSupportedSafety;
    *pdwEnabledOptions = m_dwCurrentSafety;
    return S_OK;
  }
  STDMETHOD(SetInterfaceSafetyOptions)(REFIID riid, DWORD dwOptionSetMask,
                                       DWORD dwEnabledOptions) override {
    T* pT = static_cast<T*>(this);
    IUnknown* pUnk = NULL;
    if (FAILED(pT->GetUnknown()->QueryInterface(riid, (void**)&pUnk))) return E_NOINTERFACE;
    pUnk->Release();
    if (dwOptionSetMask & ~dwSupportedSafety) return E_FAIL;
    m_dwCurrentSafety = (m_dwCurrentSafety & ~dwOptionSetMask) | (dwOptionSetMask & dwEnabledOptions);
    return S_OK;
  }
};

}  // namespace ATL

#endif  // __ATLCOM_H__
