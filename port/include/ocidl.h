// ocidl.h - OLE controls: connection points, class information,
// persistence through property bags and stream initialization,
// property pages and notifications, fonts and pictures, and the control
// and control site interfaces.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#ifndef _WINPORT_OCIDL_H
#define _WINPORT_OCIDL_H

#include <oleidl.h>
#include <oaidl.h>

interface IConnectionPoint;
interface IConnectionPointContainer;
interface IEnumConnections;
interface IEnumConnectionPoints;

extern "C" {
extern const IID IID_IConnectionPoint, IID_IConnectionPointContainer, IID_IEnumConnections,
    IID_IEnumConnectionPoints, IID_IProvideClassInfo, IID_IProvideClassInfo2, IID_IPersistStreamInit,
    IID_IPersistPropertyBag, IID_IPersistMemory, IID_IPropertyNotifySink, IID_ISpecifyPropertyPages,
    IID_IPropertyPage, IID_IPropertyPage2, IID_IPropertyPageSite, IID_IOleControl, IID_IOleControlSite,
    IID_ISimpleFrameSite, IID_IFont, IID_IFontDisp, IID_IPicture, IID_IPictureDisp, IID_IPerPropertyBrowsing,
    IID_IClassFactory2, IID_IQuickActivate, IID_IPointerInactive, IID_IViewObjectEx,
    IID_IOleInPlaceObjectWindowless, IID_IOleInPlaceSiteEx, IID_IOleInPlaceSiteWindowless,
    IID_IOleUndoManager, IID_IObjectWithSite, IID_IFontEventsDisp;
}

typedef struct tagCONNECTDATA {
  IUnknown* pUnk;
  DWORD dwCookie;
} CONNECTDATA, *PCONNECTDATA, *LPCONNECTDATA;

interface IEnumConnections : public IUnknown {
  STDMETHOD(Next)(ULONG cConnections, LPCONNECTDATA rgcd, ULONG* pcFetched) = 0;
  STDMETHOD(Skip)(ULONG cConnections) = 0;
  STDMETHOD(Reset)() = 0;
  STDMETHOD(Clone)(IEnumConnections** ppEnum) = 0;
};
typedef IEnumConnections* LPENUMCONNECTIONS;
typedef IEnumConnections* PENUMCONNECTIONS;
interface IConnectionPoint : public IUnknown {
  STDMETHOD(GetConnectionInterface)(IID* pIID) = 0;
  STDMETHOD(GetConnectionPointContainer)(IConnectionPointContainer** ppCPC) = 0;
  STDMETHOD(Advise)(IUnknown* pUnkSink, DWORD* pdwCookie) = 0;
  STDMETHOD(Unadvise)(DWORD dwCookie) = 0;
  STDMETHOD(EnumConnections)(IEnumConnections** ppEnum) = 0;
};
typedef IConnectionPoint* LPCONNECTIONPOINT;
typedef IConnectionPoint* PCONNECTIONPOINT;
interface IEnumConnectionPoints : public IUnknown {
  STDMETHOD(Next)(ULONG cConnections, LPCONNECTIONPOINT* ppCP, ULONG* pcFetched) = 0;
  STDMETHOD(Skip)(ULONG cConnections) = 0;
  STDMETHOD(Reset)() = 0;
  STDMETHOD(Clone)(IEnumConnectionPoints** ppEnum) = 0;
};
typedef IEnumConnectionPoints* LPENUMCONNECTIONPOINTS;
typedef IEnumConnectionPoints* PENUMCONNECTIONPOINTS;
interface IConnectionPointContainer : public IUnknown {
  STDMETHOD(EnumConnectionPoints)(IEnumConnectionPoints** ppEnum) = 0;
  STDMETHOD(FindConnectionPoint)(REFIID riid, IConnectionPoint** ppCP) = 0;
};
typedef IConnectionPointContainer* LPCONNECTIONPOINTCONTAINER;
typedef IConnectionPointContainer* PCONNECTIONPOINTCONTAINER;

interface IProvideClassInfo : public IUnknown {
  STDMETHOD(GetClassInfo)(ITypeInfo** ppTI) = 0;
};
typedef IProvideClassInfo* LPPROVIDECLASSINFO;
typedef enum tagGUIDKIND { GUIDKIND_DEFAULT_SOURCE_DISP_IID = 1 } GUIDKIND;
interface IProvideClassInfo2 : public IProvideClassInfo {
  STDMETHOD(GetGUID)(DWORD dwGuidKind, GUID* pGUID) = 0;
};
typedef IProvideClassInfo2* LPPROVIDECLASSINFO2;

interface IPersistStreamInit : public IPersist {
  STDMETHOD(IsDirty)() = 0;
  STDMETHOD(Load)(LPSTREAM pStm) = 0;
  STDMETHOD(Save)(LPSTREAM pStm, BOOL fClearDirty) = 0;
  STDMETHOD(GetSizeMax)(ULARGE_INTEGER* pCbSize) = 0;
  STDMETHOD(InitNew)() = 0;
};
typedef IPersistStreamInit* LPPERSISTSTREAMINIT;
interface IPersistMemory : public IPersist {
  STDMETHOD(IsDirty)() = 0;
  STDMETHOD(Load)(LPVOID pMem, ULONG cbSize) = 0;
  STDMETHOD(Save)(LPVOID pMem, BOOL fClearDirty, ULONG cbSize) = 0;
  STDMETHOD(GetSizeMax)(ULONG* pCbSize) = 0;
  STDMETHOD(InitNew)() = 0;
};
interface IPersistPropertyBag : public IPersist {
  STDMETHOD(InitNew)() = 0;
  STDMETHOD(Load)(IPropertyBag* pPropBag, IErrorLog* pErrorLog) = 0;
  STDMETHOD(Save)(IPropertyBag* pPropBag, BOOL fClearDirty, BOOL fSaveAllProperties) = 0;
};
typedef IPersistPropertyBag* LPPERSISTPROPERTYBAG;

interface IPropertyNotifySink : public IUnknown {
  STDMETHOD(OnChanged)(DISPID dispID) = 0;
  STDMETHOD(OnRequestEdit)(DISPID dispID) = 0;
};
typedef IPropertyNotifySink* LPPROPERTYNOTIFYSINK;

typedef struct tagCAUUID {
  ULONG cElems;
  GUID* pElems;
} CAUUID, *LPCAUUID;
typedef struct tagCALPOLESTR {
  ULONG cElems;
  LPOLESTR* pElems;
} CALPOLESTR, *LPCALPOLESTR;
typedef struct tagCADWORD {
  ULONG cElems;
  DWORD* pElems;
} CADWORD, *LPCADWORD;
interface ISpecifyPropertyPages : public IUnknown {
  STDMETHOD(GetPages)(CAUUID* pPages) = 0;
};
typedef ISpecifyPropertyPages* LPSPECIFYPROPERTYPAGES;

#endif
