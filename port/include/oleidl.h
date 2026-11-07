// oleidl.h - OLE documents: windows, in-place activation, objects and
// their sites, links and containers, caches and views, drag and drop.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#ifndef _WINPORT_OLEIDL_H
#define _WINPORT_OLEIDL_H

#include <objidl.h>

interface IOleWindow;
interface IOleInPlaceObject;
interface IOleInPlaceActiveObject;
interface IOleInPlaceUIWindow;
interface IOleInPlaceFrame;
interface IOleInPlaceSite;
interface IOleObject;
interface IOleClientSite;
interface IOleContainer;
interface IOleItemContainer;
interface IOleLink;
interface IOleAdviseHolder;
interface IOleCache;
interface IOleCache2;
interface IOleCacheControl;
interface IViewObject;
interface IViewObject2;
interface IEnumOLEVERB;
interface IRunnableObject;
interface IParseDisplayName;
interface IDropTarget;
interface IDropSource;

extern "C" {
extern const IID IID_IOleWindow, IID_IOleInPlaceObject, IID_IOleInPlaceActiveObject, IID_IOleInPlaceUIWindow,
    IID_IOleInPlaceFrame, IID_IOleInPlaceSite, IID_IOleObject, IID_IOleClientSite, IID_IOleContainer,
    IID_IOleItemContainer, IID_IOleLink, IID_IOleAdviseHolder, IID_IOleCache, IID_IOleCache2,
    IID_IOleCacheControl, IID_IViewObject, IID_IViewObject2, IID_IEnumOLEVERB, IID_IRunnableObject,
    IID_IParseDisplayName, IID_IDropTarget, IID_IDropSource, IID_IContinue;
}

typedef struct tagOleMenuGroupWidths {
  LONG width[6];
} OLEMENUGROUPWIDTHS, *LPOLEMENUGROUPWIDTHS;
typedef HGLOBAL HOLEMENU;
// Clipboard descriptions of embedded and linked objects. The strings
// follow the structure, at the offsets given.
typedef struct tagOBJECTDESCRIPTOR {
  ULONG cbSize;
  CLSID clsid;
  DWORD dwDrawAspect;
  SIZEL sizel;
  POINTL pointl;
  DWORD dwStatus;
  DWORD dwFullUserTypeName;
  DWORD dwSrcOfCopy;
} OBJECTDESCRIPTOR, *POBJECTDESCRIPTOR, *LPOBJECTDESCRIPTOR, LINKSRCDESCRIPTOR, *PLINKSRCDESCRIPTOR,
    *LPLINKSRCDESCRIPTOR;

typedef struct tagOIFI {
  UINT cb;
  BOOL fMDIApp;
  HWND hwndFrame;
  HACCEL haccel;
  UINT cAccelEntries;
} OLEINPLACEFRAMEINFO, *LPOLEINPLACEFRAMEINFO;
typedef RECT BORDERWIDTHS;
typedef LPRECT LPBORDERWIDTHS;
typedef LPCRECT LPCBORDERWIDTHS;
typedef struct tagOLEVERB {
  LONG lVerb;
  LPOLESTR lpszVerbName;
  DWORD fuFlags;
  DWORD grfAttribs;
} OLEVERB, *LPOLEVERB;

typedef enum tagOLEGETMONIKER { OLEGETMONIKER_ONLYIFTHERE = 1, OLEGETMONIKER_FORCEASSIGN = 2,
  OLEGETMONIKER_UNASSIGN = 3, OLEGETMONIKER_TEMPFORUSER = 4 } OLEGETMONIKER;
typedef enum tagOLEWHICHMK { OLEWHICHMK_CONTAINER = 1, OLEWHICHMK_OBJREL = 2, OLEWHICHMK_OBJFULL = 3 } OLEWHICHMK;
typedef enum tagUSERCLASSTYPE { USERCLASSTYPE_FULL = 1, USERCLASSTYPE_SHORT = 2, USERCLASSTYPE_APPNAME = 3 }
    USERCLASSTYPE;
