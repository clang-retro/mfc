// urlmon.h - URL monikers: binding, bind status callbacks and bind hosts.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#ifndef _WINPORT_URLMON_H
#define _WINPORT_URLMON_H

#include <objidl.h>
#include <oleidl.h>
#include <ocidl.h>

interface IBinding;
interface IBindStatusCallback;
interface IBindHost;

extern "C" {
extern const IID IID_IBinding, IID_IBindStatusCallback, IID_IBindHost;
#define SID_IBindHost IID_IBindHost
#define SID_SBindHost IID_IBindHost
}

#define MK_S_ASYNCHRONOUS _HRESULT_TYPEDEF_(0x000401E8)
#define S_ASYNCHRONOUS MK_S_ASYNCHRONOUS

#define INET_E_INVALID_URL _HRESULT_TYPEDEF_(0x800C0002)
#define INET_E_NO_SESSION _HRESULT_TYPEDEF_(0x800C0003)
#define INET_E_CANNOT_CONNECT _HRESULT_TYPEDEF_(0x800C0004)
#define INET_E_RESOURCE_NOT_FOUND _HRESULT_TYPEDEF_(0x800C0005)
#define INET_E_OBJECT_NOT_FOUND _HRESULT_TYPEDEF_(0x800C0006)
#define INET_E_DATA_NOT_AVAILABLE _HRESULT_TYPEDEF_(0x800C0007)
#define INET_E_DOWNLOAD_FAILURE _HRESULT_TYPEDEF_(0x800C0008)
#define INET_E_AUTHENTICATION_REQUIRED _HRESULT_TYPEDEF_(0x800C0009)
#define INET_E_NO_VALID_MEDIA _HRESULT_TYPEDEF_(0x800C000A)
#define INET_E_CONNECTION_TIMEOUT _HRESULT_TYPEDEF_(0x800C000B)
#define INET_E_INVALID_REQUEST _HRESULT_TYPEDEF_(0x800C000C)
#define INET_E_UNKNOWN_PROTOCOL _HRESULT_TYPEDEF_(0x800C000D)
#define INET_E_SECURITY_PROBLEM _HRESULT_TYPEDEF_(0x800C000E)
#define INET_E_CANNOT_LOAD_DATA _HRESULT_TYPEDEF_(0x800C000F)
#define INET_E_CANNOT_INSTANTIATE_OBJECT _HRESULT_TYPEDEF_(0x800C0010)

typedef enum {
  BINDVERB_GET = 0,
  BINDVERB_POST = 1,
  BINDVERB_PUT = 2,
  BINDVERB_CUSTOM = 3
} BINDVERB;

typedef enum {
  BINDF_ASYNCHRONOUS = 0x1,
  BINDF_ASYNCSTORAGE = 0x2,
  BINDF_NOPROGRESSIVERENDERING = 0x4,
  BINDF_OFFLINEOPERATION = 0x8,
  BINDF_GETNEWESTVERSION = 0x10,
  BINDF_NOWRITECACHE = 0x20,
  BINDF_NEEDFILE = 0x40,
  BINDF_PULLDATA = 0x80,
  BINDF_IGNORESECURITYPROBLEM = 0x100,
  BINDF_RESYNCHRONIZE = 0x200,
  BINDF_HYPERLINK = 0x400,
  BINDF_NO_UI = 0x800,
  BINDF_SILENTOPERATION = 0x1000,
  BINDF_PRAGMA_NO_CACHE = 0x2000
} BINDF;

typedef enum {
  BSCF_FIRSTDATANOTIFICATION = 0x1,
  BSCF_INTERMEDIATEDATANOTIFICATION = 0x2,
  BSCF_LASTDATANOTIFICATION = 0x4,
  BSCF_DATAFULLYAVAILABLE = 0x8,
  BSCF_AVAILABLEDATASIZEUNKNOWN = 0x10
} BSCF;

typedef enum {
  BINDSTATUS_FINDINGRESOURCE = 1,
  BINDSTATUS_CONNECTING,
  BINDSTATUS_REDIRECTING,
  BINDSTATUS_BEGINDOWNLOADDATA,
  BINDSTATUS_DOWNLOADINGDATA,
  BINDSTATUS_ENDDOWNLOADDATA,
  BINDSTATUS_BEGINDOWNLOADCOMPONENTS,
  BINDSTATUS_INSTALLINGCOMPONENTS,
  BINDSTATUS_ENDDOWNLOADCOMPONENTS,
  BINDSTATUS_USINGCACHEDCOPY,
  BINDSTATUS_SENDINGREQUEST,
  BINDSTATUS_CLASSIDAVAILABLE,
  BINDSTATUS_MIMETYPEAVAILABLE,
  BINDSTATUS_CACHEFILENAMEAVAILABLE
} BINDSTATUS;

