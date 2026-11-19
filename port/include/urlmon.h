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

// Security zones
typedef enum tagURLZONE {
  URLZONE_INVALID = -1,
  URLZONE_PREDEFINED_MIN = 0,
  URLZONE_LOCAL_MACHINE = 0,
  URLZONE_INTRANET = 1,
  URLZONE_TRUSTED = 2,
  URLZONE_INTERNET = 3,
  URLZONE_UNTRUSTED = 4,
  URLZONE_PREDEFINED_MAX = 999,
  URLZONE_USER_MIN = 1000,
  URLZONE_USER_MAX = 10000
} URLZONE;

extern "C" const IID IID_IInternetSecurityMgrSite, IID_IInternetSecurityManager;
#define SID_SInternetSecurityManager IID_IInternetSecurityManager

#define INET_E_DEFAULT_ACTION _HRESULT_TYPEDEF_(0x800C0011)

#define MAX_SIZE_SECURITY_ID 512

// MapUrlToZone's flags.
#define MUTZ_NOSAVEDFILECHECK 0x00000001
#define MUTZ_ISFILE 0x00000002
#define MUTZ_ACCEPT_WILDCARD_SCHEME 0x00000080
#define MUTZ_ENFORCERESTRICTED 0x00000100
#define MUTZ_DONT_UNESCAPE 0x00000800

// ProcessUrlAction's flags.
#define PUAF_DEFAULT 0x00000000
#define PUAF_NOUI 0x00000001
#define PUAF_ISFILE 0x00000002
#define PUAF_WARN_IF_DENIED 0x00000004
#define PUAF_FORCEUI_FOREGROUND 0x00000008
#define PUAF_CHECK_TIFS 0x00000010

// SetZoneMapping's flags.
#define SZM_CREATE 0x00000000
#define SZM_DELETE 0x00000001

// Policies.
#define URLPOLICY_ALLOW 0x00
#define URLPOLICY_QUERY 0x01
#define URLPOLICY_DISALLOW 0x03

// Actions (a few of them; the ranges hold the rest).
#define URLACTION_MIN 0x00001000
#define URLACTION_DOWNLOAD_MIN 0x00001000
#define URLACTION_DOWNLOAD_SIGNED_ACTIVEX 0x00001001
#define URLACTION_DOWNLOAD_UNSIGNED_ACTIVEX 0x00001004
#define URLACTION_DOWNLOAD_MAX 0x000011FF
#define URLACTION_ACTIVEX_MIN 0x00001200
#define URLACTION_ACTIVEX_RUN 0x00001200
#define URLACTION_ACTIVEX_OVERRIDE_OBJECT_SAFETY 0x00001201
#define URLACTION_ACTIVEX_OVERRIDE_DATA_SAFETY 0x00001202
#define URLACTION_ACTIVEX_OVERRIDE_SCRIPT_SAFETY 0x00001203
#define URLACTION_ACTIVEX_CONFIRM_NOOBJECTSAFETY 0x00001204
#define URLACTION_ACTIVEX_TREATASUNTRUSTED 0x00001205
#define URLACTION_ACTIVEX_MAX 0x000013FF
#define URLACTION_SCRIPT_MIN 0x00001400
#define URLACTION_SCRIPT_RUN 0x00001400
#define URLACTION_SCRIPT_OVERRIDE_SAFETY 0x00001401
#define URLACTION_SCRIPT_JAVA_USE 0x00001402
#define URLACTION_SCRIPT_SAFE_ACTIVEX 0x00001405
#define URLACTION_SCRIPT_MAX 0x000015FF
#define URLACTION_HTML_MIN 0x00001600
#define URLACTION_HTML_SUBMIT_FORMS 0x00001601
#define URLACTION_HTML_JAVA_RUN 0x00001605
#define URLACTION_HTML_MAX 0x000017FF
#define URLACTION_SHELL_MIN 0x00001800
#define URLACTION_SHELL_INSTALL_DTITEMS 0x00001800
#define URLACTION_SHELL_FILE_DOWNLOAD 0x00001803
#define URLACTION_SHELL_MAX 0x000019FF

interface IInternetSecurityMgrSite : public IUnknown {
  STDMETHOD(GetWindow)(HWND* phwnd) = 0;
  STDMETHOD(EnableModeless)(BOOL fEnable) = 0;
};

interface IInternetSecurityManager : public IUnknown {
  STDMETHOD(SetSecuritySite)(IInternetSecurityMgrSite* pSite) = 0;
  STDMETHOD(GetSecuritySite)(IInternetSecurityMgrSite** ppSite) = 0;
  STDMETHOD(MapUrlToZone)(LPCWSTR pwszUrl, DWORD* pdwZone, DWORD dwFlags) = 0;
  STDMETHOD(GetSecurityId)(LPCWSTR pwszUrl, BYTE* pbSecurityId, DWORD* pcbSecurityId, DWORD_PTR dwReserved) = 0;
  STDMETHOD(ProcessUrlAction)(LPCWSTR pwszUrl, DWORD dwAction, BYTE* pPolicy, DWORD cbPolicy, BYTE* pContext,
                              DWORD cbContext, DWORD dwFlags, DWORD dwReserved) = 0;
  STDMETHOD(QueryCustomPolicy)(LPCWSTR pwszUrl, REFGUID guidKey, BYTE** ppPolicy, DWORD* pcbPolicy, BYTE* pContext,
                               DWORD cbContext, DWORD dwReserved) = 0;
  STDMETHOD(SetZoneMapping)(DWORD dwZone, LPCWSTR lpszPattern, DWORD dwFlags) = 0;
  STDMETHOD(GetZoneMappings)(DWORD dwZone, IEnumString** ppenumString, DWORD dwFlags) = 0;
};

WINPORT_UUIDOF(IInternetSecurityManager)

extern "C" {
HRESULT WINAPI CreateURLMoniker(LPMONIKER pMkCtx, LPCWSTR szURL, LPMONIKER* ppmk);
HRESULT WINAPI CreateAsyncBindCtx(DWORD reserved, IBindStatusCallback* pBSCb, IEnumFORMATETC* pEFetc,
                                  IBindCtx** ppBC);
HRESULT WINAPI RegisterBindStatusCallback(LPBC pBC, IBindStatusCallback* pBSCb,
                                          IBindStatusCallback** ppBSCBPrev, DWORD dwReserved);
HRESULT WINAPI RevokeBindStatusCallback(LPBC pBC, IBindStatusCallback* pBSCb);
HRESULT WINAPI IsAsyncMoniker(IMoniker* pmk);
HRESULT WINAPI CoInternetCreateSecurityManager(IServiceProvider* pSP, IInternetSecurityManager** ppSM,
                                               DWORD dwReserved);
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
