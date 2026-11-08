// msdatsrc.h - the DataSource interface of data source controls, and the
// listener they tell when their data members change.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#ifndef _WINPORT_MSDATSRC_H
#define _WINPORT_MSDATSRC_H

#include <windows.h>
#include <ole2.h>
#include <oleauto.h>

typedef BSTR DataMember;

extern "C" const IID IID_DataSourceListener, IID_DataSource;

interface DataSourceListener : public IUnknown {
  STDMETHOD(dataMemberChanged)(DataMember bstrDM) = 0;
  STDMETHOD(dataMemberAdded)(DataMember bstrDM) = 0;
  STDMETHOD(dataMemberRemoved)(DataMember bstrDM) = 0;
};

interface DataSource : public IUnknown {
  STDMETHOD(getDataMember)(DataMember bstrDM, REFIID riid, IUnknown** ppunk) = 0;
  STDMETHOD(getDataMemberName)(LONG lIndex, DataMember* pbstrDM) = 0;
  STDMETHOD(getDataMemberCount)(LONG* plCount) = 0;
  STDMETHOD(addDataSourceListener)(DataSourceListener* pDSL) = 0;
  STDMETHOD(removeDataSourceListener)(DataSourceListener* pDSL) = 0;
};

#endif
