// servprov.h - for 16-bit Windows: IServiceProvider, which 16-bit OLE 2
// does not have. MFC's OLE classes name it. The IID has the value of
// Win32 (src/win16ole.cpp).
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <ole2.h>

extern "C" const IID FAR IID_IServiceProvider;

interface IServiceProvider : public IUnknown {
  STDMETHOD(QueryService)(REFGUID guidService, REFIID riid, void FAR *FAR *ppvObject) = 0;
};
typedef IServiceProvider FAR *LPSERVICEPROVIDER;
