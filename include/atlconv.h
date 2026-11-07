// atlconv.h - string conversion between ANSI (or another code page) and
// UTF-16: the classes (CA2W, CW2A, CT2A, ...) and the older macros that
// work with USES_CONVERSION (A2W, T2A, OLE2T, ...).
//
// Part of clang-retro MFC, an independent implementation of the MFC API
// SPDX-License-Identifier: AGPL-3.0-only
// written from its public documentation.
#ifndef __ATLCONV_H__
#define __ATLCONV_H__

#include <windows.h>
#include <string.h>

#ifndef _ATL_CONVERSION_DEFINED
#define _ATL_CONVERSION_DEFINED

// ---------------------------------------------------------------------
// Classes. Short strings convert into a buffer inside the object; longer
// ones into the heap. The result lives as long as the object.

template <int t_nBufferLength = 128>
class CA2WEX {
public:
  CA2WEX(LPCSTR psz) : m_psz(m_szBuffer) { Init(psz, CP_ACP); }
  CA2WEX(LPCSTR psz, UINT nCodePage) : m_psz(m_szBuffer) { Init(psz, nCodePage); }
  ~CA2WEX() {
    if (m_psz != m_szBuffer) delete[] m_psz;
  }
  operator LPWSTR() const { return m_psz; }
  LPWSTR m_psz;
  wchar_t m_szBuffer[t_nBufferLength];

private:
  void Init(LPCSTR psz, UINT nCodePage) {
    if (psz == NULL) {
      m_psz = NULL;
      return;
    }
    int nLength = ::MultiByteToWideChar(nCodePage, 0, psz, -1, NULL, 0);
    if (nLength > t_nBufferLength) m_psz = new wchar_t[nLength];
    if (nLength == 0 || ::MultiByteToWideChar(nCodePage, 0, psz, -1, m_psz, nLength) == 0)
      m_psz[0] = 0;
  }
  CA2WEX(const CA2WEX&);
  CA2WEX& operator=(const CA2WEX&);
};

template <int t_nBufferLength = 128>
class CW2AEX {
public:
  CW2AEX(LPCWSTR psz) : m_psz(m_szBuffer) { Init(psz, CP_ACP); }
  CW2AEX(LPCWSTR psz, UINT nCodePage) : m_psz(m_szBuffer) { Init(psz, nCodePage); }
  ~CW2AEX() {
    if (m_psz != m_szBuffer) delete[] m_psz;
  }
  operator LPSTR() const { return m_psz; }
  LPSTR m_psz;
  char m_szBuffer[t_nBufferLength];

private:
  void Init(LPCWSTR psz, UINT nCodePage) {
    if (psz == NULL) {
      m_psz = NULL;
      return;
    }
    int nLength = ::WideCharToMultiByte(nCodePage, 0, psz, -1, NULL, 0, NULL, NULL);
    if (nLength > t_nBufferLength) m_psz = new char[nLength];
    if (nLength == 0 ||
        ::WideCharToMultiByte(nCodePage, 0, psz, -1, m_psz, nLength, NULL, NULL) == 0)
      m_psz[0] = 0;
  }
  CW2AEX(const CW2AEX&);
  CW2AEX& operator=(const CW2AEX&);
};

// No conversion: the same string, for the T forms that match.
template <int t_nBufferLength = 128>
class CA2CAEX {
public:
  CA2CAEX(LPCSTR psz) : m_psz(psz) {}
  CA2CAEX(LPCSTR psz, UINT) : m_psz(psz) {}
  operator LPCSTR() const { return m_psz; }
  LPCSTR m_psz;
};

template <int t_nBufferLength = 128>
class CW2CWEX {
public:
  CW2CWEX(LPCWSTR psz) : m_psz(psz) {}
  CW2CWEX(LPCWSTR psz, UINT) : m_psz(psz) {}
  operator LPCWSTR() const { return m_psz; }
  LPCWSTR m_psz;
};

template <int t_nBufferLength = 128>
class CA2AEX {
public:
  CA2AEX(LPCSTR psz) : m_psz((LPSTR)psz) {}
  CA2AEX(LPCSTR psz, UINT) : m_psz((LPSTR)psz) {}
  operator LPSTR() const { return m_psz; }
  LPSTR m_psz;
};

template <int t_nBufferLength = 128>
class CW2WEX {
public:
  CW2WEX(LPCWSTR psz) : m_psz((LPWSTR)psz) {}
  CW2WEX(LPCWSTR psz, UINT) : m_psz((LPWSTR)psz) {}
  operator LPWSTR() const { return m_psz; }
  LPWSTR m_psz;
};

typedef CA2WEX<> CA2W;
typedef CW2AEX<> CW2A;
typedef CA2CAEX<> CA2CA;
typedef CW2CWEX<> CW2CW;
typedef CA2AEX<> CA2A;
typedef CW2WEX<> CW2W;
typedef CA2WEX<> CA2CW;
typedef CW2AEX<> CW2CA;

