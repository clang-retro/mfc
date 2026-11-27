// comutil.h - the compiler COM support classes for strings and variants:
// _bstr_t (a reference-counted BSTR, with its narrow copy made when asked
// for) and _variant_t (a VARIANT that clears itself, converts on
// extraction and raises _com_error on failure).
//
// On the portable layer WCHAR and OLECHAR are wchar_t, so a BSTR is a
// wchar_t string as on Windows (four bytes a character here). LONG and
// ULONG are int and unsigned int, which C++ cannot tell from int and
// unsigned int: a _variant_t made from an int is VT_I4 here, not VT_INT,
// and one from an unsigned int is VT_UI4 (pass VT_INT or VT_UINT to get
// those). A long (eight bytes on 64-bit Linux and macOS) is VT_I4, as
// Windows code means by it.
//
// The functions the Windows SDK has in comsupp.lib are inline (or weak)
// here: _com_issue_error, _com_issue_errorex, _com_util::ConvertStringToBSTR
// and ConvertBSTRToString, and (weak) _com_raise_error, which a program
// may define itself to report errors without exceptions. _com_error is
// here too, as these classes raise it.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#ifndef _INC_COMUTIL
#define _INC_COMUTIL

#include <windows.h>
#include <ole2.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __cplusplus

#ifndef _COM_ASSERT
#define _COM_ASSERT(x) ((void)0)
#endif

class _com_error;
class _bstr_t;
class _variant_t;
interface IErrorInfo;

__attribute__((weak)) void WINAPI _com_raise_error(HRESULT hr, IErrorInfo* perrinfo = NULL);
void WINAPI _com_issue_error(HRESULT hr);
void WINAPI _com_issue_errorex(HRESULT hr, IUnknown* punk, REFIID riid);

namespace _com_util {
inline void CheckError(HRESULT hr) {
  if (FAILED(hr)) _com_issue_error(hr);
}
// A BSTR from a string in the ANSI code page (NULL from NULL).
inline BSTR WINAPI ConvertStringToBSTR(const char* pSrc) {
  if (pSrc == NULL) return NULL;
  int n = ::MultiByteToWideChar(CP_ACP, 0, pSrc, -1, NULL, 0);
  if (n <= 0) {
    _com_issue_error(HRESULT_FROM_WIN32(::GetLastError()));
    return NULL;
  }
  BSTR b = ::SysAllocStringLen(NULL, (UINT)(n - 1));
  if (b == NULL) {
    _com_issue_error(E_OUTOFMEMORY);
    return NULL;
  }
  ::MultiByteToWideChar(CP_ACP, 0, pSrc, -1, b, n);
  return b;
}
// A string in the ANSI code page, from new[] (NULL from NULL).
inline char* WINAPI ConvertBSTRToString(BSTR pSrc) {
  if (pSrc == NULL) return NULL;
  int nChars = (int)::SysStringLen(pSrc) + 1;
  int n = ::WideCharToMultiByte(CP_ACP, 0, pSrc, nChars, NULL, 0, NULL, NULL);
  char* p = new char[n > 0 ? n : 1];
  if (n <= 0 || ::WideCharToMultiByte(CP_ACP, 0, pSrc, nChars, p, n, NULL, NULL) <= 0) p[0] = 0;
  return p;
}
}  // namespace _com_util

// ---------------------------------------------------------------------

class _bstr_t {
public:
  _bstr_t() throw() : m_Data(NULL) {}
  _bstr_t(const _bstr_t& s) throw() : m_Data(s.m_Data) { _AddRef(); }
  _bstr_t(const char* s) : m_Data(new Data_t(_com_util::ConvertStringToBSTR(s), false)) {}
  _bstr_t(const wchar_t* s) : m_Data(new Data_t(s != NULL ? _Alloc(s) : NULL, false)) {}
  _bstr_t(const _variant_t& var);
  // With fCopy false the _bstr_t takes bstr and frees it.
  _bstr_t(BSTR bstr, bool fCopy) : m_Data(new Data_t(fCopy && bstr != NULL ? _Copy(bstr) : bstr, false)) {}
  ~_bstr_t() throw() { _Free(); }

