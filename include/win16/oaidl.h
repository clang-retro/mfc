// oaidl.h - for 16-bit Windows: the automation interfaces of
// dispatch.h, and the property bag and error log, which 16-bit OLE 2
// does not have. OLE controls save their properties to a property bag.
// The IIDs have the values of Win32 (src/win16ole.cpp).
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <ole2.h>
#include <dispatch.h>

extern "C" {
extern const IID FAR IID_IErrorLog;
extern const IID FAR IID_IPropertyBag;
}

interface IErrorLog : public IUnknown {
  STDMETHOD(AddError)(LPCOLESTR pszPropName, EXCEPINFO FAR *pExcepInfo) = 0;
};
typedef IErrorLog FAR *LPERRORLOG;

interface IPropertyBag : public IUnknown {
  STDMETHOD(Read)(LPCOLESTR pszPropName, VARIANT FAR *pVar, IErrorLog FAR *pErrorLog) = 0;
  STDMETHOD(Write)(LPCOLESTR pszPropName, VARIANT FAR *pVar) = 0;
};
typedef IPropertyBag FAR *LPPROPERTYBAG;

// A counted block of bytes, as in the wtypes.h of Win32.
#ifndef _tagBLOB_DEFINED
#define _tagBLOB_DEFINED
typedef struct tagBLOB {
  ULONG cbSize;
  BYTE FAR *pBlobData;
} BLOB, FAR *LPBLOB;
#endif
