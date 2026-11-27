// comip.h - the compiler COM support smart pointer: _com_ptr_t over an
// interface and its IID (_com_IIID), as _COM_SMARTPTR_TYPEDEF makes them.
// Assigning another interface's pointer queries for this one; failures
// other than E_NOINTERFACE, and using a null pointer, raise _com_error.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#ifndef _INC_COMIP
#define _INC_COMIP

#include <windows.h>
#include <ole2.h>
#include <comutil.h>

#ifdef __cplusplus

template <typename _Interface, const IID* _IID>
class _com_IIID {
public:
  typedef _Interface Interface;
  static _Interface* GetInterfacePtr() throw() { return NULL; }
  static _Interface& GetInterface() throw() { return *GetInterfacePtr(); }
  static const IID& GetIID() throw() { return *_IID; }
};

// The IID from a function, for an IID with no address usable as a
// template argument.
template <typename _Interface, const IID& (*iid_getter)()>
class _com_IIID_getter {
public:
  typedef _Interface Interface;
  static _Interface* GetInterfacePtr() throw() { return NULL; }
  static _Interface& GetInterface() throw() { return *GetInterfacePtr(); }
  static const IID& GetIID() throw() { return iid_getter(); }
};

template <typename _IIID>
class _com_ptr_t {
public:
  typedef _IIID ThisIIID;
  typedef typename _IIID::Interface Interface;

  static const IID& GetIID() throw() { return ThisIIID::GetIID(); }

  _com_ptr_t() throw() : m_pInterface(NULL) {}
  _com_ptr_t(decltype(nullptr)) throw() : m_pInterface(NULL) {}
  // Only 0 (NULL) is a pointer.
  _com_ptr_t(int null) : m_pInterface(NULL) {
    if (null != 0) _com_issue_error(E_POINTER);
  }
  _com_ptr_t(long null) : m_pInterface(NULL) {
    if (null != 0) _com_issue_error(E_POINTER);
  }
  _com_ptr_t(const _com_ptr_t& cp) throw() : m_pInterface(cp.m_pInterface) { _AddRef(); }
  _com_ptr_t(Interface* pInterface) throw() : m_pInterface(pInterface) { _AddRef(); }
  _com_ptr_t(Interface* pInterface, bool fAddRef) throw() : m_pInterface(pInterface) {
    if (fAddRef) _AddRef();
  }
  // Another interface's smart pointer or pointer: queried for this one.
  template <typename _OtherIID>
  _com_ptr_t(const _com_ptr_t<_OtherIID>& p) : m_pInterface(NULL) {
    HRESULT hr = _QueryInterface(p.GetInterfacePtr());
    if (FAILED(hr) && hr != E_NOINTERFACE) _com_issue_error(hr);
  }
  template <typename _InterfaceType>
  _com_ptr_t(_InterfaceType* p) : m_pInterface(NULL) {
    HRESULT hr = _QueryInterface(p);
    if (FAILED(hr) && hr != E_NOINTERFACE) _com_issue_error(hr);
  }
  // A smart pointer's address: what it points to, with a reference.
  explicit _com_ptr_t(_com_ptr_t* p) : m_pInterface(NULL) {
    if (p == NULL) _com_issue_error(E_POINTER);
    m_pInterface = p->m_pInterface;
    _AddRef();
  }
  // The interface of a variant's object (VT_DISPATCH or VT_UNKNOWN, or
  // converted to one).
  _com_ptr_t(const _variant_t& varSrc) : m_pInterface(NULL) {
    HRESULT hr = QueryStdInterfaces(varSrc);
    if (FAILED(hr) && hr != E_NOINTERFACE) _com_issue_error(hr);
  }
  // A new object of a class, by CLSID, by "{...}" string or ProgID.
  explicit _com_ptr_t(const CLSID& clsid, IUnknown* pOuter = NULL, DWORD dwClsContext = CLSCTX_ALL)
      : m_pInterface(NULL) {
    HRESULT hr = CreateInstance(clsid, pOuter, dwClsContext);
    if (FAILED(hr) && hr != E_NOINTERFACE) _com_issue_error(hr);
  }
  explicit _com_ptr_t(LPCWSTR str, IUnknown* pOuter = NULL, DWORD dwClsContext = CLSCTX_ALL)
      : m_pInterface(NULL) {
    HRESULT hr = CreateInstance(str, pOuter, dwClsContext);
    if (FAILED(hr) && hr != E_NOINTERFACE) _com_issue_error(hr);
  }
  explicit _com_ptr_t(LPCSTR str, IUnknown* pOuter = NULL, DWORD dwClsContext = CLSCTX_ALL)
      : m_pInterface(NULL) {
    HRESULT hr = CreateInstance(str, pOuter, dwClsContext);
    if (FAILED(hr) && hr != E_NOINTERFACE) _com_issue_error(hr);
  }
  _com_ptr_t(LPSTR str) : m_pInterface(NULL) {
    HRESULT hr = CreateInstance((LPCSTR)str);
    if (FAILED(hr) && hr != E_NOINTERFACE) _com_issue_error(hr);
  }
  _com_ptr_t(LPWSTR str) : m_pInterface(NULL) {
    HRESULT hr = CreateInstance((LPCWSTR)str);
    if (FAILED(hr) && hr != E_NOINTERFACE) _com_issue_error(hr);
  }
  ~_com_ptr_t() throw() { _Release(); }

