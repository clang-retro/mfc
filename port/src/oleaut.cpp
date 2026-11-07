// oleaut.cpp - OLE Automation strings (BSTRs).
//
// A BSTR points at its characters, which end in a zero; the 32-bit
// length in bytes is just before them.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include "winport.h"

#include <stdlib.h>
#include <string.h>

namespace {

const size_t kHeader = 16;  // keeps the characters aligned; the length is last

BSTR AllocBytes(const void* data, UINT bytes) {
  // Room for a zero OLECHAR after the bytes, however many there are.
  char* p = (char*)malloc(kHeader + bytes + sizeof(OLECHAR) * 2);
  if (!p) return NULL;
  char* s = p + kHeader;
  *(UINT*)(s - sizeof(UINT)) = bytes;
  if (data) memcpy(s, data, bytes);
  else memset(s, 0, bytes);
  memset(s + bytes, 0, sizeof(OLECHAR) * 2);
  return (BSTR)s;
}

}  // namespace

extern "C" {

BSTR WINAPI SysAllocStringLen(const OLECHAR* s, UINT n) {
  if (n > 0x7fffffff / sizeof(OLECHAR)) return NULL;
  return AllocBytes(s, n * (UINT)sizeof(OLECHAR));
}

BSTR WINAPI SysAllocString(const OLECHAR* s) {
  if (!s) return NULL;
  return SysAllocStringLen(s, (UINT)wcslen(s));
}

BSTR WINAPI SysAllocStringByteLen(LPCSTR s, UINT n) { return AllocBytes(s, n); }

void WINAPI SysFreeString(BSTR s) {
  if (s) free((char*)s - kHeader);
}

UINT WINAPI SysStringByteLen(BSTR s) { return s ? *(const UINT*)((const char*)s - sizeof(UINT)) : 0; }
UINT WINAPI SysStringLen(BSTR s) { return SysStringByteLen(s) / (UINT)sizeof(OLECHAR); }

INT WINAPI SysReAllocStringLen(BSTR* p, const OLECHAR* s, UINT n) {
  BSTR b = SysAllocStringLen(s, n);
  if (!b) return FALSE;
  SysFreeString(*p);
  *p = b;
  return TRUE;
}

INT WINAPI SysReAllocString(BSTR* p, const OLECHAR* s) {
  return SysReAllocStringLen(p, s, s ? (UINT)wcslen(s) : 0);
}

}  // extern "C"
