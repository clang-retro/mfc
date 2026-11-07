// afxdocob.h - Active Documents (DocObjects): a server document that a
// container shows as a whole document in its own frame, through
// IOleDocument and IOleDocumentView (CDocObjectServer and
// CDocObjectServerItem), and the container's side (COleDocObjectItem).
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
#ifndef __AFXDOCOB_H__
#define __AFXDOCOB_H__

#include "afxole.h"
#include <docobj.h>

// ---------------------------------------------------------------------
// OLE command maps: IOleCommandTarget's commands (OLECMDID_*) as the
// program's command IDs.

struct OLECMDMAP_ENTRY {
  const GUID* pguid;  // NULL: the standard group
  ULONG cmdID;
  UINT nID;
};

struct OLECMDMAP {
  const OLECMDMAP* (PASCAL* pfnGetBaseMap)();
  const OLECMDMAP_ENTRY* lpEntries;
};

#define DECLARE_OLECMD_MAP()                            \
private:                                                \
  static const OLECMDMAP_ENTRY _commandEntries[];       \
protected:                                              \
  static const OLECMDMAP commandMap;                    \
  static const OLECMDMAP* PASCAL _GetBaseCommandMap();  \
  virtual const OLECMDMAP* GetCommandMap() const;

#define BEGIN_OLECMD_MAP(theClass, baseClass)                                           \
  const OLECMDMAP* PASCAL theClass::_GetBaseCommandMap() { return &baseClass::commandMap; } \
  const OLECMDMAP* theClass::GetCommandMap() const { return &theClass::commandMap; }   \
  const OLECMDMAP theClass::commandMap = {&theClass::_GetBaseCommandMap,                \
                                          &theClass::_commandEntries[0]};               \
  const OLECMDMAP_ENTRY theClass::_commandEntries[] = {
#define END_OLECMD_MAP() \
  { NULL, 0, 0 }         \
  }                      \
  ;

#define ON_OLECMD(pguid, olecmdid, id) {pguid, (ULONG)(olecmdid), (UINT)(id)},
#define ON_OLECMD_CLEARSELECTION() ON_OLECMD(NULL, OLECMDID_CLEARSELECTION, ID_EDIT_CLEAR)
#define ON_OLECMD_COPY() ON_OLECMD(NULL, OLECMDID_COPY, ID_EDIT_COPY)
#define ON_OLECMD_CUT() ON_OLECMD(NULL, OLECMDID_CUT, ID_EDIT_CUT)
#define ON_OLECMD_NEW() ON_OLECMD(NULL, OLECMDID_NEW, ID_FILE_NEW)
#define ON_OLECMD_OPEN() ON_OLECMD(NULL, OLECMDID_OPEN, ID_FILE_OPEN)
#define ON_OLECMD_PAGESETUP() ON_OLECMD(NULL, OLECMDID_PAGESETUP, ID_FILE_PAGE_SETUP)
#define ON_OLECMD_PASTE() ON_OLECMD(NULL, OLECMDID_PASTE, ID_EDIT_PASTE)
#define ON_OLECMD_PASTESPECIAL() ON_OLECMD(NULL, OLECMDID_PASTESPECIAL, ID_EDIT_PASTE_SPECIAL)
#define ON_OLECMD_PRINT() ON_OLECMD(NULL, OLECMDID_PRINT, ID_FILE_PRINT)
#define ON_OLECMD_PRINTPREVIEW() ON_OLECMD(NULL, OLECMDID_PRINTPREVIEW, ID_FILE_PRINT_PREVIEW)
#define ON_OLECMD_REDO() ON_OLECMD(NULL, OLECMDID_REDO, ID_EDIT_REDO)
#define ON_OLECMD_SAVE() ON_OLECMD(NULL, OLECMDID_SAVE, ID_FILE_SAVE)
#define ON_OLECMD_SAVE_AS() ON_OLECMD(NULL, OLECMDID_SAVEAS, ID_FILE_SAVE_AS)
#define ON_OLECMD_SAVE_COPY_AS() ON_OLECMD(NULL, OLECMDID_SAVECOPYAS, ID_FILE_SAVE_COPY_AS)
#define ON_OLECMD_SELECTALL() ON_OLECMD(NULL, OLECMDID_SELECTALL, ID_EDIT_SELECT_ALL)
#define ON_OLECMD_UNDO() ON_OLECMD(NULL, OLECMDID_UNDO, ID_EDIT_UNDO)

