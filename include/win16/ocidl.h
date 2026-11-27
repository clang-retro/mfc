// ocidl.h - for 16-bit Windows: the interfaces of OLE controls. 16-bit
// OLE 2 does not have these interfaces. The library gives them: the
// connection points, the class information, the persistence through
// property bags and stream initialization, the property pages, the
// stock font and picture, and the control and control site interfaces.
// The IIDs have the values of Win32 (src/win16ole.cpp). An OLECHAR is a
// char, as in all of 16-bit OLE 2.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <ole2.h>
#include <dispatch.h>
#include <servprov.h>
#include <oaidl.h>

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
extern const IID FAR IID_IPersistStreamInit;
extern const IID FAR IID_IPersistPropertyBag;
extern const IID FAR IID_IPersistMemory;
extern const IID FAR IID_IPropertyNotifySink;
extern const IID FAR IID_ISpecifyPropertyPages;
extern const IID FAR IID_IPropertyPage;
extern const IID FAR IID_IPropertyPage2;
extern const IID FAR IID_IPropertyPageSite;
extern const IID FAR IID_IOleControl;
extern const IID FAR IID_IOleControlSite;
extern const IID FAR IID_ISimpleFrameSite;
extern const IID FAR IID_IFont;
extern const IID FAR IID_IFontDisp;
extern const IID FAR IID_IFontEventsDisp;
extern const IID FAR IID_IPicture;
extern const IID FAR IID_IPictureDisp;
extern const IID FAR IID_IPerPropertyBrowsing;
extern const IID FAR IID_IQuickActivate;
extern const IID FAR IID_IPointerInactive;
extern const IID FAR IID_IViewObjectEx;
extern const IID FAR IID_IOleInPlaceObjectWindowless;
extern const IID FAR IID_IOleInPlaceSiteEx;
extern const IID FAR IID_IOleInPlaceSiteWindowless;
extern const IID FAR IID_IAdviseSinkEx;
extern const IID FAR IID_IObjectWithSite;
extern const IID FAR IID_IOleUndoManager;
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

// Persistence
interface IPersistStreamInit : public IPersist {
  STDMETHOD(IsDirty)() = 0;
  STDMETHOD(Load)(LPSTREAM pStm) = 0;
  STDMETHOD(Save)(LPSTREAM pStm, BOOL fClearDirty) = 0;
  STDMETHOD(GetSizeMax)(ULARGE_INTEGER FAR *pCbSize) = 0;
  STDMETHOD(InitNew)() = 0;
};
typedef IPersistStreamInit FAR *LPPERSISTSTREAMINIT;

interface IPersistMemory : public IPersist {
  STDMETHOD(IsDirty)() = 0;
  STDMETHOD(Load)(LPVOID pMem, ULONG cbSize) = 0;
  STDMETHOD(Save)(LPVOID pMem, BOOL fClearDirty, ULONG cbSize) = 0;
  STDMETHOD(GetSizeMax)(ULONG FAR *pCbSize) = 0;
  STDMETHOD(InitNew)() = 0;
};
typedef IPersistMemory FAR *LPPERSISTMEMORY;

interface IPersistPropertyBag : public IPersist {
  STDMETHOD(InitNew)() = 0;
  STDMETHOD(Load)(IPropertyBag FAR *pPropBag, IErrorLog FAR *pErrorLog) = 0;
  STDMETHOD(Save)(IPropertyBag FAR *pPropBag, BOOL fClearDirty, BOOL fSaveAllProperties) = 0;
};
typedef IPersistPropertyBag FAR *LPPERSISTPROPERTYBAG;

// Property notifications and property pages
interface IPropertyNotifySink : public IUnknown {
  STDMETHOD(OnChanged)(DISPID dispID) = 0;
  STDMETHOD(OnRequestEdit)(DISPID dispID) = 0;
};
typedef IPropertyNotifySink FAR *LPPROPERTYNOTIFYSINK;

typedef struct tagCAUUID {
  ULONG cElems;
  GUID FAR *pElems;
} CAUUID, FAR *LPCAUUID;
typedef struct tagCALPOLESTR {
  ULONG cElems;
  LPOLESTR FAR *pElems;
} CALPOLESTR, FAR *LPCALPOLESTR;
typedef struct tagCADWORD {
  ULONG cElems;
  DWORD FAR *pElems;
} CADWORD, FAR *LPCADWORD;