  template <typename _OtherIID>
  _com_ptr_t& operator=(const _com_ptr_t<_OtherIID>& p) {
    HRESULT hr = _QueryInterface(p.GetInterfacePtr());
    if (FAILED(hr) && hr != E_NOINTERFACE) _com_issue_error(hr);
    return *this;
  }
  template <typename _InterfaceType>
  _com_ptr_t& operator=(_InterfaceType* p) {
    HRESULT hr = _QueryInterface(p);
    if (FAILED(hr) && hr != E_NOINTERFACE) _com_issue_error(hr);
    return *this;
  }
  _com_ptr_t& operator=(Interface* pInterface) throw() {
    if (m_pInterface != pInterface) {
      Interface* pOld = m_pInterface;
      m_pInterface = pInterface;
      _AddRef();
      if (pOld != NULL) pOld->Release();
    }
    return *this;
  }
  _com_ptr_t& operator=(const _com_ptr_t& cp) throw() { return operator=(cp.m_pInterface); }
  _com_ptr_t& operator=(decltype(nullptr)) throw() {
    _Release();
    m_pInterface = NULL;
    return *this;
  }
  _com_ptr_t& operator=(int null) {
    if (null != 0) _com_issue_error(E_POINTER);
    return operator=(static_cast<Interface*>(NULL));
  }
  _com_ptr_t& operator=(long null) {
    if (null != 0) _com_issue_error(E_POINTER);
    return operator=(static_cast<Interface*>(NULL));
  }
  _com_ptr_t& operator=(const _variant_t& varSrc) {
    HRESULT hr = QueryStdInterfaces(varSrc);
    if (FAILED(hr) && hr != E_NOINTERFACE) _com_issue_error(hr);
    return *this;
  }

  // Takes the pointer without a reference (or with one, fAddRef true).
  void Attach(Interface* pInterface) throw() {
    _Release();
    m_pInterface = pInterface;
  }
  void Attach(Interface* pInterface, bool fAddRef) throw() {
    _Release();
    m_pInterface = pInterface;
    if (fAddRef) {
      if (pInterface == NULL) _com_issue_error(E_POINTER);
      _AddRef();
    }
  }
  Interface* Detach() throw() {
    Interface* p = m_pInterface;
    m_pInterface = NULL;
    return p;
  }

