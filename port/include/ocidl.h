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

// Fonts and pictures
typedef DWORD OLE_COLOR;
typedef LONG OLE_XPOS_PIXELS;
typedef LONG OLE_YPOS_PIXELS;
typedef LONG OLE_XSIZE_PIXELS;
typedef LONG OLE_YSIZE_PIXELS;
typedef LONG OLE_XPOS_HIMETRIC;
typedef LONG OLE_YPOS_HIMETRIC;
typedef LONG OLE_XSIZE_HIMETRIC;
typedef LONG OLE_YSIZE_HIMETRIC;
typedef float OLE_XPOS_CONTAINER;
typedef float OLE_YPOS_CONTAINER;
typedef float OLE_XSIZE_CONTAINER;
typedef float OLE_YSIZE_CONTAINER;
typedef UINT OLE_HANDLE;
typedef VARIANT_BOOL OLE_OPTEXCLUSIVE;
typedef VARIANT_BOOL OLE_CANCELBOOL;
typedef VARIANT_BOOL OLE_ENABLEDEFAULTBOOL;
typedef enum { triUnchecked = 0, triChecked = 1, triGray = 2 } OLE_TRISTATE;

interface IFont : public IUnknown {
  STDMETHOD(get_Name)(BSTR* pName) = 0;
  STDMETHOD(put_Name)(BSTR name) = 0;
  STDMETHOD(get_Size)(CY* pSize) = 0;
  STDMETHOD(put_Size)(CY size) = 0;
  STDMETHOD(get_Bold)(BOOL* pBold) = 0;
  STDMETHOD(put_Bold)(BOOL bold) = 0;
  STDMETHOD(get_Italic)(BOOL* pItalic) = 0;
  STDMETHOD(put_Italic)(BOOL italic) = 0;
  STDMETHOD(get_Underline)(BOOL* pUnderline) = 0;
  STDMETHOD(put_Underline)(BOOL underline) = 0;
  STDMETHOD(get_Strikethrough)(BOOL* pStrikethrough) = 0;
  STDMETHOD(put_Strikethrough)(BOOL strikethrough) = 0;
  STDMETHOD(get_Weight)(SHORT* pWeight) = 0;
  STDMETHOD(put_Weight)(SHORT weight) = 0;
  STDMETHOD(get_Charset)(SHORT* pCharset) = 0;
  STDMETHOD(put_Charset)(SHORT charset) = 0;
  STDMETHOD(get_hFont)(HFONT* phFont) = 0;
  STDMETHOD(Clone)(IFont** ppFont) = 0;
  STDMETHOD(IsEqual)(IFont* pFontOther) = 0;
  STDMETHOD(SetRatio)(LONG cyLogical, LONG cyHimetric) = 0;
  STDMETHOD(QueryTextMetrics)(TEXTMETRICW* pTM) = 0;
  STDMETHOD(AddRefHfont)(HFONT hFont) = 0;
  STDMETHOD(ReleaseHfont)(HFONT hFont) = 0;
  STDMETHOD(SetHdc)(HDC hDC) = 0;
};
typedef IFont* LPFONT;

interface IPicture : public IUnknown {
  STDMETHOD(get_Handle)(OLE_HANDLE* pHandle) = 0;
  STDMETHOD(get_hPal)(OLE_HANDLE* phPal) = 0;
  STDMETHOD(get_Type)(SHORT* pType) = 0;
  STDMETHOD(get_Width)(OLE_XSIZE_HIMETRIC* pWidth) = 0;
  STDMETHOD(get_Height)(OLE_YSIZE_HIMETRIC* pHeight) = 0;
  STDMETHOD(Render)(HDC hDC, LONG x, LONG y, LONG cx, LONG cy, OLE_XPOS_HIMETRIC xSrc, OLE_YPOS_HIMETRIC ySrc,
                    OLE_XSIZE_HIMETRIC cxSrc, OLE_YSIZE_HIMETRIC cySrc, LPCRECT pRcWBounds) = 0;
  STDMETHOD(set_hPal)(OLE_HANDLE hPal) = 0;
  STDMETHOD(get_CurDC)(HDC* phDC) = 0;
  STDMETHOD(SelectPicture)(HDC hDCIn, HDC* phDCOut, OLE_HANDLE* phBmpOut) = 0;
  STDMETHOD(get_KeepOriginalFormat)(BOOL* pKeep) = 0;
  STDMETHOD(put_KeepOriginalFormat)(BOOL keep) = 0;
  STDMETHOD(PictureChanged)() = 0;
  STDMETHOD(SaveAsFile)(LPSTREAM pStream, BOOL fSaveMemCopy, LONG* pCbSize) = 0;
  STDMETHOD(get_Attributes)(DWORD* pDwAttr) = 0;
};
typedef IPicture* LPPICTURE;