  _bstr_t& operator=(const _bstr_t& s) throw() {
    if (m_Data != s.m_Data) {
      _Free();
      m_Data = s.m_Data;
      _AddRef();
    }
    return *this;
  }
  _bstr_t& operator=(const char* s) {
    _bstr_t t(s);
    return *this = t;
  }
  _bstr_t& operator=(const wchar_t* s) {
    _bstr_t t(s);
    return *this = t;
  }
  _bstr_t& operator=(const _variant_t& var);
  _bstr_t& operator+=(const _bstr_t& s) {
    unsigned int n1 = length(), n2 = s.length();
    BSTR b = ::SysAllocStringLen(NULL, n1 + n2);
    if (b == NULL) {
      _com_issue_error(E_OUTOFMEMORY);
      return *this;
    }
    if (n1) memcpy(b, m_Data->m_wstr, n1 * sizeof(OLECHAR));
    if (n2) memcpy(b + n1, s.m_Data->m_wstr, n2 * sizeof(OLECHAR));
    b[n1 + n2] = 0;
    Data_t* p = new Data_t(b, false);
    _Free();
    m_Data = p;
    return *this;
  }
  _bstr_t operator+(const _bstr_t& s) const {
    _bstr_t b(*this);
    b += s;
    return b;
  }
  friend _bstr_t operator+(const char* s1, const _bstr_t& s2) {
    _bstr_t b(s1);
    b += s2;
    return b;
  }
  friend _bstr_t operator+(const wchar_t* s1, const _bstr_t& s2) {
    _bstr_t b(s1);
    b += s2;
    return b;
  }

  operator const wchar_t*() const throw() { return m_Data != NULL ? m_Data->m_wstr : NULL; }
  operator wchar_t*() const throw() { return m_Data != NULL ? m_Data->m_wstr : NULL; }
  operator const char*() const { return m_Data != NULL ? m_Data->GetString() : NULL; }
  operator char*() const { return m_Data != NULL ? (char*)m_Data->GetString() : NULL; }

  bool operator!() const throw() { return m_Data == NULL || m_Data->m_wstr == NULL; }
  bool operator==(const _bstr_t& str) const throw() { return _Compare(str) == 0; }
  bool operator!=(const _bstr_t& str) const throw() { return _Compare(str) != 0; }
  bool operator<(const _bstr_t& str) const throw() { return _Compare(str) < 0; }
  bool operator>(const _bstr_t& str) const throw() { return _Compare(str) > 0; }
  bool operator<=(const _bstr_t& str) const throw() { return _Compare(str) <= 0; }
  bool operator>=(const _bstr_t& str) const throw() { return _Compare(str) >= 0; }

