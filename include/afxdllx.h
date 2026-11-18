// afxdllx.h - included by one source file of an MFC extension DLL, as
// AppWizard writes them, after afxwin.h and before its DllMain.
//
// With Visual C++ it supplies a raw DllMain hook that switches module
// state while the DLL's static objects are made and destroyed. Here
// AfxInitExtensionModule and CDynLinkLibrary do all an extension DLL
// needs, so the header only checks that it is used as intended.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#ifndef __AFXDLLX_H__
#define __AFXDLLX_H__

#ifndef __AFXWIN_H__
#error include afxwin.h before afxdllx.h
#endif
#ifndef _AFXDLL
#error afxdllx.h is for extension DLLs, which are built with _AFXDLL
#endif

#endif  // __AFXDLLX_H__
