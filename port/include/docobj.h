// docobj.h - OLE document objects: documents, document sites and views,
// command targets, and printing.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#ifndef _WINPORT_DOCOBJ_H
#define _WINPORT_DOCOBJ_H

#include <ocidl.h>
#include <oleidl.h>
#include <objidl.h>

#ifdef __cplusplus
interface IOleDocument;
#else
typedef struct IOleDocument IOleDocument;
#endif
#ifdef __cplusplus
interface IOleDocumentSite;
#else
typedef struct IOleDocumentSite IOleDocumentSite;
#endif
#ifdef __cplusplus
interface IOleDocumentView;
#else
typedef struct IOleDocumentView IOleDocumentView;
#endif
#ifdef __cplusplus
interface IEnumOleDocumentViews;
#else
typedef struct IEnumOleDocumentViews IEnumOleDocumentViews;
#endif
#ifdef __cplusplus
interface IContinueCallback;
#else
typedef struct IContinueCallback IContinueCallback;
#endif
#ifdef __cplusplus
interface IPrint;
#else
typedef struct IPrint IPrint;
#endif
#ifdef __cplusplus
interface IOleCommandTarget;
#else
typedef struct IOleCommandTarget IOleCommandTarget;
#endif

#ifdef __cplusplus
extern "C" {
#endif
extern const IID IID_IOleDocument, IID_IOleDocumentSite, IID_IOleDocumentView, IID_IEnumOleDocumentViews,
    IID_IContinueCallback, IID_IPrint, IID_IOleCommandTarget;
extern const GUID CGID_MSHTML, CGID_Explorer, CGID_ShellDocView;
#ifdef __cplusplus
}  // extern "C"
#endif

typedef enum {
  DOCMISC_CANCREATEMULTIPLEVIEWS = 1,
  DOCMISC_SUPPORTCOMPLEXRECTANGLES = 2,
  DOCMISC_CANTOPENEDIT = 4,
  DOCMISC_NOFILESUPPORT = 8
} DOCMISC;

#ifdef __cplusplus
interface IOleDocument : public IUnknown {
  STDMETHOD(CreateView)(IOleInPlaceSite* pIPSite, IStream* pstm, DWORD dwReserved, IOleDocumentView** ppView) = 0;
  STDMETHOD(GetDocMiscStatus)(DWORD* pdwStatus) = 0;
  STDMETHOD(EnumViews)(IEnumOleDocumentViews** ppEnum, IOleDocumentView** ppView) = 0;
};
#else
typedef struct IOleDocument IOleDocument;
#endif
typedef IOleDocument* LPOLEDOCUMENT;

#ifdef __cplusplus
interface IOleDocumentSite : public IUnknown {
  STDMETHOD(ActivateMe)(IOleDocumentView* pViewToActivate) = 0;
};
#else
typedef struct IOleDocumentSite IOleDocumentSite;
#endif
typedef IOleDocumentSite* LPOLEDOCUMENTSITE;

#ifdef __cplusplus
interface IOleDocumentView : public IUnknown {
  STDMETHOD(SetInPlaceSite)(IOleInPlaceSite* pIPSite) = 0;
  STDMETHOD(GetInPlaceSite)(IOleInPlaceSite** ppIPSite) = 0;
  STDMETHOD(GetDocument)(IUnknown** ppunk) = 0;
  STDMETHOD(SetRect)(LPRECT prcView) = 0;
  STDMETHOD(GetRect)(LPRECT prcView) = 0;
  STDMETHOD(SetRectComplex)(LPRECT prcView, LPRECT prcHScroll, LPRECT prcVScroll, LPRECT prcSizeBox) = 0;
  STDMETHOD(Show)(BOOL fShow) = 0;
  STDMETHOD(UIActivate)(BOOL fUIActivate) = 0;
  STDMETHOD(Open)(void) = 0;
  STDMETHOD(CloseView)(DWORD dwReserved) = 0;
  STDMETHOD(SaveViewState)(LPSTREAM pstm) = 0;
  STDMETHOD(ApplyViewState)(LPSTREAM pstm) = 0;
  STDMETHOD(Clone)(IOleInPlaceSite* pIPSiteNew, IOleDocumentView** ppViewNew) = 0;
};
#else
typedef struct IOleDocumentView IOleDocumentView;
#endif
typedef IOleDocumentView* LPOLEDOCUMENTVIEW;