interface ISpecifyPropertyPages : public IUnknown {
  STDMETHOD(GetPages)(CAUUID FAR *pPages) = 0;
};
typedef ISpecifyPropertyPages FAR *LPSPECIFYPROPERTYPAGES;

typedef struct tagPROPPAGEINFO {
  ULONG cb;
  LPOLESTR pszTitle;
  SIZE size;
  LPOLESTR pszDocString;
  LPOLESTR pszHelpFile;
  DWORD dwHelpContext;
} PROPPAGEINFO, FAR *LPPROPPAGEINFO;

interface IPropertyPageSite : public IUnknown {
  STDMETHOD(OnStatusChange)(DWORD dwFlags) = 0;
  STDMETHOD(GetLocaleID)(LCID FAR *pLocaleID) = 0;
  STDMETHOD(GetPageContainer)(IUnknown FAR *FAR *ppUnk) = 0;
  STDMETHOD(TranslateAccelerator)(MSG FAR *pMsg) = 0;
};
typedef IPropertyPageSite FAR *LPPROPERTYPAGESITE;
#define PROPPAGESTATUS_DIRTY 0x01
#define PROPPAGESTATUS_VALIDATE 0x02
#define PROPPAGESTATUS_CLEAN 0x04

interface IPropertyPage : public IUnknown {
  STDMETHOD(SetPageSite)(IPropertyPageSite FAR *pPageSite) = 0;
  STDMETHOD(Activate)(HWND hWndParent, LPCRECT pRect, BOOL bModal) = 0;
  STDMETHOD(Deactivate)() = 0;
  STDMETHOD(GetPageInfo)(PROPPAGEINFO FAR *pPageInfo) = 0;
  STDMETHOD(SetObjects)(ULONG cObjects, IUnknown FAR *FAR *ppUnk) = 0;
  STDMETHOD(Show)(UINT nCmdShow) = 0;
  STDMETHOD(Move)(LPCRECT pRect) = 0;
  STDMETHOD(IsPageDirty)() = 0;
  STDMETHOD(Apply)() = 0;
  STDMETHOD(Help)(LPCOLESTR pszHelpDir) = 0;
  STDMETHOD(TranslateAccelerator)(MSG FAR *pMsg) = 0;
};
typedef IPropertyPage FAR *LPPROPERTYPAGE;

interface IPropertyPage2 : public IPropertyPage {
  STDMETHOD(EditProperty)(DISPID dispID) = 0;
};
typedef IPropertyPage2 FAR *LPPROPERTYPAGE2;

interface IPerPropertyBrowsing : public IUnknown {
  STDMETHOD(GetDisplayString)(DISPID dispID, BSTR FAR *pBstr) = 0;
  STDMETHOD(MapPropertyToPage)(DISPID dispID, CLSID FAR *pClsid) = 0;
  STDMETHOD(GetPredefinedStrings)(DISPID dispID, CALPOLESTR FAR *pCaStringsOut,
                                  CADWORD FAR *pCaCookiesOut) = 0;
  STDMETHOD(GetPredefinedValue)(DISPID dispID, DWORD dwCookie, VARIANT FAR *pVarOut) = 0;
};
typedef IPerPropertyBrowsing FAR *LPPERPROPERTYBROWSING;

// The types of the control properties
typedef DWORD OLE_COLOR;
typedef long OLE_XPOS_PIXELS;
typedef long OLE_YPOS_PIXELS;
typedef long OLE_XSIZE_PIXELS;
typedef long OLE_YSIZE_PIXELS;
typedef long OLE_XPOS_HIMETRIC;
typedef long OLE_YPOS_HIMETRIC;
typedef long OLE_XSIZE_HIMETRIC;
typedef long OLE_YSIZE_HIMETRIC;
typedef float OLE_XPOS_CONTAINER;
typedef float OLE_YPOS_CONTAINER;
typedef float OLE_XSIZE_CONTAINER;
typedef float OLE_YSIZE_CONTAINER;
typedef UINT OLE_HANDLE;
typedef VARIANT_BOOL OLE_OPTEXCLUSIVE;
typedef VARIANT_BOOL OLE_CANCELBOOL;
typedef VARIANT_BOOL OLE_ENABLEDEFAULTBOOL;
typedef enum { triUnchecked = 0, triChecked = 1, triGray = 2 } OLE_TRISTATE;