  // A copy for the caller to free, or (fCopy false) the BSTR itself.
  BSTR copy(bool fCopy = true) const {
    if (m_Data == NULL || m_Data->m_wstr == NULL) return NULL;
    if (!fCopy) return m_Data->m_wstr;
    BSTR b = _Copy(m_Data->m_wstr);
    if (b == NULL) _com_issue_error(E_OUTOFMEMORY);
    return b;
  }
  unsigned int length() const throw() {
    return m_Data != NULL && m_Data->m_wstr != NULL ? ::SysStringLen(m_Data->m_wstr) : 0;
  }
  // A copy of s, in place of what the _bstr_t held.
  void Assign(BSTR s) {
    if (m_Data != NULL && m_Data->m_RefCount == 1 && m_Data->m_wstr == s) return;
    _bstr_t t(s, true);
    *this = t;
  }
  // The BSTR itself (shared with the copies of this _bstr_t).
  BSTR& GetBSTR() {
    if (m_Data == NULL) m_Data = new Data_t(NULL, false);
    m_Data->_FreeNarrow();
    return m_Data->m_wstr;
  }
  // Frees what the _bstr_t held and gives the address for an [out] BSTR.
  BSTR* GetAddress() {
    _Free();
    m_Data = new Data_t(NULL, false);
    return &m_Data->m_wstr;
  }
  void Attach(BSTR s) {
    _Free();
    m_Data = new Data_t(s, false);
  }
  BSTR Detach() throw() {
    if (m_Data == NULL) return NULL;
    BSTR b;
    if (m_Data->m_RefCount == 1) {
      b = m_Data->m_wstr;
      m_Data->m_wstr = NULL;
    } else {
      b = m_Data->m_wstr != NULL ? _Copy(m_Data->m_wstr) : NULL;
    }
    _Free();
    return b;
  }

private:
  struct Data_t {
    Data_t(BSTR b, bool) : m_wstr(b), m_str(NULL), m_RefCount(1) {}
    ~Data_t() {
      ::SysFreeString(m_wstr);
      _FreeNarrow();
    }
    const char* GetString() const {
      if (m_str == NULL && m_wstr != NULL) m_str = _com_util::ConvertBSTRToString(m_wstr);
      return m_str;
    }
    void _FreeNarrow() const {
      delete[] m_str;
      m_str = NULL;
    }
    BSTR m_wstr;
    mutable char* m_str;
    unsigned long m_RefCount;
  };
  static BSTR _Alloc(const wchar_t* s) {
    BSTR b = ::SysAllocString(s);
    if (b == NULL) _com_issue_error(E_OUTOFMEMORY);
    return b;
  }
  static BSTR _Copy(BSTR s) { return ::SysAllocStringByteLen((const char*)s, ::SysStringByteLen(s)); }
  void _AddRef() throw() {
    if (m_Data != NULL) m_Data->m_RefCount++;
  }
  void _Free() throw() {
    if (m_Data != NULL && --m_Data->m_RefCount == 0) delete m_Data;
    m_Data = NULL;
  }
  // Character by character, then by length; no string at all is before
  // every other, the empty one too.
  int _Compare(const _bstr_t& str) const throw() {
    if (m_Data == str.m_Data) return 0;
    if (m_Data == NULL || str.m_Data == NULL) return m_Data == NULL ? -1 : 1;
    unsigned int n1 = length(), n2 = str.length();
    unsigned int n = n1 < n2 ? n1 : n2;
    for (unsigned int i = 0; i < n; i++) {
      wchar_t c1 = m_Data->m_wstr[i], c2 = str.m_Data->m_wstr[i];
      if (c1 != c2) return c1 < c2 ? -1 : 1;
    }
    return n1 == n2 ? 0 : (n1 < n2 ? -1 : 1);
  }

  Data_t* m_Data;
};

// ---------------------------------------------------------------------

