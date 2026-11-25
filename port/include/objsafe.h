// objsafe.h - IObjectSafety: whether a control is safe for scripts to
// call and to be initialized from untrusted data.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#ifndef _WINPORT_OBJSAFE_H
#define _WINPORT_OBJSAFE_H

#include <comcat.h>

#define INTERFACESAFE_FOR_UNTRUSTED_CALLER 0x00000001
#define INTERFACESAFE_FOR_UNTRUSTED_DATA 0x00000002
#define INTERFACE_USES_DISPEX 0x00000004
#define INTERFACE_USES_SECURITY_MANAGER 0x00000008

__WINPORT_EXTERN_C const IID IID_IObjectSafety;

// Like mingw-w64's, no __uuidof here: atlcom.h names it.
#ifdef __cplusplus
interface IObjectSafety : public IUnknown {
  STDMETHOD(GetInterfaceSafetyOptions)(REFIID riid, DWORD* pdwSupportedOptions,
                                       DWORD* pdwEnabledOptions) = 0;
  STDMETHOD(SetInterfaceSafetyOptions)(REFIID riid, DWORD dwOptionSetMask, DWORD dwEnabledOptions) = 0;
};
#else
typedef struct IObjectSafety IObjectSafety;
#endif
typedef IObjectSafety* LPOBJECTSAFETY;

#endif
