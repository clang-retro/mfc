// ocidl.h - for 16-bit Windows: the interfaces of OLE controls that MFC's
// automation classes name (connection points, IProvideClassInfo and
// IClassFactory2). 16-bit OLE 2 does not have these interfaces. MFC
// objects give them to each other in one program. The IIDs have the
// values of Win32 (src/win16ole.cpp).
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <ole2.h>
#include <dispatch.h>

interface IConnectionPoint;
interface IConnectionPointContainer;
interface IEnumConnections;
interface IEnumConnectionPoints;

extern "C" {
extern const IID FAR IID_IConnectionPoint;
extern const IID FAR IID_IConnectionPointContainer;
extern const IID FAR IID_IEnumConnections;
extern const IID FAR IID_IEnumConnectionPoints;
extern const IID FAR IID_IProvideClassInfo;
extern const IID FAR IID_IProvideClassInfo2;
extern const IID FAR IID_IClassFactory2;
}

typedef struct tagCONNECTDATA {
  IUnknown FAR *pUnk;
  DWORD dwCookie;
} CONNECTDATA, FAR *PCONNECTDATA, FAR *LPCONNECTDATA;

interface IEnumConnections : public IUnknown {
  STDMETHOD(Next)(ULONG cConnections, LPCONNECTDATA rgcd, ULONG FAR *pcFetched) = 0;
  STDMETHOD(Skip)(ULONG cConnections) = 0;
  STDMETHOD(Reset)() = 0;
  STDMETHOD(Clone)(IEnumConnections FAR *FAR *ppEnum) = 0;
};
typedef IEnumConnections FAR *LPENUMCONNECTIONS;
typedef IEnumConnections FAR *PENUMCONNECTIONS;

interface IConnectionPoint : public IUnknown {
  STDMETHOD(GetConnectionInterface)(IID FAR *pIID) = 0;
  STDMETHOD(GetConnectionPointContainer)(IConnectionPointContainer FAR *FAR *ppCPC) = 0;
  STDMETHOD(Advise)(IUnknown FAR *pUnkSink, DWORD FAR *pdwCookie) = 0;
  STDMETHOD(Unadvise)(DWORD dwCookie) = 0;
  STDMETHOD(EnumConnections)(IEnumConnections FAR *FAR *ppEnum) = 0;
};
typedef IConnectionPoint FAR *LPCONNECTIONPOINT;
typedef IConnectionPoint FAR *PCONNECTIONPOINT;

interface IEnumConnectionPoints : public IUnknown {
  STDMETHOD(Next)(ULONG cConnections, LPCONNECTIONPOINT FAR *ppCP, ULONG FAR *pcFetched) = 0;
  STDMETHOD(Skip)(ULONG cConnections) = 0;
  STDMETHOD(Reset)() = 0;
  STDMETHOD(Clone)(IEnumConnectionPoints FAR *FAR *ppEnum) = 0;
};
typedef IEnumConnectionPoints FAR *LPENUMCONNECTIONPOINTS;
typedef IEnumConnectionPoints FAR *PENUMCONNECTIONPOINTS;

interface IConnectionPointContainer : public IUnknown {
  STDMETHOD(EnumConnectionPoints)(IEnumConnectionPoints FAR *FAR *ppEnum) = 0;
  STDMETHOD(FindConnectionPoint)(REFIID riid, IConnectionPoint FAR *FAR *ppCP) = 0;
};
typedef IConnectionPointContainer FAR *LPCONNECTIONPOINTCONTAINER;
typedef IConnectionPointContainer FAR *PCONNECTIONPOINTCONTAINER;

interface IProvideClassInfo : public IUnknown {
  STDMETHOD(GetClassInfo)(ITypeInfo FAR *FAR *ppTI) = 0;
};
typedef IProvideClassInfo FAR *LPPROVIDECLASSINFO;
typedef enum tagGUIDKIND { GUIDKIND_DEFAULT_SOURCE_DISP_IID = 1 } GUIDKIND;
interface IProvideClassInfo2 : public IProvideClassInfo {
  STDMETHOD(GetGUID)(DWORD dwGuidKind, GUID FAR *pGUID) = 0;
};
typedef IProvideClassInfo2 FAR *LPPROVIDECLASSINFO2;

typedef struct tagLICINFO {
  LONG cbLicInfo;
  BOOL fRuntimeKeyAvail;
  BOOL fLicVerified;
} LICINFO, FAR *LPLICINFO;

interface IClassFactory2 : public IClassFactory {
  STDMETHOD(GetLicInfo)(LICINFO FAR *pLicInfo) = 0;
  STDMETHOD(RequestLicKey)(DWORD dwReserved, BSTR FAR *pBstrKey) = 0;
  STDMETHOD(CreateInstanceLic)(IUnknown FAR *pUnkOuter, IUnknown FAR *pUnkReserved, REFIID riid,
                               BSTR bstrKey, LPVOID FAR *ppvObj) = 0;
};
typedef IClassFactory2 FAR *LPCLASSFACTORY2;

// The results of the connection point interfaces, as in Win32.
#define CONNECT_E_FIRST 0x80040200L
#define CONNECT_E_LAST 0x8004020FL
#define CONNECT_S_FIRST 0x00040200L
#define CONNECT_S_LAST 0x0004020FL
#define CONNECT_E_NOCONNECTION (CONNECT_E_FIRST + 0)
#define CONNECT_E_ADVISELIMIT (CONNECT_E_FIRST + 1)
#define CONNECT_E_CANNOTCONNECT (CONNECT_E_FIRST + 2)
#define CONNECT_E_OVERRIDDEN (CONNECT_E_FIRST + 3)

// __uuidof(I) gives the IID of the interface I. The 16-bit compiler has
// no __declspec(uuid), so each interface that MFC names gets its IID
// here, in the form of the portable layer (port/include/unknwn.h).
#ifndef __uuidof
template <class T>
struct __winport_uuidof;
#define __uuidof(T) (::__winport_uuidof<T>::iid())
#define WINPORT_UUIDOF(I)                                      \
  template <>                                                  \
  struct __winport_uuidof<I> {                                 \
    static constexpr const IID FAR &iid() { return IID_##I; } \
  };
#endif
WINPORT_UUIDOF(IUnknown)
WINPORT_UUIDOF(IClassFactory)
WINPORT_UUIDOF(IDispatch)
WINPORT_UUIDOF(IEnumVARIANT)
WINPORT_UUIDOF(ITypeInfo)
WINPORT_UUIDOF(ITypeLib)
WINPORT_UUIDOF(IConnectionPoint)
WINPORT_UUIDOF(IConnectionPointContainer)
WINPORT_UUIDOF(IEnumConnections)
WINPORT_UUIDOF(IEnumConnectionPoints)
WINPORT_UUIDOF(IProvideClassInfo)
WINPORT_UUIDOF(IProvideClassInfo2)
WINPORT_UUIDOF(IClassFactory2)