interface IFontDisp : public IDispatch {};
typedef IFontDisp* LPFONTDISP;
interface IPictureDisp : public IDispatch {};
typedef IPictureDisp* LPPICTUREDISP;
interface IFontEventsDisp : public IDispatch {};

// Property pages
typedef struct tagPROPPAGEINFO {
  ULONG cb;
  LPOLESTR pszTitle;
  SIZE size;
  LPOLESTR pszDocString;
  LPOLESTR pszHelpFile;
  DWORD dwHelpContext;
} PROPPAGEINFO, *LPPROPPAGEINFO;

interface IPropertyPageSite : public IUnknown {
  STDMETHOD(OnStatusChange)(DWORD dwFlags) = 0;
  STDMETHOD(GetLocaleID)(LCID* pLocaleID) = 0;
  STDMETHOD(GetPageContainer)(IUnknown** ppUnk) = 0;
  STDMETHOD(TranslateAccelerator)(MSG* pMsg) = 0;
};
typedef IPropertyPageSite* LPPROPERTYPAGESITE;
#define PROPPAGESTATUS_DIRTY 0x01
#define PROPPAGESTATUS_VALIDATE 0x02
#define PROPPAGESTATUS_CLEAN 0x04

interface IPropertyPage : public IUnknown {
  STDMETHOD(SetPageSite)(IPropertyPageSite* pPageSite) = 0;
  STDMETHOD(Activate)(HWND hWndParent, LPCRECT pRect, BOOL bModal) = 0;
  STDMETHOD(Deactivate)() = 0;
  STDMETHOD(GetPageInfo)(PROPPAGEINFO* pPageInfo) = 0;
  STDMETHOD(SetObjects)(ULONG cObjects, IUnknown** ppUnk) = 0;
  STDMETHOD(Show)(UINT nCmdShow) = 0;
  STDMETHOD(Move)(LPCRECT pRect) = 0;
  STDMETHOD(IsPageDirty)() = 0;
  STDMETHOD(Apply)() = 0;
  STDMETHOD(Help)(LPCOLESTR pszHelpDir) = 0;
  STDMETHOD(TranslateAccelerator)(MSG* pMsg) = 0;
};
typedef IPropertyPage* LPPROPERTYPAGE;

interface IPropertyPage2 : public IPropertyPage {
  STDMETHOD(EditProperty)(DISPID dispID) = 0;
};
typedef IPropertyPage2* LPPROPERTYPAGE2;

interface IPerPropertyBrowsing : public IUnknown {
  STDMETHOD(GetDisplayString)(DISPID dispID, BSTR* pBstr) = 0;
  STDMETHOD(MapPropertyToPage)(DISPID dispID, CLSID* pClsid) = 0;
  STDMETHOD(GetPredefinedStrings)(DISPID dispID, CALPOLESTR* pCaStringsOut, CADWORD* pCaCookiesOut) = 0;
  STDMETHOD(GetPredefinedValue)(DISPID dispID, DWORD dwCookie, VARIANT* pVarOut) = 0;
};
typedef IPerPropertyBrowsing* LPPERPROPERTYBROWSING;

// Controls and their sites
typedef struct tagCONTROLINFO {
  ULONG cb;
  HACCEL hAccel;
  USHORT cAccel;
  DWORD dwFlags;
} CONTROLINFO, *LPCONTROLINFO;
#define CTRLINFO_EATS_RETURN 1
#define CTRLINFO_EATS_ESCAPE 2

interface IOleControl : public IUnknown {
  STDMETHOD(GetControlInfo)(CONTROLINFO* pCI) = 0;
  STDMETHOD(OnMnemonic)(MSG* pMsg) = 0;
  STDMETHOD(OnAmbientPropertyChange)(DISPID dispID) = 0;
  STDMETHOD(FreezeEvents)(BOOL bFreeze) = 0;
};
typedef IOleControl* LPOLECONTROL;

typedef struct _POINTF {
  FLOAT x;
  FLOAT y;
} POINTF, *LPPOINTF;
#define XFORMCOORDS_POSITION 0x1
#define XFORMCOORDS_SIZE 0x2
#define XFORMCOORDS_HIMETRICTOCONTAINER 0x4
#define XFORMCOORDS_CONTAINERTOHIMETRIC 0x8
#define XFORMCOORDS_EVENTCOMPAT 0x10