// The stock font and picture. QueryTextMetrics gives a TEXTMETRIC, as
// 16-bit Windows has no wide text metrics.
interface IFont : public IUnknown {
  STDMETHOD(get_Name)(BSTR FAR *pName) = 0;
  STDMETHOD(put_Name)(BSTR name) = 0;
  STDMETHOD(get_Size)(CY FAR *pSize) = 0;
  STDMETHOD(put_Size)(CY size) = 0;
  STDMETHOD(get_Bold)(BOOL FAR *pBold) = 0;
  STDMETHOD(put_Bold)(BOOL bold) = 0;
  STDMETHOD(get_Italic)(BOOL FAR *pItalic) = 0;
  STDMETHOD(put_Italic)(BOOL italic) = 0;
  STDMETHOD(get_Underline)(BOOL FAR *pUnderline) = 0;
  STDMETHOD(put_Underline)(BOOL underline) = 0;
  STDMETHOD(get_Strikethrough)(BOOL FAR *pStrikethrough) = 0;
  STDMETHOD(put_Strikethrough)(BOOL strikethrough) = 0;
  STDMETHOD(get_Weight)(SHORT FAR *pWeight) = 0;
  STDMETHOD(put_Weight)(SHORT weight) = 0;
  STDMETHOD(get_Charset)(SHORT FAR *pCharset) = 0;
  STDMETHOD(put_Charset)(SHORT charset) = 0;
  STDMETHOD(get_hFont)(HFONT FAR *phFont) = 0;
  STDMETHOD(Clone)(IFont FAR *FAR *ppFont) = 0;
  STDMETHOD(IsEqual)(IFont FAR *pFontOther) = 0;
  STDMETHOD(SetRatio)(LONG cyLogical, LONG cyHimetric) = 0;
  STDMETHOD(QueryTextMetrics)(TEXTMETRIC FAR *pTM) = 0;
  STDMETHOD(AddRefHfont)(HFONT hFont) = 0;
  STDMETHOD(ReleaseHfont)(HFONT hFont) = 0;
  STDMETHOD(SetHdc)(HDC hDC) = 0;
};
typedef IFont FAR *LPFONT;

interface IPicture : public IUnknown {
  STDMETHOD(get_Handle)(OLE_HANDLE FAR *pHandle) = 0;
  STDMETHOD(get_hPal)(OLE_HANDLE FAR *phPal) = 0;
  STDMETHOD(get_Type)(SHORT FAR *pType) = 0;
  STDMETHOD(get_Width)(OLE_XSIZE_HIMETRIC FAR *pWidth) = 0;
  STDMETHOD(get_Height)(OLE_YSIZE_HIMETRIC FAR *pHeight) = 0;
  STDMETHOD(Render)(HDC hDC, LONG x, LONG y, LONG cx, LONG cy, OLE_XPOS_HIMETRIC xSrc,
                    OLE_YPOS_HIMETRIC ySrc, OLE_XSIZE_HIMETRIC cxSrc, OLE_YSIZE_HIMETRIC cySrc,
                    LPCRECT pRcWBounds) = 0;
  STDMETHOD(set_hPal)(OLE_HANDLE hPal) = 0;
  STDMETHOD(get_CurDC)(HDC FAR *phDC) = 0;
  STDMETHOD(SelectPicture)(HDC hDCIn, HDC FAR *phDCOut, OLE_HANDLE FAR *phBmpOut) = 0;
  STDMETHOD(get_KeepOriginalFormat)(BOOL FAR *pKeep) = 0;
  STDMETHOD(put_KeepOriginalFormat)(BOOL keep) = 0;
  STDMETHOD(PictureChanged)() = 0;
  STDMETHOD(SaveAsFile)(LPSTREAM pStream, BOOL fSaveMemCopy, LONG FAR *pCbSize) = 0;
  STDMETHOD(get_Attributes)(DWORD FAR *pDwAttr) = 0;
};
typedef IPicture FAR *LPPICTURE;

interface IFontDisp : public IDispatch {};
typedef IFontDisp FAR *LPFONTDISP;
interface IPictureDisp : public IDispatch {};
typedef IPictureDisp FAR *LPPICTUREDISP;
interface IFontEventsDisp : public IDispatch {};