class _variant_t : public ::tagVARIANT {
public:
  _variant_t() throw() { ::VariantInit(this); }
  _variant_t(const VARIANT& varSrc) {
    ::VariantInit(this);
    _com_util::CheckError(::VariantCopy(this, const_cast<VARIANT*>(&varSrc)));
  }
  _variant_t(const VARIANT* pSrc) {
    if (pSrc == NULL) _com_issue_error(E_POINTER);
    ::VariantInit(this);
    _com_util::CheckError(::VariantCopy(this, const_cast<VARIANT*>(pSrc)));
  }
  _variant_t(const _variant_t& varSrc) {
    ::VariantInit(this);
    _com_util::CheckError(::VariantCopy(this, const_cast<_variant_t*>(&varSrc)));
  }
  // With fCopy false the _variant_t takes varSrc's value and empties it.
  _variant_t(VARIANT& varSrc, bool fCopy) {
    if (fCopy) {
      ::VariantInit(this);
      _com_util::CheckError(::VariantCopy(this, &varSrc));
    } else {
      memcpy((VARIANT*)this, &varSrc, sizeof(VARIANT));
      varSrc.vt = VT_EMPTY;
    }
  }
  _variant_t(short sSrc, VARTYPE vtSrc = VT_I2) {
    if (vtSrc != VT_I2 && vtSrc != VT_BOOL) _com_issue_error(E_INVALIDARG);
    ::VariantInit(this);
    vt = vtSrc;
    if (vtSrc == VT_BOOL)
      boolVal = sSrc ? VARIANT_TRUE : VARIANT_FALSE;
    else
      iVal = sSrc;
  }
  _variant_t(int lSrc, VARTYPE vtSrc = VT_I4) { _SetLong(lSrc, vtSrc); }
  _variant_t(long lSrc, VARTYPE vtSrc = VT_I4) { _SetLong((LONG)lSrc, vtSrc); }
  _variant_t(float fltSrc) throw() {
    ::VariantInit(this);
    vt = VT_R4;
    fltVal = fltSrc;
  }
  _variant_t(double dblSrc, VARTYPE vtSrc = VT_R8) {
    if (vtSrc != VT_R8 && vtSrc != VT_DATE) _com_issue_error(E_INVALIDARG);
    ::VariantInit(this);
    vt = vtSrc;
    dblVal = dblSrc;
  }
  _variant_t(const CY& cySrc) throw() {
    ::VariantInit(this);
    vt = VT_CY;
    cyVal = cySrc;
  }
  _variant_t(const _bstr_t& bstrSrc) {
    ::VariantInit(this);
    vt = VT_BSTR;
    bstrVal = bstrSrc.copy();
    _CheckString(!bstrSrc);
  }
  _variant_t(const wchar_t* pSrc) {
    ::VariantInit(this);
    vt = VT_BSTR;
    bstrVal = ::SysAllocString(pSrc);
    _CheckString(pSrc == NULL);
  }
  _variant_t(const char* pSrc) {
    ::VariantInit(this);
    vt = VT_BSTR;
    bstrVal = _com_util::ConvertStringToBSTR(pSrc);
  }
  _variant_t(IDispatch* pSrc, bool fAddRef = true) throw() {
    ::VariantInit(this);
    vt = VT_DISPATCH;
    pdispVal = pSrc;
    if (fAddRef && pSrc != NULL) pSrc->AddRef();
  }
  _variant_t(bool boolSrc) throw() {
    ::VariantInit(this);
    vt = VT_BOOL;
    boolVal = boolSrc ? VARIANT_TRUE : VARIANT_FALSE;
  }
  _variant_t(IUnknown* pSrc, bool fAddRef = true) throw() {
    ::VariantInit(this);
    vt = VT_UNKNOWN;
    punkVal = pSrc;
    if (fAddRef && pSrc != NULL) pSrc->AddRef();
  }
  // DECIMAL's first field is where VARIANT's vt is: vt goes in after.
  _variant_t(const DECIMAL& decSrc) throw() {
    decVal = decSrc;
    vt = VT_DECIMAL;
  }
  _variant_t(BYTE bSrc) throw() {
    ::VariantInit(this);
    vt = VT_UI1;
    bVal = bSrc;
  }
  _variant_t(char cSrc) throw() {
    ::VariantInit(this);
    vt = VT_I1;
    cVal = cSrc;
  }
  _variant_t(unsigned short usSrc) throw() {
    ::VariantInit(this);
    vt = VT_UI2;
    uiVal = usSrc;
  }
  _variant_t(unsigned int ulSrc, VARTYPE vtSrc = VT_UI4) {
    if (vtSrc != VT_UI4 && vtSrc != VT_UINT) _com_issue_error(E_INVALIDARG);
    ::VariantInit(this);
    vt = vtSrc;
    ulVal = ulSrc;
  }
  _variant_t(unsigned long ulSrc) throw() {
    ::VariantInit(this);
    vt = VT_UI4;
    ulVal = (ULONG)ulSrc;
  }
  _variant_t(long long i8Src) throw() {
    ::VariantInit(this);
    vt = VT_I8;
    llVal = i8Src;
  }
  _variant_t(unsigned long long ui8Src) throw() {
    ::VariantInit(this);
    vt = VT_UI8;
    ullVal = ui8Src;
  }
  ~_variant_t() throw() { ::VariantClear(this); }

  // Extraction: the value converted to the type asked for.
  operator short() const { return _Get(VT_I2).iVal; }
  operator int() const { return _Get(VT_I4).lVal; }
  operator long() const { return _Get(VT_I4).lVal; }
  operator float() const { return _Get(VT_R4).fltVal; }
  operator double() const { return _Get(VT_R8).dblVal; }
  operator CY() const { return _Get(VT_CY).cyVal; }
  operator _bstr_t() const;
  operator IDispatch*() const {
    _variant_t v = _Get(VT_DISPATCH);
    if (v.pdispVal != NULL) v.pdispVal->AddRef();
    return v.pdispVal;
  }
  operator bool() const { return _Get(VT_BOOL).boolVal != VARIANT_FALSE; }
  operator IUnknown*() const {
    _variant_t v = _Get(VT_UNKNOWN);
    if (v.punkVal != NULL) v.punkVal->AddRef();
    return v.punkVal;
  }
  operator DECIMAL() const {
    _variant_t v = _Get(VT_DECIMAL);
    DECIMAL d = v.decVal;
    d.wReserved = 0;
    return d;
  }
  operator BYTE() const { return _Get(VT_UI1).bVal; }
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wclass-conversion"
  operator VARIANT() const throw() {
    VARIANT v;
    ::VariantInit(&v);
    ::VariantCopy(&v, const_cast<_variant_t*>(this));
    return v;
  }
#pragma clang diagnostic pop
  operator char() const { return _Get(VT_I1).cVal; }
  operator unsigned short() const { return _Get(VT_UI2).uiVal; }
  operator unsigned int() const { return _Get(VT_UI4).ulVal; }
  operator unsigned long() const { return _Get(VT_UI4).ulVal; }
  operator long long() const { return _Get(VT_I8).llVal; }
  operator unsigned long long() const { return _Get(VT_UI8).ullVal; }