typedef enum tagOLEMISC {
  OLEMISC_RECOMPOSEONRESIZE = 0x1,
  OLEMISC_ONLYICONIC = 0x2,
  OLEMISC_INSERTNOTREPLACE = 0x4,
  OLEMISC_STATIC = 0x8,
  OLEMISC_CANTLINKINSIDE = 0x10,
  OLEMISC_CANLINKBYOLE1 = 0x20,
  OLEMISC_ISLINKOBJECT = 0x40,
  OLEMISC_INSIDEOUT = 0x80,
  OLEMISC_ACTIVATEWHENVISIBLE = 0x100,
  OLEMISC_RENDERINGISDEVICEINDEPENDENT = 0x200,
  OLEMISC_INVISIBLEATRUNTIME = 0x400,
  OLEMISC_ALWAYSRUN = 0x800,
  OLEMISC_ACTSLIKEBUTTON = 0x1000,
  OLEMISC_ACTSLIKELABEL = 0x2000,
  OLEMISC_NOUIACTIVATE = 0x4000,
  OLEMISC_ALIGNABLE = 0x8000,
  OLEMISC_SIMPLEFRAME = 0x10000,
  OLEMISC_SETCLIENTSITEFIRST = 0x20000,
  OLEMISC_IMEMODE = 0x40000,
  OLEMISC_IGNOREACTIVATEWHENVISIBLE = 0x80000,
  OLEMISC_WANTSTOMENUMERGE = 0x100000,
  OLEMISC_SUPPORTSMULTILEVELUNDO = 0x200000
} OLEMISC;
typedef enum tagOLECLOSE { OLECLOSE_SAVEIFDIRTY = 0, OLECLOSE_NOSAVE = 1, OLECLOSE_PROMPTSAVE = 2 } OLECLOSE;
typedef enum tagOLERENDER { OLERENDER_NONE = 0, OLERENDER_DRAW = 1, OLERENDER_FORMAT = 2, OLERENDER_ASIS = 3 }
    OLERENDER;
typedef OLERENDER* LPOLERENDER;
typedef enum tagOLEUPDATE { OLEUPDATE_ALWAYS = 1, OLEUPDATE_ONCALL = 3 } OLEUPDATE;
typedef OLEUPDATE* LPOLEUPDATE;
typedef enum tagOLELINKBIND { OLELINKBIND_EVENIFCLASSDIFF = 1 } OLELINKBIND;
typedef enum tagBINDSPEED { BINDSPEED_INDEFINITE = 1, BINDSPEED_MODERATE = 2, BINDSPEED_IMMEDIATE = 3 } BINDSPEED;
typedef enum tagOLECONTF {
  OLECONTF_EMBEDDINGS = 1,
  OLECONTF_LINKS = 2,
  OLECONTF_OTHERS = 4,
  OLECONTF_ONLYUSER = 8,
  OLECONTF_ONLYIFRUNNING = 16
} OLECONTF;
typedef enum tagOLEVERBATTRIB { OLEVERBATTRIB_NEVERDIRTIES = 1, OLEVERBATTRIB_ONCONTAINERMENU = 2 } OLEVERBATTRIB;
typedef enum tagDISCARDCACHE { DISCARDCACHE_SAVEIFDIRTY = 0, DISCARDCACHE_NOSAVE = 1 } DISCARDCACHE;

#define OLEIVERB_PRIMARY (0L)
#define OLEIVERB_SHOW (-1L)
#define OLEIVERB_OPEN (-2L)
#define OLEIVERB_HIDE (-3L)
#define OLEIVERB_UIACTIVATE (-4L)
#define OLEIVERB_INPLACEACTIVATE (-5L)
#define OLEIVERB_DISCARDUNDOSTATE (-6L)
#define EMBDHLP_INPROC_HANDLER 0x0000L
#define EMBDHLP_INPROC_SERVER 0x0001L
#define EMBDHLP_CREATENOW 0x00000000L
#define EMBDHLP_DELAYCREATE 0x00010000L
#define OLECREATE_LEAVERUNNING 0x00000001

#define MK_ALT 0x20
#define DROPEFFECT_NONE 0
#define DROPEFFECT_COPY 1
#define DROPEFFECT_MOVE 2
#define DROPEFFECT_LINK 4
#define DROPEFFECT_SCROLL 0x80000000
#define DD_DEFSCROLLINSET 11
#define DD_DEFSCROLLDELAY 50
#define DD_DEFSCROLLINTERVAL 50
#define DD_DEFDRAGDELAY 200
#define DD_DEFDRAGMINDIST 2

