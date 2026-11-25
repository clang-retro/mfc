// com_wire.cpp - the strings of the wire form (com_wire.h), which the
// proxies (com_proxy.cpp) and the structured storage (storage.cpp) both
// write and read.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include "com_wire.h"

#include <oleauto.h>

namespace winport {
namespace com {

// Writes a BSTR: its byte length and bytes, or 0xffffffff for NULL.
void Writer::Bstr(BSTR s) {
  if (s == NULL) {
    U32(0xffffffffu);
    return;
  }
  Bytes(s, SysStringByteLen(s));
}

// Reads a string that Writer wrote; *isNull says if it was NULL.
std::wstring Reader::Str(bool* isNull) {
  uint32_t k = U32();
  if (isNull) *isNull = k == 0xffffffffu;
  if (k == 0xffffffffu || bad) return std::wstring();
  if (n - pos < k) {
    bad = true;
    return std::wstring();
  }
  std::wstring s((const OLECHAR*)(p + pos), k / sizeof(OLECHAR));
  pos += k;
  return s;
}

// Reads a string into task memory (CoTaskMemAlloc), or gives NULL.
LPOLESTR Reader::TaskStr() {
  bool isNull;
  std::wstring s = Str(&isNull);
  if (isNull || bad) return NULL;
  LPOLESTR out = (LPOLESTR)CoTaskMemAlloc((s.size() + 1) * sizeof(OLECHAR));
  if (out) memcpy(out, s.c_str(), (s.size() + 1) * sizeof(OLECHAR));
  return out;
}

// Reads a BSTR that Writer::Bstr wrote, or gives NULL.
BSTR Reader::Bstr() {
  uint32_t k = U32();
  if (k == 0xffffffffu || bad) return NULL;
  if (n - pos < k) {
    bad = true;
    return NULL;
  }
  BSTR s = SysAllocStringByteLen((LPCSTR)(p + pos), k);
  pos += k;
  return s;
}

}  // namespace com
}  // namespace winport