// ---------------------------------------------------------------------
// CDocObjectServer: the IOleDocument, IOleDocumentView, IOleCommandTarget
// and IPrint of a COleServerDoc whose container is a document site. The
// document makes it in GetDocObjectServer; its interfaces are the
// document's. It has one view: the document's own, active in place in
// the rectangle the container gives.

class CDocObjectServer : public CCmdTarget {
  DECLARE_DYNAMIC(CDocObjectServer)
public:
  CDocObjectServer(COleServerDoc* pOwner, LPOLEDOCUMENTSITE pDocSite = NULL);
  virtual ~CDocObjectServer();

  // Asks the document site to show the document (IOleDocumentSite::ActivateMe).
  void ActivateDocObject();

  // The view's state, as the container keeps it between showings.
  virtual void OnSaveViewState(CArchive& ar);
  virtual void OnApplyViewState(CArchive& ar);
  // The view has become active in place in the container.
  virtual void OnActivateView();

  COleServerDoc* m_pOwner;
  LPOLEDOCUMENTSITE m_pDocSite;
  LPOLEINPLACESITE m_pViewSite;
  CRect m_rectView;
  LONG m_nFirstPage;

  void SetDocSite(LPOLEDOCUMENTSITE pDocSite);
  // The window the view's commands go to.
  CWnd* GetCommandWnd() const;

public:
  BEGIN_INTERFACE_PART(OleDocument, IOleDocument)
    STDMETHOD(CreateView)(LPOLEINPLACESITE pIPSite, LPSTREAM pstm, DWORD dwReserved,
                          LPOLEDOCUMENTVIEW* ppView);
    STDMETHOD(GetDocMiscStatus)(LPDWORD pdwStatus);
    STDMETHOD(EnumViews)(LPENUMOLEDOCUMENTVIEWS* ppEnum, LPOLEDOCUMENTVIEW* ppView);
  END_INTERFACE_PART(OleDocument)

  BEGIN_INTERFACE_PART(OleDocumentView, IOleDocumentView)
    STDMETHOD(SetInPlaceSite)(LPOLEINPLACESITE pIPSite);
    STDMETHOD(GetInPlaceSite)(LPOLEINPLACESITE* ppIPSite);
    STDMETHOD(GetDocument)(LPUNKNOWN* ppunk);
    STDMETHOD(SetRect)(LPRECT prcView);
    STDMETHOD(GetRect)(LPRECT prcView);
    STDMETHOD(SetRectComplex)(LPRECT prcView, LPRECT prcHScroll, LPRECT prcVScroll,
                              LPRECT prcSizeBox);
    STDMETHOD(Show)(BOOL fShow);
    STDMETHOD(UIActivate)(BOOL fUIActivate);
    STDMETHOD(Open)();
    STDMETHOD(CloseView)(DWORD dwReserved);
    STDMETHOD(SaveViewState)(LPSTREAM pstm);
    STDMETHOD(ApplyViewState)(LPSTREAM pstm);
    STDMETHOD(Clone)(LPOLEINPLACESITE pIPSiteNew, LPOLEDOCUMENTVIEW* ppViewNew);
  END_INTERFACE_PART(OleDocumentView)