interface IOleAdviseHolder : public IUnknown {
  STDMETHOD(Advise)(IAdviseSink* pAdvise, DWORD* pdwConnection) = 0;
  STDMETHOD(Unadvise)(DWORD dwConnection) = 0;
  STDMETHOD(EnumAdvise)(IEnumSTATDATA** ppenumAdvise) = 0;
  STDMETHOD(SendOnRename)(IMoniker* pmk) = 0;
  STDMETHOD(SendOnSave)() = 0;
  STDMETHOD(SendOnClose)() = 0;
};
typedef IOleAdviseHolder* LPOLEADVISEHOLDER;

interface IOleCache : public IUnknown {
  STDMETHOD(Cache)(FORMATETC* pformatetc, DWORD advf, DWORD* pdwConnection) = 0;
  STDMETHOD(Uncache)(DWORD dwConnection) = 0;
  STDMETHOD(EnumCache)(IEnumSTATDATA** ppenumSTATDATA) = 0;
  STDMETHOD(InitCache)(IDataObject* pDataObject) = 0;
  STDMETHOD(SetData)(FORMATETC* pformatetc, STGMEDIUM* pmedium, BOOL fRelease) = 0;
};
typedef IOleCache* LPOLECACHE;

#define UPDFCACHE_NODATACACHE 0x00000001
#define UPDFCACHE_ONSAVECACHE 0x00000002
#define UPDFCACHE_ONSTOPCACHE 0x00000004
#define UPDFCACHE_NORMALCACHE 0x00000008
#define UPDFCACHE_IFBLANK 0x00000010
#define UPDFCACHE_ONLYIFBLANK 0x80000000
#define UPDFCACHE_IFBLANKORONSAVECACHE (UPDFCACHE_IFBLANK | UPDFCACHE_ONSAVECACHE)
#define UPDFCACHE_ALL ((DWORD)~UPDFCACHE_ONLYIFBLANK)
#define UPDFCACHE_ALLBUTNODATACACHE (UPDFCACHE_ALL & (DWORD)~UPDFCACHE_NODATACACHE)
interface IOleCache2 : public IOleCache {
  STDMETHOD(UpdateCache)(LPDATAOBJECT pDataObject, DWORD grfUpdf, LPVOID pReserved) = 0;
  STDMETHOD(DiscardCache)(DWORD dwDiscardOptions) = 0;
};
typedef IOleCache2* LPOLECACHE2;

interface IOleCacheControl : public IUnknown {
  STDMETHOD(OnRun)(LPDATAOBJECT pDataObject) = 0;
  STDMETHOD(OnStop)() = 0;
};
typedef IOleCacheControl* LPOLECACHECONTROL;

interface IParseDisplayName : public IUnknown {
  STDMETHOD(ParseDisplayName)(IBindCtx* pbc, LPOLESTR pszDisplayName, ULONG* pchEaten, IMoniker** ppmkOut) = 0;
};
typedef IParseDisplayName* LPPARSEDISPLAYNAME;

interface IOleContainer : public IParseDisplayName {
  STDMETHOD(EnumObjects)(DWORD grfFlags, IEnumUnknown** ppenum) = 0;
  STDMETHOD(LockContainer)(BOOL fLock) = 0;
};
typedef IOleContainer* LPOLECONTAINER;

interface IOleClientSite : public IUnknown {
  STDMETHOD(SaveObject)() = 0;
  STDMETHOD(GetMoniker)(DWORD dwAssign, DWORD dwWhichMoniker, IMoniker** ppmk) = 0;
  STDMETHOD(GetContainer)(IOleContainer** ppContainer) = 0;
  STDMETHOD(ShowObject)() = 0;
  STDMETHOD(OnShowWindow)(BOOL fShow) = 0;
  STDMETHOD(RequestNewObjectLayout)() = 0;
};
typedef IOleClientSite* LPOLECLIENTSITE;