  _variant_t& operator=(const VARIANT& varSrc) {
    _com_util::CheckError(::VariantCopy(this, const_cast<VARIANT*>(&varSrc)));
    return *this;
  }
  _variant_t& operator=(const VARIANT* pSrc) {
    if (pSrc == NULL) _com_issue_error(E_POINTER);
    _com_util::CheckError(::VariantCopy(this, const_cast<VARIANT*>(pSrc)));
    return *this;
  }
  _variant_t& operator=(const _variant_t& varSrc) {
    _com_util::CheckError(::VariantCopy(this, const_cast<_variant_t*>(&varSrc)));
    return *this;
  }
  // A number keeps the type the variant has where it is of that kind
  // (VT_BOOL or VT_I2 for a short, VT_I4 or VT_ERROR or VT_BOOL for a
  // long, VT_R8 or VT_DATE for a double).
  _variant_t& operator=(short sSrc) {
    if (vt == VT_BOOL) {
      boolVal = sSrc ? VARIANT_TRUE : VARIANT_FALSE;
    } else if (vt != VT_I2) {
      Clear();
      vt = VT_I2;
      iVal = sSrc;
    } else {
      iVal = sSrc;
    }
    return *this;
  }
  _variant_t& operator=(int lSrc) { return _AssignLong(lSrc); }
  _variant_t& operator=(long lSrc) { return _AssignLong((LONG)lSrc); }
  _variant_t& operator=(float fltSrc) {
    _Reset(VT_R4);
    fltVal = fltSrc;
    return *this;
  }
  _variant_t& operator=(double dblSrc) {
    if (vt != VT_R8 && vt != VT_DATE) _Reset(VT_R8);
    dblVal = dblSrc;
    return *this;
  }
  _variant_t& operator=(const CY& cySrc) {
    _Reset(VT_CY);
    cyVal = cySrc;
    return *this;
  }
  _variant_t& operator=(const _bstr_t& bstrSrc) {
    _variant_t t(bstrSrc);
    return _Take(t);
  }
  _variant_t& operator=(const wchar_t* pSrc) {
    _variant_t t(pSrc);
    return _Take(t);
  }
  _variant_t& operator=(const char* pSrc) {
    _variant_t t(pSrc);
    return _Take(t);
  }
  _variant_t& operator=(IDispatch* pSrc) {
    _variant_t t(pSrc);
    return _Take(t);
  }
  _variant_t& operator=(bool boolSrc) {
    _Reset(VT_BOOL);
    boolVal = boolSrc ? VARIANT_TRUE : VARIANT_FALSE;
    return *this;
  }
  _variant_t& operator=(IUnknown* pSrc) {
    _variant_t t(pSrc);
    return _Take(t);
  }
  _variant_t& operator=(const DECIMAL& decSrc) {
    Clear();
    decVal = decSrc;
    vt = VT_DECIMAL;
    return *this;
  }
  _variant_t& operator=(BYTE bSrc) {
    _Reset(VT_UI1);
    bVal = bSrc;
    return *this;
  }
  _variant_t& operator=(char cSrc) {
    _Reset(VT_I1);
    cVal = cSrc;
    return *this;
  }
  _variant_t& operator=(unsigned short usSrc) {
    _Reset(VT_UI2);
    uiVal = usSrc;
    return *this;
  }
  _variant_t& operator=(unsigned int ulSrc) {
    if (vt != VT_UI4 && vt != VT_UINT) _Reset(VT_UI4);
    ulVal = ulSrc;
    return *this;
  }
  _variant_t& operator=(unsigned long ulSrc) {
    _Reset(VT_UI4);
    ulVal = (ULONG)ulSrc;
    return *this;
  }
  _variant_t& operator=(long long i8Src) {
    _Reset(VT_I8);
    llVal = i8Src;
    return *this;
  }
  _variant_t& operator=(unsigned long long ui8Src) {
    _Reset(VT_UI8);
    ullVal = ui8Src;
    return *this;
  }