  operator Interface*() const throw() { return m_pInterface; }
  operator Interface&() const {
    if (m_pInterface == NULL) _com_issue_error(E_POINTER);
    return *m_pInterface;
  }
  Interface& operator*() const {
    if (m_pInterface == NULL) _com_issue_error(E_POINTER);
    return *m_pInterface;
  }
  // The address for an [out] pointer: what it held is released.
  Interface** operator&() throw() {
    _Release();
    m_pInterface = NULL;
    return &m_pInterface;
  }
  Interface* operator->() const {
    if (m_pInterface == NULL) _com_issue_error(E_POINTER);
    return m_pInterface;
  }
  operator bool() const throw() { return m_pInterface != NULL; }

  // Comparisons are of the objects' IUnknown.
  template <typename _OtherIID>
  bool operator==(const _com_ptr_t<_OtherIID>& p) const {
    return _CompareUnknown(p.GetInterfacePtr()) == 0;
  }
  template <typename _InterfaceType>
  bool operator==(_InterfaceType* p) const {
    return _CompareUnknown(p) == 0;
  }
  bool operator==(Interface* p) const { return m_pInterface == p ? true : _CompareUnknown(p) == 0; }
  bool operator==(const _com_ptr_t& p) const { return operator==(p.m_pInterface); }
  bool operator==(decltype(nullptr)) const throw() { return m_pInterface == NULL; }
  bool operator==(int null) const {
    if (null != 0) _com_issue_error(E_POINTER);
    return m_pInterface == NULL;
  }
  bool operator==(long null) const {
    if (null != 0) _com_issue_error(E_POINTER);
    return m_pInterface == NULL;
  }
  template <typename _OtherIID>
  bool operator!=(const _com_ptr_t<_OtherIID>& p) const {
    return !operator==(p);
  }
  template <typename _InterfaceType>
  bool operator!=(_InterfaceType* p) const {
    return !operator==(p);
  }
  bool operator!=(Interface* p) const { return !operator==(p); }
  bool operator!=(const _com_ptr_t& p) const { return !operator==(p); }
  bool operator!=(decltype(nullptr)) const throw() { return m_pInterface != NULL; }
  bool operator!=(int null) const { return !operator==(null); }
  bool operator!=(long null) const { return !operator==(null); }
  template <typename _OtherIID>
  bool operator<(const _com_ptr_t<_OtherIID>& p) const {
    return _CompareUnknown(p.GetInterfacePtr()) < 0;
  }
  template <typename _InterfaceType>
  bool operator<(_InterfaceType* p) const {
    return _CompareUnknown(p) < 0;
  }
  template <typename _OtherIID>
  bool operator>(const _com_ptr_t<_OtherIID>& p) const {
    return _CompareUnknown(p.GetInterfacePtr()) > 0;
  }
  template <typename _InterfaceType>
  bool operator>(_InterfaceType* p) const {
    return _CompareUnknown(p) > 0;
  }
  template <typename _OtherIID>
  bool operator<=(const _com_ptr_t<_OtherIID>& p) const {
    return _CompareUnknown(p.GetInterfacePtr()) <= 0;
  }
  template <typename _InterfaceType>
  bool operator<=(_InterfaceType* p) const {
    return _CompareUnknown(p) <= 0;
  }
  template <typename _OtherIID>
  bool operator>=(const _com_ptr_t<_OtherIID>& p) const {
    return _CompareUnknown(p.GetInterfacePtr()) >= 0;
  }
  template <typename _InterfaceType>
  bool operator>=(_InterfaceType* p) const {
    return _CompareUnknown(p) >= 0;
  }

  void Release() {
    if (m_pInterface == NULL) _com_issue_error(E_POINTER);
    m_pInterface->Release();
    m_pInterface = NULL;
  }
  void AddRef() {
    if (m_pInterface == NULL) _com_issue_error(E_POINTER);
    m_pInterface->AddRef();
  }
  Interface* GetInterfacePtr() const throw() { return m_pInterface; }
  Interface*& GetInterfacePtr() throw() { return m_pInterface; }