typedef struct _tagBINDINFO {
  ULONG cbSize;
  LPWSTR szExtraInfo;
  STGMEDIUM stgmedData;
  DWORD grfBindInfoF;
  DWORD dwBindVerb;
  LPWSTR szCustomVerb;
  DWORD cbstgmedData;
  DWORD dwOptions;
  DWORD dwOptionsFlags;
  DWORD dwCodePage;
  SECURITY_ATTRIBUTES securityAttributes;
  IID iid;
  IUnknown* pUnk;
  DWORD dwReserved;
} BINDINFO;

interface IBinding : public IUnknown {
  STDMETHOD(Abort)(void) = 0;
  STDMETHOD(Suspend)(void) = 0;
  STDMETHOD(Resume)(void) = 0;
  STDMETHOD(SetPriority)(LONG nPriority) = 0;
  STDMETHOD(GetPriority)(LONG* pnPriority) = 0;
  STDMETHOD(GetBindResult)(CLSID* pclsidProtocol, DWORD* pdwResult, LPOLESTR* pszResult, DWORD* pdwReserved) = 0;
};
typedef IBinding* LPBINDING;

interface IBindStatusCallback : public IUnknown {
  STDMETHOD(OnStartBinding)(DWORD dwReserved, IBinding* pib) = 0;
  STDMETHOD(GetPriority)(LONG* pnPriority) = 0;
  STDMETHOD(OnLowResource)(DWORD reserved) = 0;
  STDMETHOD(OnProgress)(ULONG ulProgress, ULONG ulProgressMax, ULONG ulStatusCode, LPCWSTR szStatusText) = 0;
  STDMETHOD(OnStopBinding)(HRESULT hresult, LPCWSTR szError) = 0;
  STDMETHOD(GetBindInfo)(DWORD* grfBINDF, BINDINFO* pbindinfo) = 0;
  STDMETHOD(OnDataAvailable)(DWORD grfBSCF, DWORD dwSize, FORMATETC* pformatetc, STGMEDIUM* pstgmed) = 0;
  STDMETHOD(OnObjectAvailable)(REFIID riid, IUnknown* punk) = 0;
};
typedef IBindStatusCallback* LPBINDSTATUSCALLBACK;

interface IBindHost : public IUnknown {
  STDMETHOD(CreateMoniker)(LPOLESTR szName, IBindCtx* pBC, IMoniker** ppmk, DWORD dwReserved) = 0;
  STDMETHOD(MonikerBindToStorage)(IMoniker* pMk, IBindCtx* pBC, IBindStatusCallback* pBSC, REFIID riid,
                                  void** ppvObj) = 0;
  STDMETHOD(MonikerBindToObject)(IMoniker* pMk, IBindCtx* pBC, IBindStatusCallback* pBSC, REFIID riid,
                                 void** ppvObj) = 0;
};
typedef IBindHost* LPBINDHOST;

WINPORT_UUIDOF(IBinding)
WINPORT_UUIDOF(IBindStatusCallback)
WINPORT_UUIDOF(IBindHost)

extern "C" {
HRESULT WINAPI CreateURLMoniker(LPMONIKER pMkCtx, LPCWSTR szURL, LPMONIKER* ppmk);
HRESULT WINAPI CreateAsyncBindCtx(DWORD reserved, IBindStatusCallback* pBSCb, IEnumFORMATETC* pEFetc,
                                  IBindCtx** ppBC);
HRESULT WINAPI RegisterBindStatusCallback(LPBC pBC, IBindStatusCallback* pBSCb,
                                          IBindStatusCallback** ppBSCBPrev, DWORD dwReserved);
HRESULT WINAPI RevokeBindStatusCallback(LPBC pBC, IBindStatusCallback* pBSCb);
HRESULT WINAPI IsAsyncMoniker(IMoniker* pmk);
HRESULT WINAPI URLDownloadToFileA(LPUNKNOWN pCaller, LPCSTR szURL, LPCSTR szFileName, DWORD dwReserved,
                                  LPBINDSTATUSCALLBACK lpfnCB);
HRESULT WINAPI URLDownloadToFileW(LPUNKNOWN pCaller, LPCWSTR szURL, LPCWSTR szFileName, DWORD dwReserved,
                                  LPBINDSTATUSCALLBACK lpfnCB);
#ifdef UNICODE
#define URLDownloadToFile URLDownloadToFileW
#else
#define URLDownloadToFile URLDownloadToFileA
#endif
}

#endif