#ifdef _UNICODE
typedef CA2W CA2T;
typedef CW2A CT2A;
typedef CA2W CA2CT;
typedef CW2A CT2CA;
typedef CW2W CW2T;
typedef CW2W CT2W;
typedef CW2CW CW2CT;
typedef CW2CW CT2CW;
#else
typedef CA2A CA2T;
typedef CA2A CT2A;
typedef CA2CA CA2CT;
typedef CA2CA CT2CA;
typedef CW2A CW2T;
typedef CA2W CT2W;
typedef CW2A CW2CT;
typedef CA2W CT2CW;
#endif
#ifdef _UNICODE
typedef CW2W COLE2T;
typedef CW2CW COLE2CT;
typedef CW2W CT2OLE;
typedef CW2CW CT2COLE;
#else
typedef CW2A COLE2T;
typedef CW2A COLE2CT;
typedef CA2W CT2OLE;
typedef CA2W CT2COLE;
#endif

// ---------------------------------------------------------------------
// The macros. USES_CONVERSION declares the locals they use; each result
// is on the stack and lives until the function returns.

inline LPWSTR WINAPI AtlA2WHelper(LPWSTR lpw, LPCSTR lpa, int nChars, UINT acp) {
  lpw[0] = 0;
  ::MultiByteToWideChar(acp, 0, lpa, -1, lpw, nChars);
  return lpw;
}

inline LPSTR WINAPI AtlW2AHelper(LPSTR lpa, LPCWSTR lpw, int nChars, UINT acp) {
  lpa[0] = 0;
  ::WideCharToMultiByte(acp, 0, lpw, -1, lpa, nChars, NULL, NULL);
  return lpa;
}

#define USES_CONVERSION \
  int _convert = 0;     \
  (void)_convert;       \
  UINT _acp = CP_ACP;   \
  (void)_acp;           \
  LPCWSTR _lpw = NULL;  \
  (void)_lpw;           \
  LPCSTR _lpa = NULL;   \
  (void)_lpa
#define USES_CONVERSION_EX USES_CONVERSION

#define A2W(lpa)                                                                          \
  (((_lpa = (lpa)) == NULL)                                                               \
       ? NULL                                                                             \
       : (_convert = (lstrlenA(_lpa) + 1),                                                \
          AtlA2WHelper((LPWSTR)__builtin_alloca(_convert * sizeof(WCHAR)), _lpa, _convert, \
                       _acp)))
// Up to four bytes per character (UTF-8 and the double-byte code pages).
#define W2A(lpw)                                                                         \
  (((_lpw = (lpw)) == NULL)                                                              \
       ? NULL                                                                            \
       : (_convert = (lstrlenW(_lpw) + 1) * 4,                                           \
          AtlW2AHelper((LPSTR)__builtin_alloca(_convert), _lpw, _convert, _acp)))
#define A2CW(lpa) ((LPCWSTR)A2W(lpa))
#define W2CA(lpw) ((LPCSTR)W2A(lpw))

#ifdef _UNICODE
#define T2A W2A
#define A2T A2W
#define T2W(lp) ((LPWSTR)(lp))
#define W2T(lp) ((LPWSTR)(lp))
#define T2CA W2CA
#define A2CT A2CW
#define T2CW(lp) ((LPCWSTR)(lp))
#define W2CT(lp) ((LPCWSTR)(lp))
#else
#define T2W A2W
#define W2T W2A
#define T2A(lp) ((LPSTR)(lp))
#define A2T(lp) ((LPSTR)(lp))
#define T2CW A2CW
#define W2CT W2CA
#define T2CA(lp) ((LPCSTR)(lp))
#define A2CT(lp) ((LPCSTR)(lp))
#endif

// OLE strings are UTF-16.
#define OLE2A W2A
#define A2OLE A2W
#define OLE2CA W2CA
#define A2COLE A2CW
#define OLE2W(lp) ((LPWSTR)(lp))
#define W2OLE(lp) ((LPOLESTR)(lp))
#define OLE2CW(lp) ((LPCWSTR)(lp))
#define W2COLE(lp) ((LPCOLESTR)(lp))
#define OLE2T W2T
#define T2OLE T2W
#define OLE2CT W2CT
#define T2COLE T2CW

#define A2BSTR(lpa) ::SysAllocString(A2CW(lpa))
#define W2BSTR(lpw) ::SysAllocString(lpw)
#define OLE2BSTR(lp) ::SysAllocString(lp)
#ifdef _UNICODE
#define T2BSTR W2BSTR
#else
#define T2BSTR A2BSTR
#endif

#endif  // _ATL_CONVERSION_DEFINED
#endif  // __ATLCONV_H__
