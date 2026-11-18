// atlexcept.h - ATL's errors as exceptions: CAtlException, AtlThrow and
// AtlThrowLastWin32.
//
// In a program without MFC, AtlThrow throws a CAtlException with the
// HRESULT. With MFC (afx.h included first) it throws MFC's exceptions
// instead, as code that catches CException* expects: a CMemoryException
// for E_OUTOFMEMORY, a CInvalidArgException for E_INVALIDARG, a
// COleException for the rest. Define AtlThrow before including ATL's
// headers to throw something else.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#ifndef __ATLEXCEPT_H__
#define __ATLEXCEPT_H__

#include <windows.h>

#ifdef __AFX_H__
void __stdcall AfxThrowOleException(LONG sc);
#endif

namespace ATL {

class CAtlException {
public:
  HRESULT m_hr;

  CAtlException() : m_hr(E_FAIL) {}
  CAtlException(HRESULT hr) : m_hr(hr) {}
  operator HRESULT() const { return m_hr; }
};

#ifndef AtlThrow
#define AtlThrow ATL::AtlThrowImpl
#ifdef __AFX_H__
[[noreturn]] inline void AtlThrowImpl(HRESULT hr) {
  if (hr == E_OUTOFMEMORY) AfxThrowMemoryException();
  if (hr == E_INVALIDARG) AfxThrowInvalidArgException();
  AfxThrowOleException(hr);
  throw CAtlException(hr);  // (not reached)
}
#else
[[noreturn]] inline void AtlThrowImpl(HRESULT hr) { throw CAtlException(hr); }
#endif
#endif

// The calling thread's last error, as an HRESULT.
inline HRESULT AtlHresultFromLastError() {
  DWORD dw = ::GetLastError();
  return dw == 0 ? E_FAIL : HRESULT_FROM_WIN32(dw);
}
inline HRESULT AtlHresultFromWin32(DWORD dw) { return HRESULT_FROM_WIN32(dw); }

[[noreturn]] inline void AtlThrowLastWin32() {
  AtlThrow(AtlHresultFromLastError());
  throw CAtlException(E_FAIL);  // (not reached when AtlThrow throws)
}

}  // namespace ATL

#endif  // __ATLEXCEPT_H__