#ifdef __cplusplus
interface IEnumOleDocumentViews : public IUnknown {
  STDMETHOD(Next)(ULONG cViews, IOleDocumentView** rgpView, ULONG* pcFetched) = 0;
  STDMETHOD(Skip)(ULONG cViews) = 0;
  STDMETHOD(Reset)(void) = 0;
  STDMETHOD(Clone)(IEnumOleDocumentViews** ppEnum) = 0;
};
#else
typedef struct IEnumOleDocumentViews IEnumOleDocumentViews;
#endif
typedef IEnumOleDocumentViews* LPENUMOLEDOCUMENTVIEWS;

#ifdef __cplusplus
interface IContinueCallback : public IUnknown {
  STDMETHOD(FContinue)(void) = 0;
  STDMETHOD(FContinuePrinting)(LONG nCntPrinted, LONG nCurPage, wchar_t* pwszPrintStatus) = 0;
};
#else
typedef struct IContinueCallback IContinueCallback;
#endif
typedef IContinueCallback* LPCONTINUECALLBACK;

// Printing
typedef enum { PRINTFLAG_MAYBOTHERUSER = 1, PRINTFLAG_PROMPTUSER = 2 } PRINTFLAG;
// MFC defines some of these itself, with the same values.
#ifndef PRINTFLAG_USERSUPPLIEDPRINTER
#define PRINTFLAG_USERSUPPLIEDPRINTER 4
#endif
#define PRINTFLAG_RECOMPOSETODEVICE 8
#define PRINTFLAG_DONTACTUALLYPRINT 16
#define PRINTFLAG_FORCEPROPERTIES 32
#define PRINTFLAG_PRINTTOFILE 64

typedef struct tagPAGERANGE {
  LONG nFromPage;
  LONG nToPage;
} PAGERANGE;

typedef struct tagPAGESET {
  ULONG cbStruct;
  BOOL fOddPages;
  BOOL fEvenPages;
  ULONG cPageRange;
  PAGERANGE rgPages[1];
} PAGESET;

#define PAGESET_TOLASTPAGE ((WORD)(-1L))

#ifdef __cplusplus
interface IPrint : public IUnknown {
  STDMETHOD(SetInitialPageNum)(LONG nFirstPage) = 0;
  STDMETHOD(GetPageInfo)(LONG* pnFirstPage, LONG* pcPages) = 0;
  STDMETHOD(Print)(DWORD grfFlags, DVTARGETDEVICE** pptd, PAGESET** ppPageSet, STGMEDIUM* pstgmOptions,
                   IContinueCallback* pcallback, LONG nFirstPage, LONG* pcPagesPrinted, LONG* pnLastPage) = 0;
};
#else
typedef struct IPrint IPrint;
#endif
typedef IPrint* LPPRINT;

// Command targets
typedef enum {
  OLECMDF_SUPPORTED = 0x1,
  OLECMDF_ENABLED = 0x2,
  OLECMDF_LATCHED = 0x4,
  OLECMDF_NINCHED = 0x8,
  OLECMDF_INVISIBLE = 0x10,
  OLECMDF_DEFHIDEONCTXTMENU = 0x20
} OLECMDF;

typedef struct _tagOLECMD {
  ULONG cmdID;
  DWORD cmdf;
} OLECMD;

typedef struct _tagOLECMDTEXT {
  DWORD cmdtextf;
  ULONG cwActual;
  ULONG cwBuf;
  wchar_t rgwz[1];
} OLECMDTEXT;

typedef enum { OLECMDTEXTF_NONE = 0, OLECMDTEXTF_NAME = 1, OLECMDTEXTF_STATUS = 2 } OLECMDTEXTF;

