// atlchecked.h - ATL's checked string and memory functions (namespace
// ATL::Checked): memcpy_s, strcpy_s, strncpy_s, strcat_s and the rest, in
// char, wchar_t and TCHAR forms. They throw (AtlThrow) instead of
// returning an error, so a caller cannot ignore one.
//
// They are written here instead of calling the C runtime's *_s functions,
// which the msvcrt.dll of Windows 2000 and XP does not have.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#ifndef __ATLCHECKED_H__
#define __ATLCHECKED_H__

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <tchar.h>
#include "atlexcept.h"

#ifndef _TRUNCATE
#define _TRUNCATE ((size_t)-1)
#endif
#ifndef STRUNCATE
#define STRUNCATE 80
#endif

namespace ATL {

// An errno value from a C runtime function as an exception: the checked
// functions' policy. A truncation the caller asked for is not an error.
inline int AtlCrtErrorCheck(int nError) {
  switch (nError) {
    case 0:
    case STRUNCATE:
      break;
    case ENOMEM:
      AtlThrow(E_OUTOFMEMORY);
      break;
    default:
      AtlThrow(E_INVALIDARG);
      break;
  }
  return nError;
}

namespace Checked {
namespace _Impl {

template <class C>
inline size_t _Len(const C* p) {
  size_t n = 0;
  while (p[n] != 0) n++;
  return n;
}
template <class C>
inline size_t _NLen(const C* p, size_t nMax) {
  size_t n = 0;
  while (n < nMax && p[n] != 0) n++;
  return n;
}
template <class C>
inline int _Copy(C* pDest, size_t nDest, const C* pSrc) {
  if (pDest == NULL || nDest == 0) return EINVAL;
  if (pSrc == NULL) {
    pDest[0] = 0;
    return EINVAL;
  }
  size_t n = _Len(pSrc);
  if (n >= nDest) {
    pDest[0] = 0;
    return ERANGE;
  }
  memmove(pDest, pSrc, (n + 1) * sizeof(C));
  return 0;
}
template <class C>
inline int _NCopy(C* pDest, size_t nDest, const C* pSrc, size_t nCount) {
  if (pDest == NULL || nDest == 0) return EINVAL;
  if (nCount == 0) {
    pDest[0] = 0;
    return 0;
  }
  if (pSrc == NULL) {
    pDest[0] = 0;
    return EINVAL;
  }
  size_t n = _NLen(pSrc, nCount == _TRUNCATE ? nDest : nCount);
  int nRet = 0;
  if (n >= nDest) {
    if (nCount != _TRUNCATE) {
      pDest[0] = 0;
      return ERANGE;
    }
    n = nDest - 1;
    nRet = STRUNCATE;
  }
  memmove(pDest, pSrc, n * sizeof(C));
  pDest[n] = 0;
  return nRet;
}
template <class C>
inline int _Cat(C* pDest, size_t nDest, const C* pSrc) {
  if (pDest == NULL || nDest == 0) return EINVAL;
  size_t nHave = _NLen(pDest, nDest);
  if (nHave == nDest) {  // not terminated
    pDest[0] = 0;
    return EINVAL;
  }
  int nRet = _Copy(pDest + nHave, nDest - nHave, pSrc);
  if (nRet != 0) pDest[0] = 0;
  return nRet;
}
template <class C>
inline int _NCat(C* pDest, size_t nDest, const C* pSrc, size_t nCount) {
  if (pDest == NULL || nDest == 0) return EINVAL;
  size_t nHave = _NLen(pDest, nDest);
  if (nHave == nDest) {
    pDest[0] = 0;
    return EINVAL;
  }
  int nRet = _NCopy(pDest + nHave, nDest - nHave, pSrc, nCount);
  if (nRet != 0 && nRet != STRUNCATE) pDest[0] = 0;
  return nRet;
}
template <class C>
inline int _Case(C* p, size_t n, bool bUpper) {
  if (p == NULL) return EINVAL;
  size_t nLen = _NLen(p, n);
  if (nLen == n) return EINVAL;
  for (size_t i = 0; i < nLen; i++) {
    C c = p[i];
    if (bUpper && c >= 'a' && c <= 'z') p[i] = (C)(c - 'a' + 'A');
    if (!bUpper && c >= 'A' && c <= 'Z') p[i] = (C)(c - 'A' + 'a');
  }
  return 0;
}
// A number in the given radix, as _itoa_s and its relatives write it.
template <class C, class U>
inline int _ToA(U uValue, bool bNegative, C* pDest, size_t nDest, int nRadix) {
  if (pDest == NULL || nDest == 0) return EINVAL;
  if (nRadix < 2 || nRadix > 36) {
    pDest[0] = 0;
    return EINVAL;
  }
  C sz[72];
  size_t n = 0;
  do {
    unsigned d = (unsigned)(uValue % (unsigned)nRadix);
    sz[n++] = (C)(d < 10 ? '0' + d : 'a' + d - 10);
    uValue /= (unsigned)nRadix;
  } while (uValue != 0);
  if (bNegative) sz[n++] = '-';
  if (n >= nDest) {
    pDest[0] = 0;
    return ERANGE;
  }
  for (size_t i = 0; i < n; i++) pDest[i] = sz[n - 1 - i];
  pDest[n] = 0;
  return 0;
}
template <class C, class S>
inline int _SToA(S nValue, C* pDest, size_t nDest, int nRadix) {
  typedef unsigned long long U;
  bool bNeg = nRadix == 10 && nValue < 0;
  U u = bNeg ? (U)0 - (U)(long long)nValue : (U)(long long)nValue;
  if (!bNeg && nValue < 0) u &= (~(U)0) >> (64 - 8 * sizeof(S));  // two's complement digits
  return _ToA(u, bNeg, pDest, nDest, nRadix);
}

}  // namespace _Impl

inline void memcpy_s(void* pDest, size_t nDest, const void* pSrc, size_t nCount) {
  if (nCount == 0) return;
  if (pDest == NULL || pSrc == NULL) AtlCrtErrorCheck(EINVAL);
  if (nCount > nDest) {
    memset(pDest, 0, nDest);
    AtlCrtErrorCheck(ERANGE);
  }
  memcpy(pDest, pSrc, nCount);
}
inline void memmove_s(void* pDest, size_t nDest, const void* pSrc, size_t nCount) {
  if (nCount == 0) return;
  if (pDest == NULL || pSrc == NULL) AtlCrtErrorCheck(EINVAL);
  if (nCount > nDest) AtlCrtErrorCheck(ERANGE);
  memmove(pDest, pSrc, nCount);
}
inline void wmemcpy_s(wchar_t* pDest, size_t nDest, const wchar_t* pSrc, size_t nCount) {
  memcpy_s(pDest, nDest * sizeof(wchar_t), pSrc, nCount * sizeof(wchar_t));
}
inline void wmemmove_s(wchar_t* pDest, size_t nDest, const wchar_t* pSrc, size_t nCount) {
  memmove_s(pDest, nDest * sizeof(wchar_t), pSrc, nCount * sizeof(wchar_t));
}

inline int strcpy_s(char* pDest, size_t nDest, const char* pSrc) {
  return AtlCrtErrorCheck(_Impl::_Copy(pDest, nDest, pSrc));
}
inline int wcscpy_s(wchar_t* pDest, size_t nDest, const wchar_t* pSrc) {
  return AtlCrtErrorCheck(_Impl::_Copy(pDest, nDest, pSrc));
}
inline int mbscpy_s(unsigned char* pDest, size_t nDest, const unsigned char* pSrc) {
  return strcpy_s((char*)pDest, nDest, (const char*)pSrc);
}
inline int strncpy_s(char* pDest, size_t nDest, const char* pSrc, size_t nCount) {
  return AtlCrtErrorCheck(_Impl::_NCopy(pDest, nDest, pSrc, nCount));
}
inline int wcsncpy_s(wchar_t* pDest, size_t nDest, const wchar_t* pSrc, size_t nCount) {
  return AtlCrtErrorCheck(_Impl::_NCopy(pDest, nDest, pSrc, nCount));
}
inline int mbsncpy_s(unsigned char* pDest, size_t nDest, const unsigned char* pSrc,
                      size_t nCount) {
  return strncpy_s((char*)pDest, nDest, (const char*)pSrc, nCount);
}
inline int strcat_s(char* pDest, size_t nDest, const char* pSrc) {
  return AtlCrtErrorCheck(_Impl::_Cat(pDest, nDest, pSrc));
}
inline int wcscat_s(wchar_t* pDest, size_t nDest, const wchar_t* pSrc) {
  return AtlCrtErrorCheck(_Impl::_Cat(pDest, nDest, pSrc));
}
inline int mbscat_s(unsigned char* pDest, size_t nDest, const unsigned char* pSrc) {
  return strcat_s((char*)pDest, nDest, (const char*)pSrc);
}
inline int strncat_s(char* pDest, size_t nDest, const char* pSrc, size_t nCount) {
  return AtlCrtErrorCheck(_Impl::_NCat(pDest, nDest, pSrc, nCount));
}
inline int wcsncat_s(wchar_t* pDest, size_t nDest, const wchar_t* pSrc, size_t nCount) {
  return AtlCrtErrorCheck(_Impl::_NCat(pDest, nDest, pSrc, nCount));
}
inline int mbsncat_s(unsigned char* pDest, size_t nDest, const unsigned char* pSrc,
                      size_t nCount) {
  return strncat_s((char*)pDest, nDest, (const char*)pSrc, nCount);
}
inline int strlwr_s(char* p, size_t n) { return AtlCrtErrorCheck(_Impl::_Case(p, n, false)); }
inline int wcslwr_s(wchar_t* p, size_t n) { return AtlCrtErrorCheck(_Impl::_Case(p, n, false)); }
inline int strupr_s(char* p, size_t n) { return AtlCrtErrorCheck(_Impl::_Case(p, n, true)); }
inline int wcsupr_s(wchar_t* p, size_t n) { return AtlCrtErrorCheck(_Impl::_Case(p, n, true)); }

inline size_t strnlen(const char* p, size_t nMax) { return p ? _Impl::_NLen(p, nMax) : 0; }
inline size_t wcsnlen(const wchar_t* p, size_t nMax) { return p ? _Impl::_NLen(p, nMax) : 0; }

inline int itoa_s(int n, char* p, size_t c, int r) { return AtlCrtErrorCheck(_Impl::_SToA(n, p, c, r)); }
inline int itow_s(int n, wchar_t* p, size_t c, int r) { return AtlCrtErrorCheck(_Impl::_SToA(n, p, c, r)); }
inline int ltoa_s(long n, char* p, size_t c, int r) { return AtlCrtErrorCheck(_Impl::_SToA(n, p, c, r)); }
inline int ltow_s(long n, wchar_t* p, size_t c, int r) { return AtlCrtErrorCheck(_Impl::_SToA(n, p, c, r)); }
inline int ultoa_s(unsigned long n, char* p, size_t c, int r) {
  return AtlCrtErrorCheck(_Impl::_ToA((unsigned long long)n, false, p, c, r));
}
inline int ultow_s(unsigned long n, wchar_t* p, size_t c, int r) {
  return AtlCrtErrorCheck(_Impl::_ToA((unsigned long long)n, false, p, c, r));
}
inline int i64toa_s(long long n, char* p, size_t c, int r) {
  return AtlCrtErrorCheck(_Impl::_SToA(n, p, c, r));
}
inline int i64tow_s(long long n, wchar_t* p, size_t c, int r) {
  return AtlCrtErrorCheck(_Impl::_SToA(n, p, c, r));
}
inline int ui64toa_s(unsigned long long n, char* p, size_t c, int r) {
  return AtlCrtErrorCheck(_Impl::_ToA(n, false, p, c, r));
}
inline int ui64tow_s(unsigned long long n, wchar_t* p, size_t c, int r) {
  return AtlCrtErrorCheck(_Impl::_ToA(n, false, p, c, r));
}

inline int get_errno() { return errno; }
inline void set_errno(int nError) { errno = nError; }

// The TCHAR forms.
#ifdef _UNICODE
inline int tcscpy_s(wchar_t* d, size_t n, const wchar_t* s) { return wcscpy_s(d, n, s); }
inline int tcsncpy_s(wchar_t* d, size_t n, const wchar_t* s, size_t c) { return wcsncpy_s(d, n, s, c); }
inline int tcscat_s(wchar_t* d, size_t n, const wchar_t* s) { return wcscat_s(d, n, s); }
inline int tcsncat_s(wchar_t* d, size_t n, const wchar_t* s, size_t c) { return wcsncat_s(d, n, s, c); }
inline int tcslwr_s(wchar_t* p, size_t n) { return wcslwr_s(p, n); }
inline int tcsupr_s(wchar_t* p, size_t n) { return wcsupr_s(p, n); }
inline size_t tcsnlen(const wchar_t* p, size_t n) { return wcsnlen(p, n); }
inline int itot_s(int v, wchar_t* p, size_t c, int r) { return itow_s(v, p, c, r); }
inline int ltot_s(long v, wchar_t* p, size_t c, int r) { return ltow_s(v, p, c, r); }
inline int ultot_s(unsigned long v, wchar_t* p, size_t c, int r) { return ultow_s(v, p, c, r); }
inline int i64tot_s(long long v, wchar_t* p, size_t c, int r) { return i64tow_s(v, p, c, r); }
inline int ui64tot_s(unsigned long long v, wchar_t* p, size_t c, int r) { return ui64tow_s(v, p, c, r); }
#else
inline int tcscpy_s(char* d, size_t n, const char* s) { return strcpy_s(d, n, s); }
inline int tcsncpy_s(char* d, size_t n, const char* s, size_t c) { return strncpy_s(d, n, s, c); }
inline int tcscat_s(char* d, size_t n, const char* s) { return strcat_s(d, n, s); }
inline int tcsncat_s(char* d, size_t n, const char* s, size_t c) { return strncat_s(d, n, s, c); }
inline int tcslwr_s(char* p, size_t n) { return strlwr_s(p, n); }
inline int tcsupr_s(char* p, size_t n) { return strupr_s(p, n); }
inline size_t tcsnlen(const char* p, size_t n) { return strnlen(p, n); }
inline int itot_s(int v, char* p, size_t c, int r) { return itoa_s(v, p, c, r); }
inline int ltot_s(long v, char* p, size_t c, int r) { return ltoa_s(v, p, c, r); }
inline int ultot_s(unsigned long v, char* p, size_t c, int r) { return ultoa_s(v, p, c, r); }
inline int i64tot_s(long long v, char* p, size_t c, int r) { return i64toa_s(v, p, c, r); }
inline int ui64tot_s(unsigned long long v, char* p, size_t c, int r) { return ui64toa_s(v, p, c, r); }
#endif

}  // namespace Checked
}  // namespace ATL

#endif  // __ATLCHECKED_H__
