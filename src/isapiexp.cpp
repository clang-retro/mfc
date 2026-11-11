// isapiexp.cpp - the functions an Internet Server API DLL exports, which
// call its one CHttpServer or CHttpFilter. They are in a file of their
// own so that a DLL links them only when its .def file exports them.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include "afximpl.h"
#include <afxisapi.h>

#ifndef _AFX_BUILDING_DLL  // not in mfc.dll: an ISAPI DLL is a regular DLL, which uses the static library

extern CHttpServer* _afxIsapiServer;
extern CHttpFilter* _afxIsapiFilter;

extern "C" {

BOOL WINAPI GetExtensionVersion(HSE_VERSION_INFO* pVer) {
  return _afxIsapiServer != NULL && _afxIsapiServer->GetExtensionVersion(pVer);
}

DWORD WINAPI HttpExtensionProc(EXTENSION_CONTROL_BLOCK* pECB) {
  if (_afxIsapiServer == NULL) return HSE_STATUS_ERROR;
  return _afxIsapiServer->HttpExtensionProc(pECB);
}

BOOL WINAPI TerminateExtension(DWORD dwFlags) {
  return _afxIsapiServer == NULL || _afxIsapiServer->TerminateExtension(dwFlags);
}

BOOL WINAPI GetFilterVersion(PHTTP_FILTER_VERSION pVer) {
  return _afxIsapiFilter != NULL && _afxIsapiFilter->GetFilterVersion(pVer);
}

DWORD WINAPI HttpFilterProc(PHTTP_FILTER_CONTEXT pfc, DWORD dwNotificationType,
                            LPVOID pvNotification) {
  if (_afxIsapiFilter == NULL) return SF_STATUS_REQ_NEXT_NOTIFICATION;
  return _afxIsapiFilter->HttpFilterProc(pfc, dwNotificationType, pvNotification);
}

BOOL WINAPI TerminateFilter(DWORD dwFlags) {
  return _afxIsapiFilter == NULL || _afxIsapiFilter->TerminateFilter(dwFlags);
}

}  // extern "C"

#endif  // _AFX_BUILDING_DLL