// Controls and their sites
typedef struct tagCONTROLINFO {
  ULONG cb;
  HACCEL hAccel;
  USHORT cAccel;
  DWORD dwFlags;
} CONTROLINFO, FAR *LPCONTROLINFO;
#define CTRLINFO_EATS_RETURN 1
#define CTRLINFO_EATS_ESCAPE 2

interface IOleControl : public IUnknown {
  STDMETHOD(GetControlInfo)(CONTROLINFO FAR *pCI) = 0;
  STDMETHOD(OnMnemonic)(MSG FAR *pMsg) = 0;
  STDMETHOD(OnAmbientPropertyChange)(DISPID dispID) = 0;
  STDMETHOD(FreezeEvents)(BOOL bFreeze) = 0;
};
typedef IOleControl FAR *LPOLECONTROL;

typedef struct _POINTF {
  float x;
  float y;
} POINTF, FAR *LPPOINTF;
#define XFORMCOORDS_POSITION 0x1
#define XFORMCOORDS_SIZE 0x2
#define XFORMCOORDS_HIMETRICTOCONTAINER 0x4
#define XFORMCOORDS_CONTAINERTOHIMETRIC 0x8
#define XFORMCOORDS_EVENTCOMPAT 0x10

interface IOleControlSite : public IUnknown {
  STDMETHOD(OnControlInfoChanged)() = 0;
  STDMETHOD(LockInPlaceActive)(BOOL fLock) = 0;
  STDMETHOD(GetExtendedControl)(IDispatch FAR *FAR *ppDisp) = 0;
  STDMETHOD(TransformCoords)(POINTL FAR *pPtlHimetric, POINTF FAR *pPtfContainer, DWORD dwFlags) = 0;
  STDMETHOD(TranslateAccelerator)(MSG FAR *pMsg, DWORD grfModifiers) = 0;
  STDMETHOD(OnFocus)(BOOL fGotFocus) = 0;
  STDMETHOD(ShowPropertyFrame)() = 0;
};
typedef IOleControlSite FAR *LPOLECONTROLSITE;

interface ISimpleFrameSite : public IUnknown {
  STDMETHOD(PreMessageFilter)(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp, LRESULT FAR *plResult,
                              DWORD FAR *pdwCookie) = 0;
  STDMETHOD(PostMessageFilter)(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp, LRESULT FAR *plResult,
                               DWORD dwCookie) = 0;
};
typedef ISimpleFrameSite FAR *LPSIMPLEFRAMESITE;

typedef enum tagPOINTERINACTIVE {
  POINTERINACTIVE_ACTIVATEONENTRY = 1,
  POINTERINACTIVE_DEACTIVATEONLEAVE = 2,
  POINTERINACTIVE_ACTIVATEONDRAG = 4
} POINTERINACTIVE;
interface IPointerInactive : public IUnknown {
  STDMETHOD(GetActivationPolicy)(DWORD FAR *pdwPolicy) = 0;
  STDMETHOD(OnInactiveMouseMove)(LPCRECT pRectBounds, LONG x, LONG y, DWORD grfKeyState) = 0;
  STDMETHOD(OnInactiveSetCursor)(LPCRECT pRectBounds, LONG x, LONG y, DWORD dwMouseMsg,
                                 BOOL fSetAlways) = 0;
};
typedef IPointerInactive FAR *LPPOINTERINACTIVE;

// Views: the status, the hit tests and the natural extent
typedef enum tagVIEWSTATUS {
  VIEWSTATUS_OPAQUE = 1,
  VIEWSTATUS_SOLIDBKGND = 2,
  VIEWSTATUS_DVASPECTOPAQUE = 4,
  VIEWSTATUS_DVASPECTTRANSPARENT = 8
} VIEWSTATUS;
typedef enum tagHITRESULT {
  HITRESULT_OUTSIDE = 0,
  HITRESULT_TRANSPARENT = 1,
  HITRESULT_CLOSE = 2,
  HITRESULT_HIT = 3
} HITRESULT;
typedef enum tagDVASPECT2 { DVASPECT_OPAQUE = 16, DVASPECT_TRANSPARENT = 32 } DVASPECT2;
typedef struct tagExtentInfo {
  ULONG cb;
  DWORD dwExtentMode;
  SIZEL sizelProposed;
} DVEXTENTINFO;
typedef enum tagExtentMode { DVEXTENT_CONTENT = 0, DVEXTENT_INTEGRAL = 1 } DVEXTENTMODE;
typedef enum tagAspectInfoFlag { DVASPECTINFOFLAG_CANOPTIMIZE = 1 } DVASPECTINFOFLAG;
typedef struct tagAspectInfo {
  ULONG cb;
  DWORD dwFlags;
} DVASPECTINFO;