interface IOleControlSite : public IUnknown {
  STDMETHOD(OnControlInfoChanged)() = 0;
  STDMETHOD(LockInPlaceActive)(BOOL fLock) = 0;
  STDMETHOD(GetExtendedControl)(IDispatch** ppDisp) = 0;
  STDMETHOD(TransformCoords)(POINTL* pPtlHimetric, POINTF* pPtfContainer, DWORD dwFlags) = 0;
  STDMETHOD(TranslateAccelerator)(MSG* pMsg, DWORD grfModifiers) = 0;
  STDMETHOD(OnFocus)(BOOL fGotFocus) = 0;
  STDMETHOD(ShowPropertyFrame)() = 0;
};
typedef IOleControlSite* LPOLECONTROLSITE;

interface ISimpleFrameSite : public IUnknown {
  STDMETHOD(PreMessageFilter)(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp, LRESULT* plResult, DWORD* pdwCookie) = 0;
  STDMETHOD(PostMessageFilter)(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp, LRESULT* plResult, DWORD dwCookie) = 0;
};
typedef ISimpleFrameSite* LPSIMPLEFRAMESITE;

typedef struct tagLICINFO {
  LONG cbLicInfo;
  BOOL fRuntimeKeyAvail;
  BOOL fLicVerified;
} LICINFO, *LPLICINFO;

interface IClassFactory2 : public IClassFactory {
  STDMETHOD(GetLicInfo)(LICINFO* pLicInfo) = 0;
  STDMETHOD(RequestLicKey)(DWORD dwReserved, BSTR* pBstrKey) = 0;
  STDMETHOD(CreateInstanceLic)(IUnknown* pUnkOuter, IUnknown* pUnkReserved, REFIID riid, BSTR bstrKey,
                               PVOID* ppvObj) = 0;
};
typedef IClassFactory2* LPCLASSFACTORY2;

typedef enum tagPOINTERINACTIVE {
  POINTERINACTIVE_ACTIVATEONENTRY = 1,
  POINTERINACTIVE_DEACTIVATEONLEAVE = 2,
  POINTERINACTIVE_ACTIVATEONDRAG = 4
} POINTERINACTIVE;
interface IPointerInactive : public IUnknown {
  STDMETHOD(GetActivationPolicy)(DWORD* pdwPolicy) = 0;
  STDMETHOD(OnInactiveMouseMove)(LPCRECT pRectBounds, LONG x, LONG y, DWORD grfKeyState) = 0;
  STDMETHOD(OnInactiveSetCursor)(LPCRECT pRectBounds, LONG x, LONG y, DWORD dwMouseMsg, BOOL fSetAlways) = 0;
};
typedef IPointerInactive* LPPOINTERINACTIVE;

typedef enum tagVIEWSTATUS {
  VIEWSTATUS_OPAQUE = 1,
  VIEWSTATUS_SOLIDBKGND = 2,
  VIEWSTATUS_DVASPECTOPAQUE = 4,
  VIEWSTATUS_DVASPECTTRANSPARENT = 8
} VIEWSTATUS;
typedef enum tagHITRESULT { HITRESULT_OUTSIDE = 0, HITRESULT_TRANSPARENT = 1, HITRESULT_CLOSE = 2,
  HITRESULT_HIT = 3 } HITRESULT;
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
  STDMETHOD(GetViewStatus)(DWORD* pdwStatus) = 0;
  STDMETHOD(QueryHitPoint)(DWORD dwAspect, LPCRECT pRectBounds, POINT ptlLoc, LONG lCloseHint,
                           DWORD* pHitResult) = 0;
  STDMETHOD(QueryHitRect)(DWORD dwAspect, LPCRECT pRectBounds, LPCRECT pRectLoc, LONG lCloseHint,
                          DWORD* pHitResult) = 0;
  STDMETHOD(GetNaturalExtent)(DWORD dwAspect, LONG lindex, DVTARGETDEVICE* ptd, HDC hicTargetDev,
                              DVEXTENTINFO* pExtentInfo, LPSIZEL pSizel) = 0;
};
typedef IViewObjectEx* LPVIEWOBJECTEX;

interface IOleInPlaceObjectWindowless : public IOleInPlaceObject {
  STDMETHOD(OnWindowMessage)(UINT msg, WPARAM wParam, LPARAM lParam, LRESULT* plResult) = 0;
  STDMETHOD(GetDropTarget)(IDropTarget** ppDropTarget) = 0;
};
typedef IOleInPlaceObjectWindowless* LPOLEINPLACEOBJECTWINDOWLESS;

