// dllstub.cpp - what each module that uses mfc.dll keeps for itself
// (libmfcs.a, with appmain.cpp and pseudoreloc.cpp).
//
// The global operator delete: a block made by DEBUG_NEW is tracked by the
// DLL, so every module's delete goes there (see memdebug.cpp), which frees
// the rest as the C runtime does. afx.h makes the linker take this file
// (_afxDllStub), so that this delete is linked before the C++ runtime's
// where that one is weak (libc++). Where it is not (libstdc++), this one
// gives way, and the DLL is told, so that it tracks only objects.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include "afximpl.h"
#include <new>

#if defined(_AFXDLL) && !defined(_AFX_BUILDING_DLL)

extern "C" void AFXAPI _AfxDllDelete(void* p);
extern "C" void AFXAPI _AfxNoteForeignDelete();

extern "C" int _afxDllStub = 0;

#ifdef _LIBCPP_VERSION
#define _AFX_DELETE_LINKAGE
#else
#define _AFX_DELETE_LINKAGE __attribute__((weak))
#endif

static void* volatile g_pProbe;  // the test pointer of the check below

static void ModuleDelete(void* p) noexcept {
  if (p == NULL) return;
  if (p == g_pProbe) g_pProbe = NULL;  // this delete is the one linked
  _AfxDllDelete(p);
}

_AFX_DELETE_LINKAGE void operator delete(void* p) noexcept { ModuleDelete(p); }
_AFX_DELETE_LINKAGE void operator delete[](void* p) noexcept { ModuleDelete(p); }
_AFX_DELETE_LINKAGE void operator delete(void* p, size_t) noexcept { ModuleDelete(p); }
_AFX_DELETE_LINKAGE void operator delete[](void* p, size_t) noexcept { ModuleDelete(p); }
_AFX_DELETE_LINKAGE void operator delete(void* p, const std::nothrow_t&) noexcept {
  ModuleDelete(p);
}
_AFX_DELETE_LINKAGE void operator delete[](void* p, const std::nothrow_t&) noexcept {
  ModuleDelete(p);
}

// Whether this module's delete is the one above.
__attribute__((constructor)) static void CheckDelete() {
  void* p = ::malloc(1);
  g_pProbe = p;
  ::operator delete(p);
  if (g_pProbe != NULL) _AfxNoteForeignDelete();
  g_pProbe = NULL;
}

#endif
