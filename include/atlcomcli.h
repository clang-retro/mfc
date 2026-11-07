// atlcomcli.h - the ATL COM helpers MFC's own classes show in their
// interfaces: CComPtr and CComQIPtr (interface pointers that release
// themselves), CComBSTR and CComVariant.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#ifndef __ATLCOMCLI_H__
#define __ATLCOMCLI_H__

#include <ole2.h>
#include <oleauto.h>

namespace ATL {

template <class T>
class CComPtr {
public:
  T* p;

  CComPtr() : p(NULL) {}
  CComPtr(T* lp) : p(lp) {
    if (p != NULL) p->AddRef();
  }
  CComPtr(const CComPtr<T>& lp) : p(lp.p) {
    if (p != NULL) p->AddRef();
  }
  ~CComPtr() {
    if (p != NULL) p->Release();
  }

  T* operator=(T* lp) {
    if (lp != NULL) lp->AddRef();
    if (p != NULL) p->Release();
    p = lp;
    return p;
  }
  T* operator=(const CComPtr<T>& lp) { return *this = lp.p; }

  operator T*() const { return p; }
  T& operator*() const { return *p; }
  // The address, to receive a pointer; the pointer must be NULL.
  T** operator&() { return &p; }
  T* operator->() const { return p; }
  bool operator!() const { return p == NULL; }
  bool operator==(T* pT) const { return p == pT; }
  bool operator!=(T* pT) const { return p != pT; }

  void Release() {
    T* pTemp = p;
    if (pTemp != NULL) {
      p = NULL;
      pTemp->Release();
    }
  }
  void Attach(T* p2) {
    if (p != NULL) p->Release();
    p = p2;
  }
  T* Detach() {
    T* pt = p;
    p = NULL;
    return pt;
  }
  HRESULT CopyTo(T** ppT) {
    if (ppT == NULL) return E_POINTER;
    *ppT = p;
    if (p != NULL) p->AddRef();
    return S_OK;
  }
  template <class Q>
  HRESULT QueryInterface(Q** pp) const {
    return p->QueryInterface(__uuidof(Q), (void**)pp);
  }
  HRESULT CoCreateInstance(REFCLSID rclsid, LPUNKNOWN pUnkOuter = NULL,
                           DWORD dwClsContext = CLSCTX_ALL) {
    return ::CoCreateInstance(rclsid, pUnkOuter, dwClsContext, __uuidof(T), (void**)&p);
  }
  // Two pointers name the same object when their IUnknowns match.
  bool IsEqualObject(IUnknown* pOther) {
    if (p == NULL && pOther == NULL) return true;
    if (p == NULL || pOther == NULL) return false;
    IUnknown* pUnk1 = NULL;
    IUnknown* pUnk2 = NULL;
    p->QueryInterface(IID_IUnknown, (void**)&pUnk1);
    pOther->QueryInterface(IID_IUnknown, (void**)&pUnk2);
    bool b = pUnk1 == pUnk2;
    if (pUnk1 != NULL) pUnk1->Release();
    if (pUnk2 != NULL) pUnk2->Release();
    return b;
  }
};

// A CComPtr that asks for its interface when it is given another.
template <class T, const IID* piid = &__uuidof(T)>
class CComQIPtr : public CComPtr<T> {
public:
  CComQIPtr() {}
  CComQIPtr(T* lp) : CComPtr<T>(lp) {}
  CComQIPtr(const CComQIPtr<T, piid>& lp) : CComPtr<T>(lp.p) {}
  CComQIPtr(IUnknown* lp) {
    if (lp != NULL) lp->QueryInterface(*piid, (void**)&this->p);
  }
  T* operator=(T* lp) { return CComPtr<T>::operator=(lp); }
  T* operator=(const CComQIPtr<T, piid>& lp) { return CComPtr<T>::operator=(lp.p); }
  T* operator=(IUnknown* lp) {
    T* pNew = NULL;
    if (lp != NULL) lp->QueryInterface(*piid, (void**)&pNew);
    if (this->p != NULL) this->p->Release();
    this->p = pNew;
    return this->p;
  }
};

class CComBSTR {
public:
  BSTR m_str;

  CComBSTR() : m_str(NULL) {}
  CComBSTR(LPCOLESTR pSrc) : m_str(pSrc != NULL ? ::SysAllocString(pSrc) : NULL) {}
  CComBSTR(int nSize, LPCOLESTR sz) : m_str(::SysAllocStringLen(sz, (UINT)nSize)) {}
  CComBSTR(LPCSTR pSrc) : m_str(NULL) {
    if (pSrc == NULL) return;
    int n = ::MultiByteToWideChar(CP_ACP, 0, pSrc, -1, NULL, 0);
    m_str = ::SysAllocStringLen(NULL, (UINT)(n > 0 ? n - 1 : 0));
    if (m_str != NULL && n > 0) ::MultiByteToWideChar(CP_ACP, 0, pSrc, -1, m_str, n);
  }
  CComBSTR(const CComBSTR& src) : m_str(src.Copy()) {}
  ~CComBSTR() { ::SysFreeString(m_str); }

  CComBSTR& operator=(const CComBSTR& src) {
    if (m_str != src.m_str) {
      ::SysFreeString(m_str);
      m_str = src.Copy();
    }
    return *this;
  }
  CComBSTR& operator=(LPCOLESTR pSrc) {
    if (pSrc != m_str) {
      ::SysFreeString(m_str);
      m_str = pSrc != NULL ? ::SysAllocString(pSrc) : NULL;
    }
    return *this;
  }
  operator BSTR() const { return m_str; }
  BSTR* operator&() { return &m_str; }
  bool operator!() const { return m_str == NULL; }