interface IOleObject : public IUnknown {
  STDMETHOD(SetClientSite)(IOleClientSite* pClientSite) = 0;
  STDMETHOD(GetClientSite)(IOleClientSite** ppClientSite) = 0;
  STDMETHOD(SetHostNames)(LPCOLESTR szContainerApp, LPCOLESTR szContainerObj) = 0;
  STDMETHOD(Close)(DWORD dwSaveOption) = 0;
  STDMETHOD(SetMoniker)(DWORD dwWhichMoniker, IMoniker* pmk) = 0;
  STDMETHOD(GetMoniker)(DWORD dwAssign, DWORD dwWhichMoniker, IMoniker** ppmk) = 0;
  STDMETHOD(InitFromData)(IDataObject* pDataObject, BOOL fCreation, DWORD dwReserved) = 0;
  STDMETHOD(GetClipboardData)(DWORD dwReserved, IDataObject** ppDataObject) = 0;
  STDMETHOD(DoVerb)(LONG iVerb, LPMSG lpmsg, IOleClientSite* pActiveSite, LONG lindex, HWND hwndParent,
                    LPCRECT lprcPosRect) = 0;
  STDMETHOD(EnumVerbs)(IEnumOLEVERB** ppEnumOleVerb) = 0;
  STDMETHOD(Update)() = 0;
  STDMETHOD(IsUpToDate)() = 0;
  STDMETHOD(GetUserClassID)(CLSID* pClsid) = 0;
  STDMETHOD(GetUserType)(DWORD dwFormOfType, LPOLESTR* pszUserType) = 0;
  STDMETHOD(SetExtent)(DWORD dwDrawAspect, SIZEL* psizel) = 0;
  STDMETHOD(GetExtent)(DWORD dwDrawAspect, SIZEL* psizel) = 0;
  STDMETHOD(Advise)(IAdviseSink* pAdvSink, DWORD* pdwConnection) = 0;
  STDMETHOD(Unadvise)(DWORD dwConnection) = 0;
  STDMETHOD(EnumAdvise)(IEnumSTATDATA** ppenumAdvise) = 0;
  STDMETHOD(GetMiscStatus)(DWORD dwAspect, DWORD* pdwStatus) = 0;
  STDMETHOD(SetColorScheme)(LOGPALETTE* pLogpal) = 0;
};
typedef IOleObject* LPOLEOBJECT;

interface IOleWindow : public IUnknown {
  STDMETHOD(GetWindow)(HWND* phwnd) = 0;
  STDMETHOD(ContextSensitiveHelp)(BOOL fEnterMode) = 0;
};
typedef IOleWindow* LPOLEWINDOW;

interface IOleLink : public IUnknown {
  STDMETHOD(SetUpdateOptions)(DWORD dwUpdateOpt) = 0;
  STDMETHOD(GetUpdateOptions)(DWORD* pdwUpdateOpt) = 0;
  STDMETHOD(SetSourceMoniker)(IMoniker* pmk, REFCLSID rclsid) = 0;
  STDMETHOD(GetSourceMoniker)(IMoniker** ppmk) = 0;
  STDMETHOD(SetSourceDisplayName)(LPCOLESTR pszStatusText) = 0;
  STDMETHOD(GetSourceDisplayName)(LPOLESTR* ppszDisplayName) = 0;
  STDMETHOD(BindToSource)(DWORD bindflags, IBindCtx* pbc) = 0;
  STDMETHOD(BindIfRunning)() = 0;
  STDMETHOD(GetBoundSource)(IUnknown** ppunk) = 0;
  STDMETHOD(UnbindSource)() = 0;
  STDMETHOD(Update)(IBindCtx* pbc) = 0;
};
typedef IOleLink* LPOLELINK;

interface IOleItemContainer : public IOleContainer {
  STDMETHOD(GetObject)(LPOLESTR pszItem, DWORD dwSpeedNeeded, IBindCtx* pbc, REFIID riid, void** ppvObject) = 0;
  STDMETHOD(GetObjectStorage)(LPOLESTR pszItem, IBindCtx* pbc, REFIID riid, void** ppvStorage) = 0;
  STDMETHOD(IsRunning)(LPOLESTR pszItem) = 0;
};
typedef IOleItemContainer* LPOLEITEMCONTAINER;