  BEGIN_INTERFACE_PART(OleCommandTarget, IOleCommandTarget)
    STDMETHOD(QueryStatus)(const GUID* pguidCmdGroup, ULONG cCmds, OLECMD rgCmds[],
                           OLECMDTEXT* pcmdtext);
    STDMETHOD(Exec)(const GUID* pguidCmdGroup, DWORD nCmdID, DWORD nCmdExecOpt,
                    VARIANTARG* pvarargIn, VARIANTARG* pvarargOut);
  END_INTERFACE_PART(OleCommandTarget)

  BEGIN_INTERFACE_PART(Print, IPrint)
    STDMETHOD(SetInitialPageNum)(LONG nFirstPage);
    STDMETHOD(GetPageInfo)(LPLONG pnFirstPage, LPLONG pcPages);
    STDMETHOD(Print)(DWORD grfFlags, DVTARGETDEVICE** pptd, PAGESET** ppPageSet,
                     LPSTGMEDIUM pstgmOptions, LPCONTINUECALLBACK pCallback, LONG nFirstPage,
                     LPLONG pcPagesPrinted, LPLONG pnLastPage);
  END_INTERFACE_PART(Print)

  DECLARE_INTERFACE_MAP()
  DECLARE_OLECMD_MAP()
};

// ---------------------------------------------------------------------
// CDocObjectServerItem: the embedded item of a DocObject server's
// document: showing it asks the document site to show the document.

class CDocObjectServerItem : public COleServerItem {
  DECLARE_DYNAMIC(CDocObjectServerItem)
public:
  CDocObjectServerItem(COleServerDoc* pServerDoc, BOOL bAutoDelete);
  virtual ~CDocObjectServerItem();
  COleServerDoc* GetDocument() const { return COleServerItem::GetDocument(); }

  virtual void OnDoVerb(LONG iVerb);
  virtual void OnShow();
  virtual void OnHide();
};

// ---------------------------------------------------------------------
// COleDocObjectItem: a container's item for a DocObject: its client site
// is a document site, and the object shows as a whole document filling
// the item's view.

class COleDocObjectItem : public COleClientItem {
  DECLARE_DYNAMIC(COleDocObjectItem)
public:
  COleDocObjectItem(COleDocument* pContainerDoc = NULL);
  virtual ~COleDocObjectItem();

  LPOLEDOCUMENTVIEW GetActiveView() const { return m_pActiveView; }
  // The object's command target, through IOleCommandTarget.
  HRESULT ExecCommand(DWORD nCmdID, DWORD nCmdExecOpt = OLECMDEXECOPT_DONTPROMPTUSER,
                      const GUID* pguidCmdGroup = NULL);
  HRESULT QueryCommand(ULONG nCmdID, DWORD* pdwStatus, OLECMDTEXT* pCmdText = NULL,
                       const GUID* pguidCmdGroup = NULL);
  BOOL GetPageCount(LPLONG pnFirstPage, LPLONG pcPages);
  // Printing through the object's own Print command.
  void DoDefaultPrinting(CView* pCaller, CPrintInfo* pInfo);
  static BOOL OnPreparePrinting(CView* pCaller, CPrintInfo* pInfo, BOOL bPrintAll = TRUE);
  static void OnPrint(CView* pCaller, CPrintInfo* pInfo, BOOL bPrintAll = TRUE);

  virtual void Release(OLECLOSE dwCloseOption = OLECLOSE_NOSAVE);
  // The whole of the view's client area.
  virtual void OnGetItemPosition(CRect& rPosition);

  LPOLEDOCUMENTVIEW m_pActiveView;
  LPPRINT m_pIPrint;
  BOOL m_bInClose;

  // Shows the object's view in the item's view.
  HRESULT ActivateView(LPOLEDOCUMENTVIEW pView);
  void CloseView();

public:
  BEGIN_INTERFACE_PART(OleDocumentSite, IOleDocumentSite)
    STDMETHOD(ActivateMe)(LPOLEDOCUMENTVIEW pViewToActivate);
  END_INTERFACE_PART(OleDocumentSite)

  DECLARE_INTERFACE_MAP()
};

#endif  // __AFXDOCOB_H__
