// CString storage and printf-style formatting with Microsoft's rules.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include "stdafx.h"

// ---------------------------------------------------------------------
// String data

namespace {
// The shared empty string: a locked header and room for one zero char of
// either width.
union NilString {
  struct {
    CStringData hdr;
    WCHAR zero[2];
  } s;
  double align;
};
NilString g_nil = {{{-1, 0, 0}, {0, 0}}};
}  // namespace

CStringData* AFXAPI AfxStrNil() { return &g_nil.s.hdr; }

CStringData* AFXAPI AfxStrAlloc(int nChars, int nCharSize) {
  if (nChars < 0) AfxThrowInvalidArgException();
  // Round up so that small changes do not reallocate every time.
  int nAlloc = nChars < 64 ? (nChars + 16) & ~15 : (nChars + 64) & ~63;
  CStringData* p =
      (CStringData*)malloc(sizeof(CStringData) + (size_t)(nAlloc + 1) * nCharSize);
  if (!p) AfxThrowMemoryException();
  p->nRefs = 1;
  p->nDataLength = 0;
  p->nAllocLength = nAlloc;
  memset(p->data(), 0, nCharSize);
  return p;
}

void AFXAPI AfxStrRelease(CStringData* p) {
  if (p == &g_nil.s.hdr) return;
  if (p->nRefs < 0) {  // locked buffer: only one owner
    free(p);
    return;
  }
  if (InterlockedDecrement(&p->nRefs) <= 0) free(p);
}

// ---------------------------------------------------------------------
// Formatting

#include <atlstrfmt_.h>


int AFX_CDECL AfxVFormatA(char* pszBuf, int nMax, const char* pszFormat, va_list args) {
  return _AtlStrFmt::VFormat<char>(pszBuf, nMax, pszFormat, args);
}

int AFX_CDECL AfxVFormatW(WCHAR* pszBuf, int nMax, const WCHAR* pszFormat, va_list args) {
  return _AtlStrFmt::VFormat<WCHAR>(pszBuf, nMax, pszFormat, args);
}

// ---------------------------------------------------------------------
// Helpers on strings

BOOL AFXAPI AfxExtractSubString(CString& rString, LPCTSTR lpszFullString,
                                int iSubString, TCHAR chSep) {
  if (lpszFullString == NULL) return FALSE;
  while (iSubString--) {
    lpszFullString = _tcschr(lpszFullString, chSep);
    if (lpszFullString == NULL) {
      rString.Empty();
      return FALSE;
    }
    lpszFullString++;
  }
  LPCTSTR lpchEnd = _tcschr(lpszFullString, chSep);
  int nLen = lpchEnd == NULL ? lstrlen(lpszFullString) : (int)(lpchEnd - lpszFullString);
  rString = CString(lpszFullString, nLen);
  return TRUE;
}