interface IOleInPlaceUIWindow : public IOleWindow {
  STDMETHOD(GetBorder)(LPRECT lprectBorder) = 0;
  STDMETHOD(RequestBorderSpace)(LPCBORDERWIDTHS pborderwidths) = 0;
  STDMETHOD(SetBorderSpace)(LPCBORDERWIDTHS pborderwidths) = 0;
  STDMETHOD(SetActiveObject)(IOleInPlaceActiveObject* pActiveObject, LPCOLESTR pszObjName) = 0;
};
typedef IOleInPlaceUIWindow* LPOLEINPLACEUIWINDOW;

interface IOleInPlaceActiveObject : public IOleWindow {
  STDMETHOD(TranslateAccelerator)(LPMSG lpmsg) = 0;
  STDMETHOD(OnFrameWindowActivate)(BOOL fActivate) = 0;
  STDMETHOD(OnDocWindowActivate)(BOOL fActivate) = 0;
  STDMETHOD(ResizeBorder)(LPCRECT prcBorder, IOleInPlaceUIWindow* pUIWindow, BOOL fFrameWindow) = 0;
  STDMETHOD(EnableModeless)(BOOL fEnable) = 0;
};
typedef IOleInPlaceActiveObject* LPOLEINPLACEACTIVEOBJECT;

interface IOleInPlaceFrame : public IOleInPlaceUIWindow {
  STDMETHOD(InsertMenus)(HMENU hmenuShared, LPOLEMENUGROUPWIDTHS lpMenuWidths) = 0;
  STDMETHOD(SetMenu)(HMENU hmenuShared, HOLEMENU holemenu, HWND hwndActiveObject) = 0;
  STDMETHOD(RemoveMenus)(HMENU hmenuShared) = 0;
  STDMETHOD(SetStatusText)(LPCOLESTR pszStatusText) = 0;
  STDMETHOD(EnableModeless)(BOOL fEnable) = 0;
  STDMETHOD(TranslateAccelerator)(LPMSG lpmsg, WORD wID) = 0;
};
typedef IOleInPlaceFrame* LPOLEINPLACEFRAME;

interface IOleInPlaceObject : public IOleWindow {
  STDMETHOD(InPlaceDeactivate)() = 0;
  STDMETHOD(UIDeactivate)() = 0;
  STDMETHOD(SetObjectRects)(LPCRECT lprcPosRect, LPCRECT lprcClipRect) = 0;
  STDMETHOD(ReactivateAndUndo)() = 0;
};
typedef IOleInPlaceObject* LPOLEINPLACEOBJECT;

interface IOleInPlaceSite : public IOleWindow {
  STDMETHOD(CanInPlaceActivate)() = 0;
  STDMETHOD(OnInPlaceActivate)() = 0;
  STDMETHOD(OnUIActivate)() = 0;
  STDMETHOD(GetWindowContext)(IOleInPlaceFrame** ppFrame, IOleInPlaceUIWindow** ppDoc, LPRECT lprcPosRect,
                              LPRECT lprcClipRect, LPOLEINPLACEFRAMEINFO lpFrameInfo) = 0;
  STDMETHOD(Scroll)(SIZE scrollExtant) = 0;
  STDMETHOD(OnUIDeactivate)(BOOL fUndoable) = 0;
  STDMETHOD(OnInPlaceDeactivate)() = 0;
  STDMETHOD(DiscardUndoState)() = 0;
  STDMETHOD(DeactivateAndUndo)() = 0;
  STDMETHOD(OnPosRectChange)(LPCRECT lprcPosRect) = 0;
};
typedef IOleInPlaceSite* LPOLEINPLACESITE;

interface IContinue : public IUnknown {
  STDMETHOD(FContinue)() = 0;
};

