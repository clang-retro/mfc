// atlsafe.h - CComSafeArray, a SAFEARRAY owned by an object, and
// CComSafeArrayBound.
//
// The array stays locked while the object holds it, so GetAt and
// operator[] give references into its data. Elements of BSTR, VARIANT
// and interface pointers are copied (SysAllocString, VariantCopy,
// AddRef) or, with bCopy FALSE, taken over.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#ifndef __ATLSAFE_H__
#define __ATLSAFE_H__

#include <atlbase.h>

namespace ATL {

// The VARTYPE of a C++ element type.
template <typename T>
struct _ATL_AutomationType;
#define _ATL_AUTOMATION_TYPE(T, v) \
  template <>                      \
  struct _ATL_AutomationType<T> {  \
    static const VARTYPE type = v; \
  };
_ATL_AUTOMATION_TYPE(char, VT_I1)
_ATL_AUTOMATION_TYPE(signed char, VT_I1)
_ATL_AUTOMATION_TYPE(unsigned char, VT_UI1)
_ATL_AUTOMATION_TYPE(short, VT_I2)
_ATL_AUTOMATION_TYPE(unsigned short, VT_UI2)
_ATL_AUTOMATION_TYPE(int, VT_I4)
_ATL_AUTOMATION_TYPE(unsigned int, VT_UI4)
_ATL_AUTOMATION_TYPE(long, sizeof(long) == 4 ? VT_I4 : VT_I8)
_ATL_AUTOMATION_TYPE(unsigned long, sizeof(long) == 4 ? VT_UI4 : VT_UI8)
_ATL_AUTOMATION_TYPE(long long, VT_I8)
_ATL_AUTOMATION_TYPE(unsigned long long, VT_UI8)
_ATL_AUTOMATION_TYPE(float, VT_R4)
_ATL_AUTOMATION_TYPE(double, VT_R8)
_ATL_AUTOMATION_TYPE(DECIMAL, VT_DECIMAL)
_ATL_AUTOMATION_TYPE(CY, VT_CY)
_ATL_AUTOMATION_TYPE(BSTR, VT_BSTR)
_ATL_AUTOMATION_TYPE(CComBSTR, VT_BSTR)
_ATL_AUTOMATION_TYPE(VARIANT, VT_VARIANT)
_ATL_AUTOMATION_TYPE(CComVariant, VT_VARIANT)
_ATL_AUTOMATION_TYPE(IUnknown*, VT_UNKNOWN)
_ATL_AUTOMATION_TYPE(IDispatch*, VT_DISPATCH)
#undef _ATL_AUTOMATION_TYPE

class CComSafeArrayBound : public SAFEARRAYBOUND {
public:
  CComSafeArrayBound(ULONG ulCount = 0, LONG lLowerBound = 0) {
    cElements = ulCount;
    lLbound = lLowerBound;
  }
  CComSafeArrayBound& operator=(const CComSafeArrayBound& bound) {
    cElements = bound.cElements;
    lLbound = bound.lLbound;
    return *this;
  }
  CComSafeArrayBound& operator=(ULONG ulCount) {
    cElements = ulCount;
    lLbound = 0;
    return *this;
  }
  ULONG GetCount() const { return cElements; }
  ULONG SetCount(ULONG ulCount) { return cElements = ulCount; }
  LONG GetLowerBound() const { return lLbound; }
  LONG SetLowerBound(LONG lLowerBound) { return lLbound = lLowerBound; }
  LONG GetUpperBound() const { return lLbound + (LONG)cElements - 1; }
};

template <typename T, VARTYPE _vartype = _ATL_AutomationType<T>::type>
class CComSafeArray {
public:
  LPSAFEARRAY m_psa;

  CComSafeArray() : m_psa(NULL) {}
  CComSafeArray(const SAFEARRAYBOUND& bound) : m_psa(NULL) { _Check(Create(&bound, 1)); }
  CComSafeArray(ULONG ulCount, LONG lLBound = 0) : m_psa(NULL) { _Check(Create(ulCount, lLBound)); }
  CComSafeArray(const SAFEARRAYBOUND* pBound, UINT uDims = 1) : m_psa(NULL) {
    _Check(Create(pBound, uDims));
  }
  CComSafeArray(const SAFEARRAY* psaSrc) : m_psa(NULL) { _Check(CopyFrom(psaSrc)); }
  CComSafeArray(const SAFEARRAY& saSrc) : m_psa(NULL) { _Check(CopyFrom(&saSrc)); }
  CComSafeArray(const CComSafeArray& saSrc) : m_psa(NULL) {
    if (saSrc.m_psa != NULL) _Check(CopyFrom(saSrc.m_psa));
  }
  ~CComSafeArray() { Destroy(); }

  CComSafeArray& operator=(const CComSafeArray& saSrc) {
    if (this != &saSrc) *this = saSrc.m_psa;
    return *this;
  }
  CComSafeArray& operator=(const SAFEARRAY* psaSrc) {
    if (psaSrc == m_psa) return *this;
    if (psaSrc == NULL)
      Destroy();
    else
      _Check(CopyFrom(psaSrc));
    return *this;
  }

