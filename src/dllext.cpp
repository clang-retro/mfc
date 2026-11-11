// dllext.cpp - MFC extension DLLs: AfxInitExtensionModule,
// AfxTermExtensionModule and CDynLinkLibrary.
//
// An extension DLL shares the program's copy of the library (mfc.dll), so
// the classes it registers go into the program's list as the DLL loads.
// CDynLinkLibrary puts the DLL into the list of modules that
// AfxFindResourceHandle searches; AfxTermExtensionModule takes it and its
// classes out again when the DLL unloads.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include "afximpl.h"

static const int CRIT_DYNLINKLIST = 1;  // the lock for the module state's DLL list

// The bytes of a loaded module's image.
static void ModuleRange(HMODULE hModule, const BYTE** ppStart, const BYTE** ppEnd) {
#ifdef _WINPORT
  // A module there is not a PE image, and there are no extension DLLs
  // yet, so no classes belong to one.
  (void)hModule;
  *ppStart = *ppEnd = NULL;
#else
  const BYTE* pBase = (const BYTE*)hModule;
  const IMAGE_DOS_HEADER* pDos = (const IMAGE_DOS_HEADER*)pBase;
  const IMAGE_NT_HEADERS* pNt = (const IMAGE_NT_HEADERS*)(pBase + pDos->e_lfanew);
  *ppStart = pBase;
  *ppEnd = pBase + pNt->OptionalHeader.SizeOfImage;
#endif
}

BOOL AFXAPI AfxInitExtensionModule(AFX_EXTENSION_MODULE& state, HMODULE hModule) {
  if (state.bInitialized) return TRUE;
  state.hModule = hModule;
  state.hResource = hModule;
  state.pFirstSharedClass = NULL;
  state.pFirstSharedFactory = NULL;
  state.bInitialized = TRUE;
  return TRUE;
}

void AFXAPI AfxTermExtensionModule(AFX_EXTENSION_MODULE& state, BOOL bAll) {
  if (!state.bInitialized) return;
  AFX_MODULE_STATE* pModuleState = AfxGetModuleState();
  AfxLockGlobals(CRIT_DYNLINKLIST);
  CDynLinkLibrary* p = pModuleState->m_pFirstDLL;
  while (p != NULL) {
    CDynLinkLibrary* pNext = p->m_pNextDLL;
    if (bAll || p->m_hModule == state.hModule) delete p;
    p = pNext;
  }
  AfxUnlockGlobals(CRIT_DYNLINKLIST);
  if (state.hModule != NULL) {
    const BYTE* pStart;
    const BYTE* pEnd;
    ModuleRange(state.hModule, &pStart, &pEnd);
    _AfxRemoveClasses(pStart, pEnd);
  }
  state.bInitialized = FALSE;
}

IMPLEMENT_DYNAMIC(CDynLinkLibrary, CCmdTarget)

static void AddLibrary(CDynLinkLibrary* pLib) {
  AFX_MODULE_STATE* pModuleState = AfxGetModuleState();
  AfxLockGlobals(CRIT_DYNLINKLIST);
  CDynLinkLibrary** pp = &pModuleState->m_pFirstDLL;
  while (*pp != NULL) pp = &(*pp)->m_pNextDLL;
  *pp = pLib;
  AfxUnlockGlobals(CRIT_DYNLINKLIST);
}

CDynLinkLibrary::CDynLinkLibrary(AFX_EXTENSION_MODULE& state, BOOL bSystem)
    : m_hModule(state.hModule),
      m_hResource(state.hResource),
      m_pFirstSharedClass(state.pFirstSharedClass),
      m_pFirstSharedFactory(state.pFirstSharedFactory),
      m_bSystem(bSystem),
      m_pNextDLL(NULL) {
  AddLibrary(this);
}

CDynLinkLibrary::CDynLinkLibrary(HINSTANCE hModule, HINSTANCE hResource)
    : m_hModule(hModule),
      m_hResource(hResource),
      m_pFirstSharedClass(NULL),
      m_pFirstSharedFactory(NULL),
      m_bSystem(FALSE),
      m_pNextDLL(NULL) {
  AddLibrary(this);
}

CDynLinkLibrary::~CDynLinkLibrary() {
  AFX_MODULE_STATE* pModuleState = AfxGetModuleState();
  AfxLockGlobals(CRIT_DYNLINKLIST);
  for (CDynLinkLibrary** pp = &pModuleState->m_pFirstDLL; *pp != NULL; pp = &(*pp)->m_pNextDLL) {
    if (*pp == this) {
      *pp = m_pNextDLL;
      break;
    }
  }
  AfxUnlockGlobals(CRIT_DYNLINKLIST);
}

void CDynLinkLibrary::AssertValid() const { CCmdTarget::AssertValid(); }

void CDynLinkLibrary::Dump(CDumpContext& dc) const {
  CCmdTarget::Dump(dc);
  dc << "m_hModule = " << (void*)m_hModule << "\n";
  dc << "m_hResource = " << (void*)m_hResource << "\n";
  dc << "m_bSystem = " << m_bSystem << "\n";
}