interface IViewObject : public IUnknown {
  STDMETHOD(Draw)(DWORD dwDrawAspect, LONG lindex, void* pvAspect, DVTARGETDEVICE* ptd, HDC hdcTargetDev,
                  HDC hdcDraw, LPCRECTL lprcBounds, LPCRECTL lprcWBounds, BOOL(STDMETHODCALLTYPE* pfnContinue)(ULONG_PTR),
                  ULONG_PTR dwContinue) = 0;
  STDMETHOD(GetColorSet)(DWORD dwDrawAspect, LONG lindex, void* pvAspect, DVTARGETDEVICE* ptd, HDC hicTargetDev,
                         LOGPALETTE** ppColorSet) = 0;
  STDMETHOD(Freeze)(DWORD dwDrawAspect, LONG lindex, void* pvAspect, DWORD* pdwFreeze) = 0;
  STDMETHOD(Unfreeze)(DWORD dwFreeze) = 0;
  STDMETHOD(SetAdvise)(DWORD aspects, DWORD advf, IAdviseSink* pAdvSink) = 0;
  STDMETHOD(GetAdvise)(DWORD* pAspects, DWORD* pAdvf, IAdviseSink** ppAdvSink) = 0;
};
typedef IViewObject* LPVIEWOBJECT;

interface IViewObject2 : public IViewObject {
  STDMETHOD(GetExtent)(DWORD dwDrawAspect, LONG lindex, DVTARGETDEVICE* ptd, LPSIZEL lpsizel) = 0;
};
typedef IViewObject2* LPVIEWOBJECT2;

interface IDropSource : public IUnknown {
  STDMETHOD(QueryContinueDrag)(BOOL fEscapePressed, DWORD grfKeyState) = 0;
  STDMETHOD(GiveFeedback)(DWORD dwEffect) = 0;
};
typedef IDropSource* LPDROPSOURCE;

interface IDropTarget : public IUnknown {
  STDMETHOD(DragEnter)(IDataObject* pDataObj, DWORD grfKeyState, POINTL pt, DWORD* pdwEffect) = 0;
  STDMETHOD(DragOver)(DWORD grfKeyState, POINTL pt, DWORD* pdwEffect) = 0;
  STDMETHOD(DragLeave)() = 0;
  STDMETHOD(Drop)(IDataObject* pDataObj, DWORD grfKeyState, POINTL pt, DWORD* pdwEffect) = 0;
};
typedef IDropTarget* LPDROPTARGET;

interface IEnumOLEVERB : public IUnknown {
  STDMETHOD(Next)(ULONG celt, LPOLEVERB rgelt, ULONG* pceltFetched) = 0;
  STDMETHOD(Skip)(ULONG celt) = 0;
  STDMETHOD(Reset)() = 0;
  STDMETHOD(Clone)(IEnumOLEVERB** ppenum) = 0;
};
typedef IEnumOLEVERB* LPENUMOLEVERB;

interface IRunnableObject : public IUnknown {
  STDMETHOD(GetRunningClass)(LPCLSID lpClsid) = 0;
  STDMETHOD(Run)(LPBINDCTX pbc) = 0;
  STDMETHOD_(BOOL, IsRunning)() = 0;
  STDMETHOD(LockRunning)(BOOL fLock, BOOL fLastUnlockCloses) = 0;
  STDMETHOD(SetContainedObject)(BOOL fContained) = 0;
};
typedef IRunnableObject* LPRUNNABLEOBJECT;

WINPORT_UUIDOF(IOleAdviseHolder)
WINPORT_UUIDOF(IOleCache)
WINPORT_UUIDOF(IOleCache2)
WINPORT_UUIDOF(IOleCacheControl)
WINPORT_UUIDOF(IParseDisplayName)
WINPORT_UUIDOF(IOleContainer)
WINPORT_UUIDOF(IOleClientSite)
WINPORT_UUIDOF(IOleObject)
WINPORT_UUIDOF(IOleWindow)
WINPORT_UUIDOF(IOleLink)
WINPORT_UUIDOF(IOleItemContainer)
WINPORT_UUIDOF(IOleInPlaceUIWindow)
WINPORT_UUIDOF(IOleInPlaceActiveObject)
WINPORT_UUIDOF(IOleInPlaceFrame)
WINPORT_UUIDOF(IOleInPlaceObject)
WINPORT_UUIDOF(IOleInPlaceSite)
WINPORT_UUIDOF(IContinue)
WINPORT_UUIDOF(IViewObject)
WINPORT_UUIDOF(IViewObject2)
WINPORT_UUIDOF(IDropSource)
WINPORT_UUIDOF(IDropTarget)
WINPORT_UUIDOF(IEnumOLEVERB)
WINPORT_UUIDOF(IRunnableObject)

#endif