  // The same type and the same value.
  bool operator==(const VARIANT& varSrc) const throw() { return _Equal(&varSrc); }
  bool operator==(const VARIANT* pSrc) const throw() { return pSrc != NULL && _Equal(pSrc); }
  bool operator!=(const VARIANT& varSrc) const throw() { return !_Equal(&varSrc); }
  bool operator!=(const VARIANT* pSrc) const throw() { return pSrc == NULL || !_Equal(pSrc); }

  void Clear() { _com_util::CheckError(::VariantClear(this)); }
  // Takes varSrc's value, leaving varSrc empty.
  void Attach(VARIANT& varSrc) {
    Clear();
    memcpy((VARIANT*)this, &varSrc, sizeof(VARIANT));
    varSrc.vt = VT_EMPTY;
  }
  VARIANT Detach() {
    VARIANT v;
    memcpy(&v, (VARIANT*)this, sizeof(VARIANT));
    vt = VT_EMPTY;
    return v;
  }
  VARIANT& GetVARIANT() throw() { return *(VARIANT*)this; }
  // Clears the value and gives the address for an [out] VARIANT.
  VARIANT* GetAddress() {
    Clear();
    return (VARIANT*)this;
  }
  // This variant (or pSrc's value) converted in place.
  void ChangeType(VARTYPE vartype, const _variant_t* pSrc = NULL) {
    if (pSrc == NULL) pSrc = this;
    if (pSrc != this || vartype != vt)
      _com_util::CheckError(::VariantChangeType(this, const_cast<_variant_t*>(pSrc), 0, vartype));
  }
  void SetString(const char* pSrc) { operator=(pSrc); }

private:
  void _SetLong(LONG lSrc, VARTYPE vtSrc) {
    if (vtSrc != VT_I4 && vtSrc != VT_ERROR && vtSrc != VT_BOOL && vtSrc != VT_INT) _com_issue_error(E_INVALIDARG);
    ::VariantInit(this);
    vt = vtSrc;
    if (vtSrc == VT_ERROR)
      scode = lSrc;
    else if (vtSrc == VT_BOOL)
      boolVal = lSrc ? VARIANT_TRUE : VARIANT_FALSE;
    else
      lVal = lSrc;
  }
  _variant_t& _AssignLong(LONG lSrc) {
    if (vt == VT_ERROR)
      scode = lSrc;
    else if (vt == VT_BOOL)
      boolVal = lSrc ? VARIANT_TRUE : VARIANT_FALSE;
    else if (vt == VT_INT)
      intVal = lSrc;
    else {
      _Reset(VT_I4);
      lVal = lSrc;
    }
    return *this;
  }
  void _Reset(VARTYPE vtNew) {
    if (vt != vtNew) {
      Clear();
      vt = vtNew;
    }
  }
  _variant_t& _Take(_variant_t& t) {
    Clear();
    memcpy((VARIANT*)this, (VARIANT*)&t, sizeof(VARIANT));
    t.vt = VT_EMPTY;
    return *this;
  }
  void _CheckString(bool bSrcNull) {
    if (bstrVal == NULL && !bSrcNull) _com_issue_error(E_OUTOFMEMORY);
  }
  _variant_t _Get(VARTYPE vtWant) const {
    if (vt == vtWant) return *this;
    _variant_t v;
    v.ChangeType(vtWant, this);
    return v;
  }
  bool _Equal(const VARIANT* p) const throw() {
    if (p == (const VARIANT*)this) return true;
    if (vt != p->vt) return false;
    switch (vt) {
      case VT_EMPTY:
      case VT_NULL:
        return true;
      case VT_BOOL:
        return boolVal == p->boolVal;
      case VT_I1:
      case VT_UI1:
        return bVal == p->bVal;
      case VT_I2:
      case VT_UI2:
        return iVal == p->iVal;
      case VT_I4:
      case VT_UI4:
      case VT_INT:
      case VT_UINT:
      case VT_ERROR:
        return lVal == p->lVal;
      case VT_I8:
      case VT_UI8:
        return llVal == p->llVal;
      case VT_R4:
        return fltVal == p->fltVal;
      case VT_R8:
      case VT_DATE:
        return dblVal == p->dblVal;
      case VT_CY:
        return cyVal.int64 == p->cyVal.int64;
      case VT_BSTR:
        return ::SysStringByteLen(bstrVal) == ::SysStringByteLen(p->bstrVal) &&
               (bstrVal == p->bstrVal || bstrVal == NULL || p->bstrVal == NULL ||
                memcmp(bstrVal, p->bstrVal, ::SysStringByteLen(bstrVal)) == 0);
      case VT_DISPATCH:
        return pdispVal == p->pdispVal;
      case VT_UNKNOWN:
        return punkVal == p->punkVal;
      case VT_DECIMAL:
        return decVal.signscale == p->decVal.signscale && decVal.Hi32 == p->decVal.Hi32 &&
               decVal.Lo64 == p->decVal.Lo64;
      default:
        return ::VarCmp(const_cast<_variant_t*>(this), const_cast<VARIANT*>(p), LOCALE_USER_DEFAULT, 0) ==
               VARCMP_EQ;
    }
  }
};