typedef enum {
  OLECMDEXECOPT_DODEFAULT = 0,
  OLECMDEXECOPT_PROMPTUSER = 1,
  OLECMDEXECOPT_DONTPROMPTUSER = 2,
  OLECMDEXECOPT_SHOWHELP = 3
} OLECMDEXECOPT;

typedef enum {
  OLECMDID_OPEN = 1,
  OLECMDID_NEW = 2,
  OLECMDID_SAVE = 3,
  OLECMDID_SAVEAS = 4,
  OLECMDID_SAVECOPYAS = 5,
  OLECMDID_PRINT = 6,
  OLECMDID_PRINTPREVIEW = 7,
  OLECMDID_PAGESETUP = 8,
  OLECMDID_SPELL = 9,
  OLECMDID_PROPERTIES = 10,
  OLECMDID_CUT = 11,
  OLECMDID_COPY = 12,
  OLECMDID_PASTE = 13,
  OLECMDID_PASTESPECIAL = 14,
  OLECMDID_UNDO = 15,
  OLECMDID_REDO = 16,
  OLECMDID_SELECTALL = 17,
  OLECMDID_CLEARSELECTION = 18,
  OLECMDID_ZOOM = 19,
  OLECMDID_GETZOOMRANGE = 20,
  OLECMDID_UPDATECOMMANDS = 21,
  OLECMDID_REFRESH = 22,
  OLECMDID_STOP = 23,
  OLECMDID_HIDETOOLBARS = 24,
  OLECMDID_SETPROGRESSMAX = 25,
  OLECMDID_SETPROGRESSPOS = 26,
  OLECMDID_SETPROGRESSTEXT = 27,
  OLECMDID_SETTITLE = 28,
  OLECMDID_SETDOWNLOADSTATE = 29,
  OLECMDID_STOPDOWNLOAD = 30,
  OLECMDID_ONTOOLBARACTIVATED = 31,
  OLECMDID_FIND = 32,
  OLECMDID_DELETE = 33,
  OLECMDID_HTTPEQUIV = 34,
  OLECMDID_HTTPEQUIV_DONE = 35,
  OLECMDID_ENABLE_INTERACTION = 36,
  OLECMDID_ONUNLOAD = 37
} OLECMDID;

// The OLECMDERR_E_ codes are in winerror.h.
#define MSOCMDERR_E_FIRST OLECMDERR_E_FIRST
#define MSOCMDERR_E_NOTSUPPORTED OLECMDERR_E_NOTSUPPORTED
#define MSOCMDERR_E_DISABLED OLECMDERR_E_DISABLED
#define MSOCMDERR_E_NOHELP OLECMDERR_E_NOHELP
#define MSOCMDERR_E_CANCELED OLECMDERR_E_CANCELED
#define MSOCMDERR_E_UNKNOWNGROUP OLECMDERR_E_UNKNOWNGROUP

#ifdef __cplusplus
interface IOleCommandTarget : public IUnknown {
  STDMETHOD(QueryStatus)(const GUID* pguidCmdGroup, ULONG cCmds, OLECMD prgCmds[], OLECMDTEXT* pCmdText) = 0;
  STDMETHOD(Exec)(const GUID* pguidCmdGroup, DWORD nCmdID, DWORD nCmdexecopt, VARIANT* pvaIn, VARIANT* pvaOut) = 0;
};
#else
typedef struct IOleCommandTarget IOleCommandTarget;
#endif
typedef IOleCommandTarget* LPOLECOMMANDTARGET;

WINPORT_UUIDOF(IOleDocument)
WINPORT_UUIDOF(IOleDocumentSite)
WINPORT_UUIDOF(IOleDocumentView)
WINPORT_UUIDOF(IEnumOleDocumentViews)
WINPORT_UUIDOF(IContinueCallback)
WINPORT_UUIDOF(IPrint)
WINPORT_UUIDOF(IOleCommandTarget)

#endif