#define ACTIVATE_WINDOWLESS 1
interface IOleInPlaceSiteEx : public IOleInPlaceSite {
  STDMETHOD(OnInPlaceActivateEx)(BOOL* pfNoRedraw, DWORD dwFlags) = 0;
  STDMETHOD(OnInPlaceDeactivateEx)(BOOL fNoRedraw) = 0;
  STDMETHOD(RequestUIActivate)() = 0;
};
typedef IOleInPlaceSiteEx* LPOLEINPLACESITEEX;

#define OLEDC_NODRAW 0x01
#define OLEDC_PAINTBKGND 0x02
#define OLEDC_OFFSCREEN 0x04
interface IOleInPlaceSiteWindowless : public IOleInPlaceSiteEx {
  STDMETHOD(CanWindowlessActivate)() = 0;
  STDMETHOD(GetCapture)() = 0;
  STDMETHOD(SetCapture)(BOOL fCapture) = 0;
  STDMETHOD(GetFocus)() = 0;
  STDMETHOD(SetFocus)(BOOL fFocus) = 0;
  STDMETHOD(GetDC)(LPCRECT pRect, DWORD grfFlags, HDC* phDC) = 0;
  STDMETHOD(ReleaseDC)(HDC hDC) = 0;
  STDMETHOD(InvalidateRect)(LPCRECT pRect, BOOL fErase) = 0;
  STDMETHOD(InvalidateRgn)(HRGN hRGN, BOOL fErase) = 0;
  STDMETHOD(ScrollRect)(INT dx, INT dy, LPCRECT pRectScroll, LPCRECT pRectClip) = 0;
  STDMETHOD(AdjustRect)(LPRECT prc) = 0;
  STDMETHOD(OnDefWindowMessage)(UINT msg, WPARAM wParam, LPARAM lParam, LRESULT* plResult) = 0;
};
typedef IOleInPlaceSiteWindowless* LPOLEINPLACESITEWINDOWLESS;

interface IAdviseSinkEx : public IAdviseSink {
  STDMETHOD_(void, OnViewStatusChange)(DWORD dwViewStatus) = 0;
};

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
  IOleClientSite* pClientSite;
  IAdviseSinkEx* pAdviseSink;
  IPropertyNotifySink* pPropertyNotifySink;
  IUnknown* pUnkEventSink;
  DWORD dwAmbientFlags;
  OLE_COLOR colorFore;
  OLE_COLOR colorBack;
  IFont* pFont;
  IUnknown* pUndoMgr;
  DWORD dwAppearance;
  LONG lcid;
  HPALETTE hpal;
  IUnknown* pBindHost;
  IUnknown* pOleControlSite;
  IUnknown* pServiceProvider;
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
  STDMETHOD(QuickActivate)(QACONTAINER* pQaContainer, QACONTROL* pQaControl) = 0;
  STDMETHOD(SetContentExtent)(LPSIZEL pSizel) = 0;
  STDMETHOD(GetContentExtent)(LPSIZEL pSizel) = 0;
};
typedef IQuickActivate* LPQUICKACTIVATE;

typedef enum tagREADYSTATE {
  READYSTATE_UNINITIALIZED = 0,
  READYSTATE_LOADING = 1,
  READYSTATE_LOADED = 2,
  READYSTATE_INTERACTIVE = 3,
  READYSTATE_COMPLETE = 4
} READYSTATE;

interface IObjectWithSite : public IUnknown {
  STDMETHOD(SetSite)(IUnknown* pUnkSite) = 0;
  STDMETHOD(GetSite)(REFIID riid, void** ppvSite) = 0;
};

interface IOleUndoManager;

// Services
extern "C" const IID IID_IServiceProvider;
interface IServiceProvider : public IUnknown {
  STDMETHOD(QueryService)(REFGUID guidService, REFIID riid, void** ppvObject) = 0;
};
typedef IServiceProvider* LPSERVICEPROVIDER;

WINPORT_UUIDOF(IEnumConnections)
WINPORT_UUIDOF(IConnectionPoint)
WINPORT_UUIDOF(IEnumConnectionPoints)
WINPORT_UUIDOF(IConnectionPointContainer)
WINPORT_UUIDOF(IProvideClassInfo)
WINPORT_UUIDOF(IProvideClassInfo2)
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
WINPORT_UUIDOF(IClassFactory2)
WINPORT_UUIDOF(IPointerInactive)
WINPORT_UUIDOF(IViewObjectEx)
WINPORT_UUIDOF(IOleInPlaceObjectWindowless)
WINPORT_UUIDOF(IOleInPlaceSiteEx)
WINPORT_UUIDOF(IOleInPlaceSiteWindowless)
WINPORT_UUIDOF(IQuickActivate)
WINPORT_UUIDOF(IObjectWithSite)
WINPORT_UUIDOF(IServiceProvider)

#endif