inline _bstr_t::_bstr_t(const _variant_t& var) : m_Data(NULL) {
  if (var.vt == VT_BSTR) {
    *this = _bstr_t(var.bstrVal, true);
    return;
  }
  _variant_t v;
  v.ChangeType(VT_BSTR, &var);
  *this = _bstr_t(v.bstrVal, true);
}
inline _bstr_t& _bstr_t::operator=(const _variant_t& var) {
  _bstr_t t(var);
  return *this = t;
}
inline _variant_t::operator _bstr_t() const {
  _variant_t v = _Get(VT_BSTR);
  return _bstr_t(v.bstrVal, true);
}

// The argument left out of an automation call.
inline _variant_t vtMissing(DISP_E_PARAMNOTFOUND, VT_ERROR);

#ifndef _USE_RAW
#define bstr_t _bstr_t
#define variant_t _variant_t
#endif

// ---------------------------------------------------------------------
// _com_error, which the support classes raise (here rather than in
// comdef.h, as they need it without that header).

class _com_error {
public:
  _com_error(HRESULT hr, IErrorInfo* perrinfo = NULL, bool fAddRef = false) throw()
      : m_hresult(hr), m_perrinfo(perrinfo), m_pszMsg(NULL) {
    if (m_perrinfo != NULL && fAddRef) m_perrinfo->AddRef();
  }
  _com_error(const _com_error& that) throw() : m_hresult(that.m_hresult), m_perrinfo(that.m_perrinfo), m_pszMsg(NULL) {
    if (m_perrinfo != NULL) m_perrinfo->AddRef();
  }
  virtual ~_com_error() throw() { _Free(); }
  _com_error& operator=(const _com_error& that) throw() {
    if (this != &that) {
      _Free();
      m_hresult = that.m_hresult;
      m_perrinfo = that.m_perrinfo;
      if (m_perrinfo != NULL) m_perrinfo->AddRef();
    }
    return *this;
  }