  // An out-of-process server is started (OleRun) before the query, as
  // its class may not give out this interface until running.
  HRESULT CreateInstance(const CLSID& rclsid, IUnknown* pOuter = NULL, DWORD dwClsContext = CLSCTX_ALL) throw() {
    _Release();
    m_pInterface = NULL;
    HRESULT hr;
    if (dwClsContext & (CLSCTX_LOCAL_SERVER | CLSCTX_REMOTE_SERVER)) {
      IUnknown* pUnk = NULL;
      hr = ::CoCreateInstance(rclsid, pOuter, dwClsContext, IID_IUnknown, (void**)&pUnk);
      if (SUCCEEDED(hr)) {
        hr = ::OleRun(pUnk);
        if (SUCCEEDED(hr)) hr = pUnk->QueryInterface(GetIID(), (void**)&m_pInterface);
        pUnk->Release();
      }
    } else {
      hr = ::CoCreateInstance(rclsid, pOuter, dwClsContext, GetIID(), (void**)&m_pInterface);
    }
    if (FAILED(hr)) m_pInterface = NULL;
    return hr;
  }
  HRESULT CreateInstance(LPCWSTR clsidString, IUnknown* pOuter = NULL, DWORD dwClsContext = CLSCTX_ALL) throw() {
    CLSID clsid;
    HRESULT hr = _ClsidFrom(clsidString, &clsid);
    if (FAILED(hr)) return hr;
    return CreateInstance(clsid, pOuter, dwClsContext);
  }
  HRESULT CreateInstance(LPCSTR clsidStringA, IUnknown* pOuter = NULL, DWORD dwClsContext = CLSCTX_ALL) throw() {
    if (clsidStringA == NULL) return E_INVALIDARG;
    BSTR b = _com_util::ConvertStringToBSTR(clsidStringA);
    if (b == NULL) return E_OUTOFMEMORY;
    HRESULT hr = CreateInstance((LPCWSTR)b, pOuter, dwClsContext);
    ::SysFreeString(b);
    return hr;
  }
  // The running object of a class, from the running object table.
  HRESULT GetActiveObject(const CLSID& rclsid) throw() {
    _Release();
    m_pInterface = NULL;
    IUnknown* pUnk = NULL;
    HRESULT hr = ::GetActiveObject(rclsid, NULL, &pUnk);
    if (SUCCEEDED(hr)) {
      hr = pUnk->QueryInterface(GetIID(), (void**)&m_pInterface);
      pUnk->Release();
    }
    if (FAILED(hr)) m_pInterface = NULL;
    return hr;
  }
  HRESULT GetActiveObject(LPCWSTR clsidString) throw() {
    CLSID clsid;
    HRESULT hr = _ClsidFrom(clsidString, &clsid);
    if (FAILED(hr)) return hr;
    return GetActiveObject(clsid);
  }
  HRESULT GetActiveObject(LPCSTR clsidStringA) throw() {
    if (clsidStringA == NULL) return E_INVALIDARG;
    BSTR b = _com_util::ConvertStringToBSTR(clsidStringA);
    if (b == NULL) return E_OUTOFMEMORY;
    HRESULT hr = GetActiveObject((LPCWSTR)b);
    ::SysFreeString(b);
    return hr;
  }

  template <typename _InterfaceType>
  HRESULT QueryInterface(const IID& iid, _InterfaceType*& p) throw() {
    if (m_pInterface == NULL) return E_POINTER;
    return m_pInterface->QueryInterface(iid, reinterpret_cast<void**>(&p));
  }
  template <typename _InterfaceType>
  HRESULT QueryInterface(const IID& iid, _InterfaceType** p) throw() {
    return QueryInterface(iid, *p);
  }

private:
  Interface* m_pInterface;