interface IViewObjectEx : public IViewObject2 {
  STDMETHOD(GetRect)(DWORD dwAspect, LPRECTL pRect) = 0;
  STDMETHOD(GetViewStatus)(DWORD FAR *pdwStatus) = 0;
  STDMETHOD(QueryHitPoint)(DWORD dwAspect, LPCRECT pRectBounds, POINT ptlLoc, LONG lCloseHint,
                           DWORD FAR *pHitResult) = 0;
  STDMETHOD(QueryHitRect)(DWORD dwAspect, LPCRECT pRectBounds, LPCRECT pRectLoc, LONG lCloseHint,
                          DWORD FAR *pHitResult) = 0;
  STDMETHOD(GetNaturalExtent)(DWORD dwAspect, LONG lindex, DVTARGETDEVICE FAR *ptd, HDC hicTargetDev,
                              DVEXTENTINFO FAR *pExtentInfo, LPSIZEL pSizel) = 0;
};
typedef IViewObjectEx FAR *LPVIEWOBJECTEX;

// Windowless controls
interface IOleInPlaceObjectWindowless : public IOleInPlaceObject {
  STDMETHOD(OnWindowMessage)(UINT msg, WPARAM wParam, LPARAM lParam, LRESULT FAR *plResult) = 0;
  STDMETHOD(GetDropTarget)(IDropTarget FAR *FAR *ppDropTarget) = 0;
};
typedef IOleInPlaceObjectWindowless FAR *LPOLEINPLACEOBJECTWINDOWLESS;

#define ACTIVATE_WINDOWLESS 1
interface IOleInPlaceSiteEx : public IOleInPlaceSite {
  STDMETHOD(OnInPlaceActivateEx)(BOOL FAR *pfNoRedraw, DWORD dwFlags) = 0;
  STDMETHOD(OnInPlaceDeactivateEx)(BOOL fNoRedraw) = 0;
  STDMETHOD(RequestUIActivate)() = 0;
};
typedef IOleInPlaceSiteEx FAR *LPOLEINPLACESITEEX;

#define OLEDC_NODRAW 0x01
#define OLEDC_PAINTBKGND 0x02
#define OLEDC_OFFSCREEN 0x04
interface IOleInPlaceSiteWindowless : public IOleInPlaceSiteEx {
  STDMETHOD(CanWindowlessActivate)() = 0;
  STDMETHOD(GetCapture)() = 0;
  STDMETHOD(SetCapture)(BOOL fCapture) = 0;
  STDMETHOD(GetFocus)() = 0;
  STDMETHOD(SetFocus)(BOOL fFocus) = 0;
  STDMETHOD(GetDC)(LPCRECT pRect, DWORD grfFlags, HDC FAR *phDC) = 0;
  STDMETHOD(ReleaseDC)(HDC hDC) = 0;
  STDMETHOD(InvalidateRect)(LPCRECT pRect, BOOL fErase) = 0;
  STDMETHOD(InvalidateRgn)(HRGN hRGN, BOOL fErase) = 0;
  STDMETHOD(ScrollRect)(int dx, int dy, LPCRECT pRectScroll, LPCRECT pRectClip) = 0;
  STDMETHOD(AdjustRect)(LPRECT prc) = 0;
  STDMETHOD(OnDefWindowMessage)(UINT msg, WPARAM wParam, LPARAM lParam, LRESULT FAR *plResult) = 0;
};
typedef IOleInPlaceSiteWindowless FAR *LPOLEINPLACESITEWINDOWLESS;

interface IAdviseSinkEx : public IAdviseSink {
  STDMETHOD_(void, OnViewStatusChange)(DWORD dwViewStatus) = 0;
};
typedef IAdviseSinkEx FAR *LPADVISESINKEX;