  unsigned int Length() const { return ::SysStringLen(m_str); }
  BSTR Copy() const {
    return m_str != NULL ? ::SysAllocStringLen(m_str, ::SysStringLen(m_str)) : NULL;
  }
  HRESULT CopyTo(BSTR* pbstr) const {
    if (pbstr == NULL) return E_POINTER;
    *pbstr = Copy();
    return (*pbstr == NULL && m_str != NULL) ? E_OUTOFMEMORY : S_OK;
  }
  void Attach(BSTR src) {
    if (m_str != src) {
      ::SysFreeString(m_str);
      m_str = src;
    }
  }
  BSTR Detach() {
    BSTR s = m_str;
    m_str = NULL;
    return s;
  }
  void Empty() {
    ::SysFreeString(m_str);
    m_str = NULL;
  }
  HRESULT Append(LPCOLESTR lpsz, int nLen) {
    if (lpsz == NULL || nLen == 0) return S_OK;
    UINT n1 = Length();
    BSTR b = ::SysAllocStringLen(NULL, n1 + (UINT)nLen);
    if (b == NULL) return E_OUTOFMEMORY;
    if (n1 != 0) memcpy(b, m_str, n1 * sizeof(OLECHAR));
    memcpy(b + n1, lpsz, (size_t)nLen * sizeof(OLECHAR));
    b[n1 + (UINT)nLen] = 0;
    ::SysFreeString(m_str);
    m_str = b;
    return S_OK;
  }
  HRESULT Append(LPCOLESTR lpsz) { return lpsz != NULL ? Append(lpsz, (int)wcslen(lpsz)) : S_OK; }
  HRESULT Append(const CComBSTR& bstrSrc) { return Append(bstrSrc.m_str, (int)bstrSrc.Length()); }
  CComBSTR& operator+=(LPCOLESTR lpsz) {
    Append(lpsz);
    return *this;
  }
  CComBSTR& operator+=(const CComBSTR& bstrSrc) {
    Append(bstrSrc);
    return *this;
  }
  bool operator==(LPCOLESTR lpsz) const {
    if (m_str == NULL || lpsz == NULL) return (m_str == NULL || *m_str == 0) && (lpsz == NULL || *lpsz == 0);
    return wcscmp(m_str, lpsz) == 0;
  }
  bool operator!=(LPCOLESTR lpsz) const { return !(*this == lpsz); }
};

class CComVariant : public tagVARIANT {
public:
  CComVariant() { ::VariantInit(this); }
  CComVariant(const VARIANT& varSrc) {
    ::VariantInit(this);
    ::VariantCopy(this, const_cast<VARIANT*>(&varSrc));
  }
  CComVariant(const CComVariant& varSrc) {
    ::VariantInit(this);
    ::VariantCopy(this, const_cast<CComVariant*>(&varSrc));
  }
  CComVariant(LPCOLESTR lpszSrc) {
    vt = VT_BSTR;
    bstrVal = ::SysAllocString(lpszSrc);
  }
  CComVariant(bool bSrc) {
    vt = VT_BOOL;
    boolVal = bSrc ? VARIANT_TRUE : VARIANT_FALSE;
  }
  CComVariant(int nSrc) {
    vt = VT_I4;
    lVal = nSrc;
  }
  CComVariant(long nSrc) {
    vt = VT_I4;
    lVal = (LONG)nSrc;
  }
  CComVariant(short nSrc) {
    vt = VT_I2;
    iVal = nSrc;
  }
  CComVariant(double dblSrc) {
    vt = VT_R8;
    dblVal = dblSrc;
  }
  CComVariant(IDispatch* pSrc) {
    vt = VT_DISPATCH;
    pdispVal = pSrc;
    if (pSrc != NULL) pSrc->AddRef();
  }
  ~CComVariant() { ::VariantClear(this); }

  CComVariant& operator=(const CComVariant& varSrc) {
    if (this != &varSrc) ::VariantCopy(this, const_cast<CComVariant*>(&varSrc));
    return *this;
  }
  CComVariant& operator=(const VARIANT& varSrc) {
    if (this != &varSrc) ::VariantCopy(this, const_cast<VARIANT*>(&varSrc));
    return *this;
  }

  HRESULT Clear() { return ::VariantClear(this); }
  HRESULT Copy(const VARIANT* pSrc) { return ::VariantCopy(this, const_cast<VARIANT*>(pSrc)); }
  HRESULT Detach(VARIANT* pDest) {
    HRESULT hr = ::VariantClear(pDest);
    if (SUCCEEDED(hr)) {
      *pDest = *this;
      vt = VT_EMPTY;
    }
    return hr;
  }
  HRESULT ChangeType(VARTYPE vtNew, const VARIANT* pSrc = NULL) {
    VARIANT* pVar = const_cast<VARIANT*>(pSrc);
    if (pVar == NULL) pVar = this;
    return ::VariantChangeType(this, pVar, 0, vtNew);
  }
};

}  // namespace ATL

#ifndef _ATL_NO_AUTOMATIC_NAMESPACE
using namespace ATL;
#endif

#endif  // __ATLCOMCLI_H__
