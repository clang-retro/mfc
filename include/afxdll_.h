// afxdll_.h - MFC extension DLLs: AFX_EXTENSION_MODULE and
// CDynLinkLibrary. Included by afxwin.h.
//
// An extension DLL is built with _AFXDLL and _AFXEXT and uses the
// program's copy of the shared library (mfc.dll), so its classes, objects
// and resources are part of the program. Its DllMain registers it:
//
//   static AFX_EXTENSION_MODULE extDLL = {NULL, NULL};
//   extern "C" int APIENTRY DllMain(HINSTANCE hInstance, DWORD dwReason, LPVOID) {
//     if (dwReason == DLL_PROCESS_ATTACH) {
//       if (!AfxInitExtensionModule(extDLL, hInstance)) return 0;
//       new CDynLinkLibrary(extDLL);
//     } else if (dwReason == DLL_PROCESS_DETACH) {
//       AfxTermExtensionModule(extDLL);
//     }
//     return 1;
//   }
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#ifndef __AFXDLL__H__
#define __AFXDLL__H__

class COleObjectFactory;

struct AFX_EXTENSION_MODULE {
  BOOL bInitialized;
  HMODULE hModule;
  HMODULE hResource;
  CRuntimeClass* pFirstSharedClass;
  COleObjectFactory* pFirstSharedFactory;
};

BOOL AFXAPI AfxInitExtensionModule(AFX_EXTENSION_MODULE& state, HMODULE hModule);
void AFXAPI AfxTermExtensionModule(AFX_EXTENSION_MODULE& state, BOOL bAll = FALSE);


// One per extension DLL in the program, in a list the library searches
// for resources (AfxFindResourceHandle).
class CDynLinkLibrary : public CCmdTarget {
  DECLARE_DYNAMIC(CDynLinkLibrary)
public:
  explicit CDynLinkLibrary(AFX_EXTENSION_MODULE& state, BOOL bSystem = FALSE);
  CDynLinkLibrary(HINSTANCE hModule, HINSTANCE hResource);
  virtual ~CDynLinkLibrary();

  HMODULE m_hModule;
  HMODULE m_hResource;
  CRuntimeClass* m_pFirstSharedClass;
  COleObjectFactory* m_pFirstSharedFactory;
  BOOL m_bSystem;
  CDynLinkLibrary* m_pNextDLL;

  virtual void AssertValid() const;
  virtual void Dump(CDumpContext& dc) const;
};

#endif  // __AFXDLL__H__
