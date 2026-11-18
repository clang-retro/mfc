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

// CComQIPtr<IUnknown>: any pointer is already an IUnknown.
template <>
class CComQIPtr<IUnknown, &__uuidof(IUnknown)> : public CComPtr<IUnknown> {
public:
  CComQIPtr() {}
  CComQIPtr(IUnknown* lp) : CComPtr<IUnknown>(lp) {}
  CComQIPtr(const CComQIPtr<IUnknown, &__uuidof(IUnknown)>& lp) : CComPtr<IUnknown>(lp.p) {}
  IUnknown* operator=(IUnknown* lp) { return CComPtr<IUnknown>::operator=(lp); }
  IUnknown* operator=(const CComQIPtr<IUnknown, &__uuidof(IUnknown)>& lp) {
    return CComPtr<IUnknown>::operator=(lp.p);
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
  bool operator==(const CComBSTR& src) const {
    UINT n = Length();
    return n == src.Length() && (n == 0 || memcmp(m_str, src.m_str, n * sizeof(OLECHAR)) == 0);
  }
  bool operator!=(const CComBSTR& src) const { return !(*this == src); }
  bool operator<(const CComBSTR& src) const {
    if (m_str == NULL) return src.m_str != NULL;
    if (src.m_str == NULL) return false;
    return wcscmp(m_str, src.m_str) < 0;
  }

  unsigned int ByteLength() const { return m_str != NULL ? ::SysStringByteLen(m_str) : 0; }
  HRESULT AppendBSTR(BSTR p) { return Append(p, (int)::SysStringLen(p)); }
  HRESULT Append(OLECHAR ch) { return Append(&ch, 1); }
  HRESULT Append(char ch) {
    OLECHAR w = (OLECHAR)(unsigned char)ch;
    return Append(&w, 1);
  }
  HRESULT Append(LPCSTR lpsz) {
    CComBSTR s(lpsz);
    return Append(s);
  }
  HRESULT AppendBytes(const char* lpsz, int nLen) {
    if (lpsz == NULL || nLen == 0) return S_OK;
    UINT n1 = ByteLength();
    BSTR b = ::SysAllocStringByteLen(NULL, n1 + (UINT)nLen);
    if (b == NULL) return E_OUTOFMEMORY;
    if (n1 != 0) memcpy(b, m_str, n1);
    memcpy((char*)b + n1, lpsz, (size_t)nLen);
    ::SysFreeString(m_str);
    m_str = b;
    return S_OK;
  }
  CComBSTR& operator+=(LPCSTR lpsz) {
    Append(lpsz);
    return *this;
  }
  HRESULT ToLower() {
    if (m_str != NULL) ::CharLowerBuffW(m_str, Length());
    return S_OK;
  }
  HRESULT ToUpper() {
    if (m_str != NULL) ::CharUpperBuffW(m_str, Length());
    return S_OK;
  }
  // A string resource of the module (or of hInst).
  bool LoadString(HINSTANCE hInst, UINT nID) {
    Empty();
    WCHAR sz[1024];
    int n = ::LoadStringW(hInst, nID, sz, 1024);
    if (n <= 0) return false;
    m_str = ::SysAllocStringLen(sz, (UINT)n);
    return m_str != NULL;
  }
  bool LoadString(UINT nID) { return LoadString(_AtlComCliResourceInstance(), nID); }

  // The string in a stream: its size in bytes with the terminating nul
  // (0 for no string), then its characters and the nul.
  HRESULT WriteToStream(IStream* pStream) {
    if (pStream == NULL) return E_INVALIDARG;
    ULONG cb = m_str != NULL ? ::SysStringByteLen(m_str) + (ULONG)sizeof(OLECHAR) : 0;
    HRESULT hr = pStream->Write(&cb, sizeof(cb), NULL);
    if (SUCCEEDED(hr) && cb != 0) hr = pStream->Write(m_str, cb, NULL);
    return hr;
  }
  HRESULT ReadFromStream(IStream* pStream) {
    if (pStream == NULL) return E_INVALIDARG;
    Empty();
    ULONG cb = 0, cbRead = 0;
    HRESULT hr = pStream->Read(&cb, sizeof(cb), &cbRead);
    if (FAILED(hr)) return hr;
    if (cbRead != sizeof(cb)) return E_FAIL;
    if (cb == 0) return S_OK;
    if (cb < sizeof(OLECHAR)) return E_FAIL;
    m_str = ::SysAllocStringByteLen(NULL, cb - (ULONG)sizeof(OLECHAR));
    if (m_str == NULL) return E_OUTOFMEMORY;
    hr = pStream->Read(m_str, cb, &cbRead);
    if (SUCCEEDED(hr) && cbRead != cb) hr = E_FAIL;
    if (FAILED(hr)) Empty();
    return hr;
  }

private:
  static HINSTANCE _AtlComCliResourceInstance() {
    extern IMAGE_DOS_HEADER __ImageBase;
    return (HINSTANCE)&__ImageBase;
  }
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
  CComVariant(IUnknown* pSrc) {
    vt = VT_UNKNOWN;
    punkVal = pSrc;
    if (pSrc != NULL) pSrc->AddRef();
  }
  CComVariant(LPCSTR lpszSrc) {
    ::VariantInit(this);
    CComBSTR s(lpszSrc);
    vt = VT_BSTR;
    bstrVal = s.Detach();
  }
  CComVariant(const CComBSTR& bstrSrc) {
    vt = VT_BSTR;
    bstrVal = bstrSrc.Copy();
  }
  CComVariant(long nSrc, VARTYPE vtSrc) {
    vt = vtSrc == VT_ERROR ? VT_ERROR : VT_I4;
    lVal = (LONG)nSrc;
  }
  CComVariant(BYTE nSrc) {
    vt = VT_UI1;
    bVal = nSrc;
  }
  CComVariant(char cSrc) {
    vt = VT_I1;
    cVal = cSrc;
  }
  CComVariant(unsigned short nSrc) {
    vt = VT_UI2;
    uiVal = nSrc;
  }
  CComVariant(unsigned int nSrc, VARTYPE vtSrc = VT_UI4) {
    vt = vtSrc == VT_UINT ? VT_UINT : VT_UI4;
    ulVal = nSrc;
  }
  CComVariant(unsigned long nSrc) {
    vt = VT_UI4;
    ulVal = (ULONG)nSrc;
  }
  CComVariant(LONGLONG nSrc) {
    vt = VT_I8;
    llVal = nSrc;
  }
  CComVariant(ULONGLONG nSrc) {
    vt = VT_UI8;
    ullVal = nSrc;
  }
  CComVariant(float fltSrc) {
    vt = VT_R4;
    fltVal = fltSrc;
  }
  CComVariant(double dblSrc, VARTYPE vtSrc) {
    vt = vtSrc == VT_DATE ? VT_DATE : VT_R8;
    dblVal = dblSrc;
  }
  CComVariant(CY cySrc) {
    vt = VT_CY;
    cyVal = cySrc;
  }
  ~CComVariant() { ::VariantClear(this); }

  CComVariant& operator=(LPCOLESTR lpszSrc) {
    Clear();
    vt = VT_BSTR;
    bstrVal = ::SysAllocString(lpszSrc);
    return *this;
  }
  CComVariant& operator=(LPCSTR lpszSrc) {
    Clear();
    CComBSTR s(lpszSrc);
    vt = VT_BSTR;
    bstrVal = s.Detach();
    return *this;
  }
  CComVariant& operator=(const CComBSTR& bstrSrc) {
    Clear();
    vt = VT_BSTR;
    bstrVal = bstrSrc.Copy();
    return *this;
  }
  CComVariant& operator=(bool bSrc) {
    Clear();
    vt = VT_BOOL;
    boolVal = bSrc ? VARIANT_TRUE : VARIANT_FALSE;
    return *this;
  }
  CComVariant& operator=(int nSrc) {
    Clear();
    vt = VT_I4;
    lVal = nSrc;
    return *this;
  }
  CComVariant& operator=(long nSrc) {
    Clear();
    vt = VT_I4;
    lVal = (LONG)nSrc;
    return *this;
  }
  CComVariant& operator=(short nSrc) {
    Clear();
    vt = VT_I2;
    iVal = nSrc;
    return *this;
  }
  CComVariant& operator=(BYTE nSrc) {
    Clear();
    vt = VT_UI1;
    bVal = nSrc;
    return *this;
  }
  CComVariant& operator=(unsigned long nSrc) {
    Clear();
    vt = VT_UI4;
    ulVal = (ULONG)nSrc;
    return *this;
  }
  CComVariant& operator=(float fltSrc) {
    Clear();
    vt = VT_R4;
    fltVal = fltSrc;
    return *this;
  }
  CComVariant& operator=(double dblSrc) {
    Clear();
    vt = VT_R8;
    dblVal = dblSrc;
    return *this;
  }
  CComVariant& operator=(IDispatch* pSrc) {
    if (pSrc != NULL) pSrc->AddRef();
    Clear();
    vt = VT_DISPATCH;
    pdispVal = pSrc;
    return *this;
  }
  CComVariant& operator=(IUnknown* pSrc) {
    if (pSrc != NULL) pSrc->AddRef();
    Clear();
    vt = VT_UNKNOWN;
    punkVal = pSrc;
    return *this;
  }

  // Equal by type and value (VarCmp for those it compares).
  bool operator==(const VARIANT& varSrc) const {
    if (this == &varSrc) return true;
    if (vt != varSrc.vt) return false;
    if (vt == VT_EMPTY || vt == VT_NULL) return true;
    if (vt == VT_UNKNOWN || vt == VT_DISPATCH) return punkVal == varSrc.punkVal;
    return ::VarCmp(const_cast<CComVariant*>(this), const_cast<VARIANT*>(&varSrc),
                    LOCALE_USER_DEFAULT, 0) == (HRESULT)VARCMP_EQ;
  }
  bool operator!=(const VARIANT& varSrc) const { return !(*this == varSrc); }
  bool operator<(const VARIANT& varSrc) const {
    return ::VarCmp(const_cast<CComVariant*>(this), const_cast<VARIANT*>(&varSrc),
                    LOCALE_USER_DEFAULT, 0) == (HRESULT)VARCMP_LT;
  }
  bool operator>(const VARIANT& varSrc) const {
    return ::VarCmp(const_cast<CComVariant*>(this), const_cast<VARIANT*>(&varSrc),
                    LOCALE_USER_DEFAULT, 0) == (HRESULT)VARCMP_GT;
  }

  HRESULT Attach(VARIANT* pSrc) {
    if (pSrc == NULL) return E_INVALIDARG;
    HRESULT hr = Clear();
    if (SUCCEEDED(hr)) {
      memcpy((VARIANT*)this, pSrc, sizeof(VARIANT));
      pSrc->vt = VT_EMPTY;
    }
    return hr;
  }
  HRESULT CopyTo(BSTR* pstrDest) const {
    if (pstrDest == NULL) return E_POINTER;
    *pstrDest = NULL;
    if (vt == VT_BSTR) {
      *pstrDest = ::SysAllocStringByteLen((const char*)bstrVal, ::SysStringByteLen(bstrVal));
      return *pstrDest != NULL || bstrVal == NULL ? S_OK : E_OUTOFMEMORY;
    }
    CComVariant v;
    HRESULT hr = ::VariantChangeType(&v, const_cast<CComVariant*>(this), 0, VT_BSTR);
    if (SUCCEEDED(hr)) {
      *pstrDest = v.bstrVal;
      v.vt = VT_EMPTY;
    }
    return hr;
  }
  template <typename T>
  void SetByRef(T* pT);

  // The variant in a stream: its type, then its value (a string as
  // CComBSTR writes it, an object by OleSaveToStream: its CLSID and its
  // state).
  HRESULT WriteToStream(IStream* pStream) {
    if (pStream == NULL) return E_INVALIDARG;
    HRESULT hr = pStream->Write(&vt, sizeof(VARTYPE), NULL);
    if (FAILED(hr)) return hr;
    int cb = _FixedSize(vt);
    if (cb > 0) return pStream->Write(&bVal, (ULONG)cb, NULL);
    switch (vt) {
    case VT_EMPTY:
    case VT_NULL:
      return S_OK;
    case VT_BSTR: {
      CComBSTR s;
      s.m_str = bstrVal;
      hr = s.WriteToStream(pStream);
      s.m_str = NULL;
      return hr;
    }
    case VT_UNKNOWN:
    case VT_DISPATCH: {
      if (punkVal == NULL) return ::WriteClassStm(pStream, _NullClsid());
      IPersistStream* pPersist = NULL;
      hr = punkVal->QueryInterface(IID_IPersistStream, (void**)&pPersist);
      if (FAILED(hr)) {
        IPersistStreamInit* pInit = NULL;
        hr = punkVal->QueryInterface(IID_IPersistStreamInit, (void**)&pInit);
        if (FAILED(hr)) return hr;
        pPersist = (IPersistStream*)pInit;  // the same vtable up to Save
      }
      hr = ::OleSaveToStream(pPersist, pStream);
      pPersist->Release();
      return hr;
    }
    default:
      return DISP_E_BADVARTYPE;
    }
  }
  HRESULT ReadFromStream(IStream* pStream) {
    if (pStream == NULL) return E_INVALIDARG;
    HRESULT hr = Clear();
    if (FAILED(hr)) return hr;
    VARTYPE vtRead = VT_EMPTY;
    ULONG cbRead = 0;
    hr = pStream->Read(&vtRead, sizeof(VARTYPE), &cbRead);
    if (FAILED(hr)) return hr;
    if (cbRead != sizeof(VARTYPE)) return E_FAIL;
    int cb = _FixedSize(vtRead);
    if (cb > 0) {
      vt = vtRead;
      hr = pStream->Read(&bVal, (ULONG)cb, &cbRead);
      if (SUCCEEDED(hr) && cbRead != (ULONG)cb) hr = E_FAIL;
      if (FAILED(hr)) vt = VT_EMPTY;
      return hr;
    }
    switch (vtRead) {
    case VT_EMPTY:
    case VT_NULL:
      vt = vtRead;
      return S_OK;
    case VT_BSTR: {
      CComBSTR s;
      hr = s.ReadFromStream(pStream);
      if (SUCCEEDED(hr)) {
        vt = VT_BSTR;
        bstrVal = s.Detach();
      }
      return hr;
    }
    case VT_UNKNOWN:
    case VT_DISPATCH: {
      CLSID clsid;
      hr = ::ReadClassStm(pStream, &clsid);
      if (FAILED(hr)) return hr;
      vt = vtRead;
      punkVal = NULL;
      if (memcmp(&clsid, &_NullClsid(), sizeof(CLSID)) == 0) return S_OK;
      IUnknown* pUnk = NULL;
      hr = ::CoCreateInstance(clsid, NULL, CLSCTX_ALL, IID_IUnknown, (void**)&pUnk);
      if (FAILED(hr)) {
        vt = VT_EMPTY;
        return hr;
      }
      IPersistStream* pPersist = NULL;
      hr = pUnk->QueryInterface(IID_IPersistStream, (void**)&pPersist);
      if (FAILED(hr)) hr = pUnk->QueryInterface(IID_IPersistStreamInit, (void**)&pPersist);
      if (SUCCEEDED(hr)) {
        hr = pPersist->Load(pStream);
        pPersist->Release();
      }
      if (SUCCEEDED(hr) && vtRead == VT_DISPATCH) {
        IDispatch* pDisp = NULL;
        hr = pUnk->QueryInterface(IID_IDispatch, (void**)&pDisp);
        pUnk->Release();
        pUnk = pDisp;
      }
      if (FAILED(hr)) {
        if (pUnk != NULL) pUnk->Release();
        vt = VT_EMPTY;
        return hr;
      }
      punkVal = pUnk;
      return S_OK;
    }
    default:
      return DISP_E_BADVARTYPE;
    }
  }
  // The size WriteToStream writes.
  ULONG GetSize() const {
    int cb = _FixedSize(vt);
    if (cb > 0) return sizeof(VARTYPE) + (ULONG)cb;
    if (vt == VT_BSTR)
      return sizeof(VARTYPE) + sizeof(ULONG) +
             (bstrVal != NULL ? ::SysStringByteLen(bstrVal) + (ULONG)sizeof(OLECHAR) : 0);
    return sizeof(VARTYPE);
  }

private:
  static int _FixedSize(VARTYPE v) {
    switch (v) {
    case VT_UI1:
    case VT_I1:
      return 1;
    case VT_I2:
    case VT_UI2:
    case VT_BOOL:
      return 2;
    case VT_I4:
    case VT_UI4:
    case VT_R4:
    case VT_INT:
    case VT_UINT:
    case VT_ERROR:
      return 4;
    case VT_R8:
    case VT_CY:
    case VT_DATE:
    case VT_I8:
    case VT_UI8:
      return 8;
    default:
      return 0;
    }
  }
  static const CLSID& _NullClsid() {
    static const CLSID c = {0, 0, 0, {0, 0, 0, 0, 0, 0, 0, 0}};
    return c;
  }

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

// A reference to a value of the caller's: VT_BYREF and the type of *pT.
template <typename T> struct _AtlVarTypeOf;
#define _ATL_VARTYPE_OF(t, v) \
  template <> struct _AtlVarTypeOf<t> { static const VARTYPE vt = v; };
_ATL_VARTYPE_OF(BYTE, VT_UI1)
_ATL_VARTYPE_OF(char, VT_I1)
_ATL_VARTYPE_OF(short, VT_I2)
_ATL_VARTYPE_OF(unsigned short, VT_UI2)
_ATL_VARTYPE_OF(int, VT_INT)
_ATL_VARTYPE_OF(unsigned int, VT_UINT)
_ATL_VARTYPE_OF(long, VT_I4)
_ATL_VARTYPE_OF(unsigned long, VT_UI4)
_ATL_VARTYPE_OF(LONGLONG, VT_I8)
_ATL_VARTYPE_OF(ULONGLONG, VT_UI8)
_ATL_VARTYPE_OF(float, VT_R4)
_ATL_VARTYPE_OF(double, VT_R8)
_ATL_VARTYPE_OF(CY, VT_CY)
_ATL_VARTYPE_OF(BSTR, VT_BSTR)
_ATL_VARTYPE_OF(IUnknown*, VT_UNKNOWN)
_ATL_VARTYPE_OF(IDispatch*, VT_DISPATCH)
_ATL_VARTYPE_OF(VARIANT, VT_VARIANT)
#undef _ATL_VARTYPE_OF

template <typename T>
inline void CComVariant::SetByRef(T* pT) {
  Clear();
  vt = (VARTYPE)(_AtlVarTypeOf<T>::vt | VT_BYREF);
  byref = (void*)pT;
}

}  // namespace ATL

#ifndef _ATL_NO_AUTOMATIC_NAMESPACE
using namespace ATL;
#endif

#endif  // __ATLCOMCLI_H__