  operator const SAFEARRAY*() const { return m_psa; }
  operator LPSAFEARRAY() { return m_psa; }
  LPSAFEARRAY* GetSafeArrayPtr() { return &m_psa; }

  HRESULT Create(ULONG ulCount = 0, LONG lLBound = 0) {
    CComSafeArrayBound bound(ulCount, lLBound);
    return Create(&bound, 1);
  }
  HRESULT Create(const SAFEARRAYBOUND* pBound, UINT uDims = 1) {
    if (m_psa != NULL) return E_FAIL;
    if (pBound == NULL || uDims == 0) return E_INVALIDARG;
    m_psa = ::SafeArrayCreate(_vartype, uDims, const_cast<SAFEARRAYBOUND*>(pBound));
    if (m_psa == NULL) return E_OUTOFMEMORY;
    HRESULT hr = ::SafeArrayLock(m_psa);
    if (FAILED(hr)) {
      ::SafeArrayDestroy(m_psa);
      m_psa = NULL;
    }
    return hr;
  }
  HRESULT Destroy() {
    if (m_psa == NULL) return S_OK;
    HRESULT hr = ::SafeArrayUnlock(m_psa);
    if (SUCCEEDED(hr)) hr = ::SafeArrayDestroy(m_psa);
    m_psa = NULL;
    return hr;
  }
  HRESULT Attach(const SAFEARRAY* psaSrc) {
    if (psaSrc == NULL) return E_INVALIDARG;
    VARTYPE vt;
    HRESULT hr = ::SafeArrayGetVartype(const_cast<LPSAFEARRAY>(psaSrc), &vt);
    if (FAILED(hr)) return hr;
    if (vt != _vartype) return E_INVALIDARG;
    hr = Destroy();
    if (FAILED(hr)) return hr;
    m_psa = const_cast<LPSAFEARRAY>(psaSrc);
    hr = ::SafeArrayLock(m_psa);
    if (FAILED(hr)) m_psa = NULL;
    return hr;
  }
  LPSAFEARRAY Detach() {
    if (m_psa == NULL) return NULL;
    ::SafeArrayUnlock(m_psa);
    LPSAFEARRAY psa = m_psa;
    m_psa = NULL;
    return psa;
  }
  HRESULT CopyFrom(const SAFEARRAY* psaSrc) {
    if (psaSrc == NULL) return E_INVALIDARG;
    LPSAFEARRAY psaCopy = NULL;
    HRESULT hr = ::SafeArrayCopy(const_cast<LPSAFEARRAY>(psaSrc), &psaCopy);
    if (FAILED(hr)) return hr;
    hr = Attach(psaCopy);
    if (FAILED(hr)) ::SafeArrayDestroy(psaCopy);
    return hr;
  }
  HRESULT CopyTo(LPSAFEARRAY* ppArray) {
    if (ppArray == NULL) return E_POINTER;
    if (m_psa == NULL) return E_FAIL;
    return ::SafeArrayCopy(m_psa, ppArray);
  }

  UINT GetDimensions() const { return m_psa == NULL ? 0 : ::SafeArrayGetDim(m_psa); }
  LONG GetLowerBound(UINT uDim = 0) const {
    LONG l = 0;
    _Check(::SafeArrayGetLBound(m_psa, uDim + 1, &l));
    return l;
  }
  LONG GetUpperBound(UINT uDim = 0) const {
    LONG l = 0;
    _Check(::SafeArrayGetUBound(m_psa, uDim + 1, &l));
    return l;
  }
  ULONG GetCount(UINT uDim = 0) const {
    if (m_psa == NULL) return 0;
    return (ULONG)(GetUpperBound(uDim) - GetLowerBound(uDim) + 1);
  }
  VARTYPE GetType() const {
    VARTYPE vt = VT_EMPTY;
    if (m_psa != NULL) ::SafeArrayGetVartype(m_psa, &vt);
    return vt;
  }
  bool IsSizable() const {
    return m_psa != NULL && (m_psa->fFeatures & FADF_FIXEDSIZE) == 0 && m_psa->cLocks <= 1;
  }

  // One-dimensional access.
  T& GetAt(LONG lIndex) const {
    LONG lLBound = GetLowerBound();
    if (lIndex < lLBound || lIndex > GetUpperBound()) AtlThrow(E_INVALIDARG);
    return ((T*)m_psa->pvData)[lIndex - lLBound];
  }
  template <typename I>
  T& operator[](I nIndex) const {
    return GetAt((LONG)nIndex);
  }
  HRESULT SetAt(LONG lIndex, const T& t, BOOL bCopy = TRUE) {
    if (m_psa == NULL) return E_FAIL;
    LONG lLBound = GetLowerBound();
    if (lIndex < lLBound || lIndex > GetUpperBound()) return E_INVALIDARG;
    return _Assign(((T*)m_psa->pvData)[lIndex - lLBound], t, bCopy);
  }