// Quick activation
typedef enum tagQACONTAINERFLAGS {
  QACONTAINER_SHOWHATCHING = 0x1,
  QACONTAINER_SHOWGRABHANDLES = 0x2,
  QACONTAINER_USERMODE = 0x4,
  QACONTAINER_DISPLAYASDEFAULT = 0x8,
  QACONTAINER_UIDEAD = 0x10,
  QACONTAINER_AUTOCLIP = 0x20,
  QACONTAINER_MESSAGEREFLECT = 0x40,
  QACONTAINER_SUPPORTSMNEMONICS = 0x80
} QACONTAINERFLAGS;
typedef struct tagQACONTAINER {
  ULONG cbSize;
  IOleClientSite FAR *pClientSite;
  IAdviseSinkEx FAR *pAdviseSink;
  IPropertyNotifySink FAR *pPropertyNotifySink;
  IUnknown FAR *pUnkEventSink;
  DWORD dwAmbientFlags;
  OLE_COLOR colorFore;
  OLE_COLOR colorBack;
  IFont FAR *pFont;
  IUnknown FAR *pUndoMgr;
  DWORD dwAppearance;
  LONG lcid;
  HPALETTE hpal;
  IUnknown FAR *pBindHost;
  IUnknown FAR *pOleControlSite;
  IUnknown FAR *pServiceProvider;
} QACONTAINER;
typedef struct tagQACONTROL {
  ULONG cbSize;
  DWORD dwMiscStatus;
  DWORD dwViewStatus;
  DWORD dwEventCookie;
  DWORD dwPropNotifyCookie;
  DWORD dwPointerActivationPolicy;
} QACONTROL;
interface IQuickActivate : public IUnknown {
  STDMETHOD(QuickActivate)(QACONTAINER FAR *pQaContainer, QACONTROL FAR *pQaControl) = 0;
  STDMETHOD(SetContentExtent)(LPSIZEL pSizel) = 0;
  STDMETHOD(GetContentExtent)(LPSIZEL pSizel) = 0;
};
typedef IQuickActivate FAR *LPQUICKACTIVATE;

typedef enum tagREADYSTATE {
  READYSTATE_UNINITIALIZED = 0,
  READYSTATE_LOADING = 1,
  READYSTATE_LOADED = 2,
  READYSTATE_INTERACTIVE = 3,
  READYSTATE_COMPLETE = 4
} READYSTATE;

interface IObjectWithSite : public IUnknown {
  STDMETHOD(SetSite)(IUnknown FAR *pUnkSite) = 0;
  STDMETHOD(GetSite)(REFIID riid, void FAR *FAR *ppvSite) = 0;
};
typedef IObjectWithSite FAR *LPOBJECTWITHSITE;

interface IOleUndoManager;

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
WINPORT_UUIDOF(IServiceProvider)
WINPORT_UUIDOF(IPersistStreamInit)
WINPORT_UUIDOF(IPersistMemory)
WINPORT_UUIDOF(IPersistPropertyBag)
WINPORT_UUIDOF(IPropertyNotifySink)
WINPORT_UUIDOF(ISpecifyPropertyPages)
WINPORT_UUIDOF(IFont)
WINPORT_UUIDOF(IPicture)
WINPORT_UUIDOF(IFontDisp)
WINPORT_UUIDOF(IPictureDisp)
WINPORT_UUIDOF(IFontEventsDisp)
WINPORT_UUIDOF(IPropertyPageSite)
WINPORT_UUIDOF(IPropertyPage)
WINPORT_UUIDOF(IPropertyPage2)
WINPORT_UUIDOF(IPerPropertyBrowsing)
WINPORT_UUIDOF(IOleControl)
WINPORT_UUIDOF(IOleControlSite)
WINPORT_UUIDOF(ISimpleFrameSite)
WINPORT_UUIDOF(IPointerInactive)
WINPORT_UUIDOF(IViewObjectEx)
WINPORT_UUIDOF(IOleInPlaceObjectWindowless)
WINPORT_UUIDOF(IOleInPlaceSiteEx)
WINPORT_UUIDOF(IOleInPlaceSiteWindowless)
WINPORT_UUIDOF(IAdviseSinkEx)
WINPORT_UUIDOF(IQuickActivate)
WINPORT_UUIDOF(IObjectWithSite)
WINPORT_UUIDOF(IErrorLog)
WINPORT_UUIDOF(IPropertyBag)