  HRESULT Error() const throw() { return m_hresult; }
  WORD WCode() const throw() { return HRESULTToWCode(m_hresult); }
  // The error object, with a reference for the caller.
  IErrorInfo* ErrorInfo() const throw() {
    if (m_perrinfo != NULL) m_perrinfo->AddRef();
    return m_perrinfo;
  }
  _bstr_t Description() const {
    BSTR b = NULL;
    if (m_perrinfo != NULL) m_perrinfo->GetDescription(&b);
    return _bstr_t(b, false);
  }
  DWORD HelpContext() const throw() {
    DWORD dw = 0;
    if (m_perrinfo != NULL) m_perrinfo->GetHelpContext(&dw);
    return dw;
  }
  _bstr_t HelpFile() const {
    BSTR b = NULL;
    if (m_perrinfo != NULL) m_perrinfo->GetHelpFile(&b);
    return _bstr_t(b, false);
  }
  _bstr_t Source() const {
    BSTR b = NULL;
    if (m_perrinfo != NULL) m_perrinfo->GetSource(&b);
    return _bstr_t(b, false);
  }
  GUID GUID_() const throw() {
    GUID guid = GUID_NULL;
    if (m_perrinfo != NULL) m_perrinfo->GetGUID(&guid);
    return guid;
  }
  // The system's text for the HRESULT, "IDispatch error #n" for a
  // dispatch exception's code, or "Unknown error 0x...".
  const TCHAR* ErrorMessage() const throw() {
    if (m_pszMsg == NULL) {
      ::FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                      NULL, (DWORD)m_hresult, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPTSTR)&m_pszMsg, 0,
                      NULL);
      if (m_pszMsg != NULL) {
        size_t n = 0;
        while (m_pszMsg[n]) n++;
        while (n > 0 && (m_pszMsg[n - 1] == '\r' || m_pszMsg[n - 1] == '\n')) m_pszMsg[--n] = 0;
      } else {
        m_pszMsg = (TCHAR*)::LocalAlloc(LMEM_FIXED, 32 * sizeof(TCHAR));
        if (m_pszMsg != NULL) {
          char sz[32];
          WORD wCode = WCode();
          if (wCode != 0)
            snprintf(sz, sizeof(sz), "IDispatch error #%d", (int)wCode);
          else
            snprintf(sz, sizeof(sz), "Unknown error 0x%0lX", (unsigned long)(DWORD)m_hresult);
          for (int i = 0; i < 32; i++)
            if ((m_pszMsg[i] = (TCHAR)(unsigned char)sz[i]) == 0) break;
        }
      }
    }
    return m_pszMsg;
  }

  // Dispatch exception codes and the HRESULTs they stand for.
  static HRESULT WCodeToHRESULT(WORD wCode) throw() {
    return wCode >= 0xFE00 ? (HRESULT)WCODE_HRESULT_LAST : (HRESULT)(WCODE_HRESULT_FIRST + wCode);
  }
  static WORD HRESULTToWCode(HRESULT hr) throw() {
    return hr >= (HRESULT)WCODE_HRESULT_FIRST && hr <= (HRESULT)WCODE_HRESULT_LAST
               ? (WORD)(hr - (HRESULT)WCODE_HRESULT_FIRST)
               : 0;
  }

private:
  enum {
    WCODE_HRESULT_FIRST = MAKE_HRESULT(SEVERITY_ERROR, FACILITY_ITF, 0x200),
    WCODE_HRESULT_LAST = MAKE_HRESULT(SEVERITY_ERROR, FACILITY_ITF + 1, 0) - 1
  };
  void _Free() throw() {
    if (m_perrinfo != NULL) m_perrinfo->Release();
    m_perrinfo = NULL;
    if (m_pszMsg != NULL) ::LocalFree((HLOCAL)m_pszMsg);
    m_pszMsg = NULL;
  }
  HRESULT m_hresult;
  IErrorInfo* m_perrinfo;
  mutable TCHAR* m_pszMsg;
};

// Throws _com_error (aborts in a program built without exceptions). A
// program's own _com_raise_error takes the place of this weak one.
__attribute__((weak)) void WINAPI _com_raise_error(HRESULT hr, IErrorInfo* perrinfo) {
#if defined(__cpp_exceptions) || defined(__EXCEPTIONS)
  throw _com_error(hr, perrinfo);
#else
  if (perrinfo != NULL) perrinfo->Release();
  fprintf(stderr, "_com_error 0x%08lX\n", (unsigned long)(DWORD)hr);
  abort();
#endif
}
inline void WINAPI _com_issue_error(HRESULT hr) { _com_raise_error(hr, NULL); }
// With the error object the thread has, where punk says it gives them
// for riid (ISupportErrorInfo).
inline void WINAPI _com_issue_errorex(HRESULT hr, IUnknown* punk, REFIID riid) {
  IErrorInfo* perrinfo = NULL;
  if (punk != NULL) {
    ISupportErrorInfo* psei = NULL;
    if (SUCCEEDED(punk->QueryInterface(IID_ISupportErrorInfo, (void**)&psei))) {
      if (psei->InterfaceSupportsErrorInfo(riid) == S_OK && ::GetErrorInfo(0, &perrinfo) != S_OK) perrinfo = NULL;
      psei->Release();
    }
  }
  _com_raise_error(hr, perrinfo);
}

#endif  // __cplusplus
#endif  // _INC_COMUTIL
