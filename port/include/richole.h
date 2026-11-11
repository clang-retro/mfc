// richole.h - OLE in rich edit controls: embedded object descriptions,
// the control's OLE interface and the container's callback.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#ifndef _WINPORT_RICHOLE_H
#define _WINPORT_RICHOLE_H

#include <oleidl.h>
#include <richedit.h>

interface IRichEditOle;
interface IRichEditOleCallback;

extern "C" {
extern const IID IID_IRichEditOle, IID_IRichEditOleCallback;
}

typedef struct _reobject {
  DWORD cbStruct;
  LONG cp;
  CLSID clsid;
  LPOLEOBJECT poleobj;
  LPSTORAGE pstg;
  LPOLECLIENTSITE polesite;
  SIZEL sizel;
  DWORD dvaspect;
  DWORD dwFlags;
  DWORD dwUser;
} REOBJECT;

#define REO_CP_SELECTION ((ULONG)-1L)
#define REO_IOB_SELECTION ((ULONG)-1L)
#define REO_IOB_USE_CP ((ULONG)-2L)

#define REO_NULL 0x00000000L
#define REO_READWRITEMASK 0x0000003FL
#define REO_DONTNEEDPALETTE 0x00000020L
#define REO_BLANK 0x00000010L
#define REO_DYNAMICSIZE 0x00000008L
#define REO_INVERTEDSELECT 0x00000004L
#define REO_BELOWBASELINE 0x00000002L
#define REO_RESIZABLE 0x00000001L
#define REO_LINK 0x80000000L
#define REO_STATIC 0x40000000L
#define REO_SELECTED 0x08000000L
#define REO_OPEN 0x04000000L
#define REO_INPLACEACTIVE 0x02000000L
#define REO_HILITED 0x01000000L
#define REO_LINKAVAILABLE 0x00800000L
#define REO_GETMETAFILE 0x00400000L

#define REO_GETOBJ_NO_INTERFACES 0x00000000L
#define REO_GETOBJ_POLEOBJ 0x00000001L
#define REO_GETOBJ_PSTG 0x00000002L
#define REO_GETOBJ_POLESITE 0x00000004L
#define REO_GETOBJ_ALL_INTERFACES 0x00000007L

#define RECO_PASTE 0x00000000L
#define RECO_DROP 0x00000001L
#define RECO_COPY 0x00000002L
#define RECO_CUT 0x00000003L
#define RECO_DRAG 0x00000004L

interface IRichEditOle : public IUnknown {
  STDMETHOD(GetClientSite)(LPOLECLIENTSITE* lplpolesite) = 0;
  STDMETHOD_(LONG, GetObjectCount)(void) = 0;
  STDMETHOD_(LONG, GetLinkCount)(void) = 0;
  STDMETHOD(GetObject)(LONG iob, REOBJECT* lpreobject, DWORD dwFlags) = 0;
  STDMETHOD(InsertObject)(REOBJECT* lpreobject) = 0;
  STDMETHOD(ConvertObject)(LONG iob, REFCLSID rclsidNew, LPCSTR lpstrUserTypeNew) = 0;
  STDMETHOD(ActivateAs)(REFCLSID rclsid, REFCLSID rclsidAs) = 0;
  STDMETHOD(SetHostNames)(LPCSTR lpstrContainerApp, LPCSTR lpstrContainerObj) = 0;
  STDMETHOD(SetLinkAvailable)(LONG iob, BOOL fLinkAvailable) = 0;
  STDMETHOD(SetDvaspect)(LONG iob, DWORD dvaspect) = 0;
  STDMETHOD(HandsOffStorage)(LONG iob) = 0;
  STDMETHOD(SaveCompleted)(LONG iob, LPSTORAGE lpstg) = 0;
  STDMETHOD(InPlaceDeactivate)(void) = 0;
  STDMETHOD(ContextSensitiveHelp)(BOOL fEnterMode) = 0;
  STDMETHOD(GetClipboardData)(CHARRANGE* lpchrg, DWORD reco, LPDATAOBJECT* lplpdataobj) = 0;
  STDMETHOD(ImportDataObject)(LPDATAOBJECT lpdataobj, CLIPFORMAT cf, HGLOBAL hMetaPict) = 0;
};
typedef IRichEditOle* LPRICHEDITOLE;

interface IRichEditOleCallback : public IUnknown {
  STDMETHOD(GetNewStorage)(LPSTORAGE* lplpstg) = 0;
  STDMETHOD(GetInPlaceContext)(LPOLEINPLACEFRAME* lplpFrame, LPOLEINPLACEUIWINDOW* lplpDoc,
                               LPOLEINPLACEFRAMEINFO lpFrameInfo) = 0;
  STDMETHOD(ShowContainerUI)(BOOL fShow) = 0;
  STDMETHOD(QueryInsertObject)(LPCLSID lpclsid, LPSTORAGE lpstg, LONG cp) = 0;
  STDMETHOD(DeleteObject)(LPOLEOBJECT lpoleobj) = 0;
  STDMETHOD(QueryAcceptData)(LPDATAOBJECT lpdataobj, CLIPFORMAT* lpcfFormat, DWORD reco, BOOL fReally,
                             HGLOBAL hMetaPict) = 0;
  STDMETHOD(ContextSensitiveHelp)(BOOL fEnterMode) = 0;
  STDMETHOD(GetClipboardData)(CHARRANGE* lpchrg, DWORD reco, LPDATAOBJECT* lplpdataobj) = 0;
  STDMETHOD(GetDragDropEffect)(BOOL fDrag, DWORD grfKeyState, LPDWORD pdwEffect) = 0;
  STDMETHOD(GetContextMenu)(WORD seltype, LPOLEOBJECT lpoleobj, CHARRANGE* lpchrg, HMENU* lphmenu) = 0;
};
typedef IRichEditOleCallback* LPRICHEDITOLECALLBACK;

WINPORT_UUIDOF(IRichEditOle)
WINPORT_UUIDOF(IRichEditOleCallback)

#endif
