// sddl.h - security descriptors and identifiers as strings
// ("O:BAG:SYD:(A;;GA;;;WD)", "S-1-5-32-544").
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#ifndef _WINPORT_SDDL_H
#define _WINPORT_SDDL_H

#include <windows.h>

#define SDDL_REVISION_1 1
#define SDDL_REVISION SDDL_REVISION_1

extern "C" {

BOOL WINAPI ConvertSidToStringSidA(PSID, LPSTR*);
BOOL WINAPI ConvertSidToStringSidW(PSID, LPWSTR*);
BOOL WINAPI ConvertStringSidToSidA(LPCSTR, PSID*);
BOOL WINAPI ConvertStringSidToSidW(LPCWSTR, PSID*);
BOOL WINAPI ConvertSecurityDescriptorToStringSecurityDescriptorA(PSECURITY_DESCRIPTOR, DWORD, SECURITY_INFORMATION,
                                                                 LPSTR*, PULONG);
BOOL WINAPI ConvertSecurityDescriptorToStringSecurityDescriptorW(PSECURITY_DESCRIPTOR, DWORD, SECURITY_INFORMATION,
                                                                 LPWSTR*, PULONG);
BOOL WINAPI ConvertStringSecurityDescriptorToSecurityDescriptorA(LPCSTR, DWORD, PSECURITY_DESCRIPTOR*, PULONG);
BOOL WINAPI ConvertStringSecurityDescriptorToSecurityDescriptorW(LPCWSTR, DWORD, PSECURITY_DESCRIPTOR*, PULONG);

}  // extern "C"

#define ConvertSidToStringSid __WINPORT_AW(ConvertSidToStringSid)
#define ConvertStringSidToSid __WINPORT_AW(ConvertStringSidToSid)
#define ConvertSecurityDescriptorToStringSecurityDescriptor \
  __WINPORT_AW(ConvertSecurityDescriptorToStringSecurityDescriptor)
#define ConvertStringSecurityDescriptorToSecurityDescriptor \
  __WINPORT_AW(ConvertStringSecurityDescriptorToSecurityDescriptor)

#endif