  void _Release() throw() {
    if (m_pInterface != NULL) m_pInterface->Release();
  }
  void _AddRef() throw() {
    if (m_pInterface != NULL) m_pInterface->AddRef();
  }
  template <typename _InterfacePtr>
  HRESULT _QueryInterface(_InterfacePtr p) throw() {
    Interface* pNew = NULL;
    HRESULT hr = E_NOINTERFACE;
    if (p != NULL) {
      hr = p->QueryInterface(GetIID(), (void**)&pNew);
      if (FAILED(hr)) pNew = NULL;
    }
    _Release();
    m_pInterface = pNew;
    return p != NULL ? hr : S_OK;
  }
  template <typename _InterfacePtr>
  int _CompareUnknown(_InterfacePtr p) const {
    IUnknown* pu1 = NULL;
    IUnknown* pu2 = NULL;
    if (m_pInterface != NULL) {
      HRESULT hr = m_pInterface->QueryInterface(IID_IUnknown, (void**)&pu1);
      if (FAILED(hr)) _com_issue_error(hr);
      pu1->Release();
    }
    if (p != NULL) {
      HRESULT hr = p->QueryInterface(IID_IUnknown, (void**)&pu2);
      if (FAILED(hr)) _com_issue_error(hr);
      pu2->Release();
    }
    return pu1 == pu2 ? 0 : (pu1 < pu2 ? -1 : 1);
  }
  static HRESULT _ClsidFrom(LPCWSTR str, CLSID* pclsid) {
    if (str == NULL) return E_INVALIDARG;
    return str[0] == L'{' ? ::CLSIDFromString(str, pclsid) : ::CLSIDFromProgID(str, pclsid);
  }
  HRESULT QueryStdInterfaces(const _variant_t& varSrc) throw() {
    if (varSrc.vt == VT_DISPATCH) return _QueryInterface(varSrc.pdispVal);
    if (varSrc.vt == VT_UNKNOWN) return _QueryInterface(varSrc.punkVal);
    VARIANT v;
    ::VariantInit(&v);
    HRESULT hr = ::VariantChangeType(&v, const_cast<_variant_t*>(&varSrc), 0, VT_DISPATCH);
    if (SUCCEEDED(hr)) {
      hr = _QueryInterface(v.pdispVal);
    } else {
      hr = ::VariantChangeType(&v, const_cast<_variant_t*>(&varSrc), 0, VT_UNKNOWN);
      if (SUCCEEDED(hr)) hr = _QueryInterface(v.punkVal);
    }
    ::VariantClear(&v);
    return hr;
  }
};

// The pointer or NULL on the left.
template <typename _IIID>
bool operator==(int null, const _com_ptr_t<_IIID>& p) {
  return p == null;
}
template <typename _IIID>
bool operator!=(int null, const _com_ptr_t<_IIID>& p) {
  return p != null;
}
template <typename _IIID>
bool operator==(decltype(nullptr), const _com_ptr_t<_IIID>& p) {
  return p == nullptr;
}
template <typename _IIID>
bool operator!=(decltype(nullptr), const _com_ptr_t<_IIID>& p) {
  return p != nullptr;
}
template <typename _Interface, typename _IIID>
bool operator==(_Interface* i, const _com_ptr_t<_IIID>& p) {
  return p == i;
}
template <typename _Interface, typename _IIID>
bool operator!=(_Interface* i, const _com_ptr_t<_IIID>& p) {
  return p != i;
}
template <typename _Interface, typename _IIID>
bool operator<(_Interface* i, const _com_ptr_t<_IIID>& p) {
  return p > i;
}
template <typename _Interface, typename _IIID>
bool operator>(_Interface* i, const _com_ptr_t<_IIID>& p) {
  return p < i;
}
template <typename _Interface, typename _IIID>
bool operator<=(_Interface* i, const _com_ptr_t<_IIID>& p) {
  return p >= i;
}
template <typename _Interface, typename _IIID>
bool operator>=(_Interface* i, const _com_ptr_t<_IIID>& p) {
  return p <= i;
}

#endif  // __cplusplus
#endif  // _INC_COMIP
