// aclapi.h - the security of named and open objects: files (from and to
// their owner, group and mode bits), registry keys and kernel objects.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#ifndef _WINPORT_ACLAPI_H
#define _WINPORT_ACLAPI_H

#include <windows.h>
#include <accctrl.h>

#ifdef __cplusplus
extern "C" {
#endif

DWORD WINAPI GetSecurityInfo(HANDLE, SE_OBJECT_TYPE, SECURITY_INFORMATION, PSID*, PSID*, PACL*, PACL*,
                             PSECURITY_DESCRIPTOR*);
DWORD WINAPI SetSecurityInfo(HANDLE, SE_OBJECT_TYPE, SECURITY_INFORMATION, PSID, PSID, PACL, PACL);
DWORD WINAPI GetNamedSecurityInfoA(LPCSTR, SE_OBJECT_TYPE, SECURITY_INFORMATION, PSID*, PSID*, PACL*, PACL*,
                                   PSECURITY_DESCRIPTOR*);
DWORD WINAPI GetNamedSecurityInfoW(LPCWSTR, SE_OBJECT_TYPE, SECURITY_INFORMATION, PSID*, PSID*, PACL*, PACL*,
                                   PSECURITY_DESCRIPTOR*);
DWORD WINAPI SetNamedSecurityInfoA(LPSTR, SE_OBJECT_TYPE, SECURITY_INFORMATION, PSID, PSID, PACL, PACL);
DWORD WINAPI SetNamedSecurityInfoW(LPWSTR, SE_OBJECT_TYPE, SECURITY_INFORMATION, PSID, PSID, PACL, PACL);
DWORD WINAPI SetEntriesInAclA(ULONG, PEXPLICIT_ACCESS_A, PACL, PACL*);
DWORD WINAPI SetEntriesInAclW(ULONG, PEXPLICIT_ACCESS_W, PACL, PACL*);
void WINAPI BuildTrusteeWithSidA(PTRUSTEE_A, PSID);
void WINAPI BuildTrusteeWithSidW(PTRUSTEE_W, PSID);
void WINAPI BuildTrusteeWithNameA(PTRUSTEE_A, LPSTR);
void WINAPI BuildTrusteeWithNameW(PTRUSTEE_W, LPWSTR);

#ifdef __cplusplus
}  // extern "C"
#endif

#define GetNamedSecurityInfo __WINPORT_AW(GetNamedSecurityInfo)
#define SetNamedSecurityInfo __WINPORT_AW(SetNamedSecurityInfo)
#define SetEntriesInAcl __WINPORT_AW(SetEntriesInAcl)
#define BuildTrusteeWithSid __WINPORT_AW(BuildTrusteeWithSid)
#define BuildTrusteeWithName __WINPORT_AW(BuildTrusteeWithName)

#endif
