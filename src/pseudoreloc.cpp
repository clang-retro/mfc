// pseudoreloc.cpp - fixes a module's references to data in DLLs, for C
// runtimes whose startup does not (picolibc). Part of libmfcs.a.
//
// A program that uses mfc.dll refers to the DLL's data, such as the
// CRuntimeClass of a base class, without __declspec(dllimport). The
// linker then makes such a reference point at the import address table,
// and lists it ("runtime pseudo relocations"): once Windows has filled
// the table, each listed place gets the difference between the address
// the table holds and its own. The MinGW runtime does that before the
// constructors run, in _pei386_runtime_relocator. picolibc has no such
// function, so afx.h asks the linker for this one there, and a
// constructor that runs before the others calls it.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include <windows.h>
#include <string.h>

#if defined(_AFXDLL) && !defined(_AFX_BUILDING_DLL)

extern "C" char __RUNTIME_PSEUDO_RELOC_LIST__[];
extern "C" char __RUNTIME_PSEUDO_RELOC_LIST_END__[];
extern "C" IMAGE_DOS_HEADER __ImageBase;

namespace {

struct Header {  // version 2 of the list, the one lld writes
  DWORD dwZero1;
  DWORD dwZero2;
  DWORD dwVersion;
};

struct Entry {
  DWORD dwSymbol;  // the RVA of the import address table entry
  DWORD dwTarget;  // the RVA of the place that refers to it
  DWORD dwFlags;   // the low byte: the size of the place in bits
};

// Writes to a place that may be in a read-only section.
void Write(BYTE* pTarget, const void* pData, size_t nSize) {
  MEMORY_BASIC_INFORMATION mbi;
  if (::VirtualQuery(pTarget, &mbi, sizeof(mbi)) == 0) return;
  const DWORD dwWritable =
      PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
  DWORD dwOld = 0;
  BOOL bProtect = (mbi.Protect & dwWritable) == 0;
  if (bProtect && !::VirtualProtect(mbi.BaseAddress, mbi.RegionSize, PAGE_EXECUTE_READWRITE, &dwOld))
    return;
  memcpy(pTarget, pData, nSize);
  if (bProtect) ::VirtualProtect(mbi.BaseAddress, mbi.RegionSize, dwOld, &dwOld);
}

}  // namespace

extern "C" void _pei386_runtime_relocator(void) {
  static BOOL bDone;
  if (bDone) return;
  bDone = TRUE;
  BYTE* pBase = (BYTE*)&__ImageBase;
  BYTE* p = (BYTE*)__RUNTIME_PSEUDO_RELOC_LIST__;
  BYTE* pEnd = (BYTE*)__RUNTIME_PSEUDO_RELOC_LIST_END__;
  if (pEnd - p < (ptrdiff_t)sizeof(Header)) return;
  const Header* pHeader = (const Header*)p;
  if (pHeader->dwZero1 != 0 || pHeader->dwZero2 != 0 || pHeader->dwVersion != 1) return;
  for (const Entry* e = (const Entry*)(pHeader + 1); (BYTE*)(e + 1) <= pEnd; e++) {
    BYTE* pTarget = pBase + e->dwTarget;
    ULONG_PTR nSlot = (ULONG_PTR)(pBase + e->dwSymbol);
    ULONG_PTR nAddress = *(ULONG_PTR*)nSlot;
    switch (e->dwFlags & 0xff) {
      case 8: {
        BYTE b = (BYTE)(*pTarget - nSlot + nAddress);
        Write(pTarget, &b, 1);
        break;
      }
      case 16: {
        WORD w;
        memcpy(&w, pTarget, 2);
        w = (WORD)(w - nSlot + nAddress);
        Write(pTarget, &w, 2);
        break;
      }
      case 32: {
        DWORD dw;
        memcpy(&dw, pTarget, 4);
        dw = (DWORD)(dw - nSlot + nAddress);
        Write(pTarget, &dw, 4);
        break;
      }
#ifdef _WIN64
      case 64: {
        ULONGLONG ull;
        memcpy(&ull, pTarget, 8);
        ull = ull - nSlot + nAddress;
        Write(pTarget, &ull, 8);
        break;
      }
#endif
    }
  }
}

// Before the module's other constructors, which may use the data.
__attribute__((constructor(101))) static void RelocateEarly() { _pei386_runtime_relocator(); }

#endif