  // Any number of dimensions, indexes as SafeArrayGetElement takes them.
  HRESULT MultiDimGetAt(const LONG* alIndex, T& t) {
    if (alIndex == NULL || m_psa == NULL) return E_INVALIDARG;
    void* pv;
    HRESULT hr = ::SafeArrayPtrOfIndex(m_psa, const_cast<LONG*>(alIndex), &pv);
    if (FAILED(hr)) return hr;
    return _Assign(t, *(T*)pv, TRUE);
  }
  HRESULT MultiDimSetAt(const LONG* alIndex, const T& t) {
    if (alIndex == NULL || m_psa == NULL) return E_INVALIDARG;
    void* pv;
    HRESULT hr = ::SafeArrayPtrOfIndex(m_psa, const_cast<LONG*>(alIndex), &pv);
    if (FAILED(hr)) return hr;
    return _Assign(*(T*)pv, t, TRUE);
  }

  HRESULT Resize(ULONG ulCount, LONG lLBound = 0) {
    CComSafeArrayBound bound(ulCount, lLBound);
    return Resize(&bound);
  }
  HRESULT Resize(const SAFEARRAYBOUND* pBound) {
    if (pBound == NULL) return E_INVALIDARG;
    if (m_psa == NULL) return Create(pBound, 1);
    if (GetDimensions() != 1) return E_FAIL;
    HRESULT hr = ::SafeArrayUnlock(m_psa);
    if (FAILED(hr)) return hr;
    hr = ::SafeArrayRedim(m_psa, const_cast<SAFEARRAYBOUND*>(pBound));
    HRESULT hrLock = ::SafeArrayLock(m_psa);
    return FAILED(hr) ? hr : hrLock;
  }

  // Appending to a one-dimensional array.
  HRESULT Add(const T& t, BOOL bCopy = TRUE) {
    if (m_psa == NULL) {
      HRESULT hr = Create(1, 0);
      if (FAILED(hr)) return hr;
      return SetAt(0, t, bCopy);
    }
    LONG lUpper = GetUpperBound();
    HRESULT hr = Resize(GetCount() + 1, GetLowerBound());
    if (FAILED(hr)) return hr;
    return SetAt(lUpper + 1, t, bCopy);
  }
  HRESULT Add(ULONG ulCount, const T* pT, BOOL bCopy = TRUE) {
    if (pT == NULL) return E_INVALIDARG;
    LONG lFirst = 0;
    if (m_psa == NULL) {
      HRESULT hr = Create(ulCount, 0);
      if (FAILED(hr)) return hr;
    } else {
      lFirst = GetUpperBound() + 1;
      HRESULT hr = Resize(GetCount() + ulCount, GetLowerBound());
      if (FAILED(hr)) return hr;
    }
    for (ULONG i = 0; i < ulCount; i++) {
      HRESULT hr = SetAt(lFirst + (LONG)i, pT[i], bCopy);
      if (FAILED(hr)) return hr;
    }
    return S_OK;
  }
  HRESULT Add(const SAFEARRAY* psaSrc) {
    if (psaSrc == NULL) return E_INVALIDARG;
    CComSafeArray<T, _vartype> src;
    HRESULT hr = src.CopyFrom(psaSrc);
    if (FAILED(hr)) return hr;
    ULONG n = src.GetCount();
    for (ULONG i = 0; i < n; i++) {
      hr = Add(src.GetAt(src.GetLowerBound() + (LONG)i), TRUE);
      if (FAILED(hr)) return hr;
    }
    return S_OK;
  }

private:
  static void _Check(HRESULT hr) {
    if (FAILED(hr)) AtlThrow(hr);
  }
  // An element of the array's own kind, copied or taken over.
  static HRESULT _Assign(T& dst, const T& src, BOOL bCopy) {
    if constexpr (_vartype == VT_BSTR) {
      BSTR* pd = (BSTR*)&dst;
      BSTR s = *(const BSTR*)&src;
      if (*pd == s) return S_OK;
      BSTR n = s;
      if (bCopy && s != NULL) {
        n = ::SysAllocStringByteLen((LPCSTR)s, ::SysStringByteLen(s));
        if (n == NULL) return E_OUTOFMEMORY;
      }
      ::SysFreeString(*pd);
      *pd = n;
      return S_OK;
    } else if constexpr (_vartype == VT_VARIANT) {
      VARIANT* pd = (VARIANT*)&dst;
      const VARIANT* ps = (const VARIANT*)&src;
      if (pd == ps) return S_OK;
      if (bCopy) return ::VariantCopy(pd, const_cast<VARIANT*>(ps));
      HRESULT hr = ::VariantClear(pd);
      if (FAILED(hr)) return hr;
      ::memcpy(pd, ps, sizeof(VARIANT));
      return S_OK;
    } else if constexpr (_vartype == VT_UNKNOWN || _vartype == VT_DISPATCH) {
      IUnknown** pd = (IUnknown**)&dst;
      IUnknown* s = *(IUnknown* const*)&src;
      if (bCopy && s != NULL) s->AddRef();
      if (*pd != NULL) (*pd)->Release();
      *pd = s;
      return S_OK;
    } else {
      (void)bCopy;
      dst = src;
      return S_OK;
    }
  }
};

}  // namespace ATL

#endif  // __ATLSAFE_H__
