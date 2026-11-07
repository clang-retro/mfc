// afxole.h - OLE: data transfer through the clipboard and drag and drop
// (COleDataSource, COleDataObject, COleDropSource, COleDropTarget), and
// documents that contain linked and embedded objects (COleDocument,
// COleClientItem), with COleStreamFile for streams in a storage, and
// servers (COleServerDoc, COleServerItem, COleTemplateServer).
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#ifndef __AFXOLE_H__
#define __AFXOLE_H__

#include "afxwin.h"
#include "afxext.h"
#include "afxdisp.h"
// docobj.h names these two print flags too, with the same values.
#ifdef PRINTFLAG_MAYBOTHERUSER
#undef PRINTFLAG_MAYBOTHERUSER
#undef PRINTFLAG_PROMPTUSER
#endif
#include <docobj.h>

#ifndef _AFX_NOFORCE_LIBS
__asm__(".section .drectve,\"yn\"\n"
        ".ascii \" -defaultlib:oledlg\"\n"
        ".text");
#endif

class COleDataSource;
class COleDataObject;
class COleDropSource;
class COleDropTarget;
class COleDocument;
class COleClientItem;
class COleStreamFile;
class CDocItem;
class COleLinkingDoc;
class COleServerDoc;
class COleServerItem;
class COleTemplateServer;
class CDocObjectServer;
class COleFrameHook;
class COleIPFrameWnd;
class COleCntrFrameWnd;
class COleResizeBar;

// Defaults for drag scrolling and starting a drag, in pixels and
// milliseconds; the [windows] section of win.ini can change them.
#ifndef DD_DEFSCROLLINSET
#define DD_DEFSCROLLINSET 11
#define DD_DEFSCROLLDELAY 50
#define DD_DEFSCROLLINTERVAL 50
#define DD_DEFDRAGDELAY 200
#define DD_DEFDRAGMINDIST 2
#endif

// ---------------------------------------------------------------------
// COleDataObject: the receiving side, over an IDataObject from the
// clipboard or a drop.

class COleDataObject {
public:
  COleDataObject();
  ~COleDataObject();

  void Attach(LPDATAOBJECT lpDataObject, BOOL bAutoRelease = TRUE);
  LPDATAOBJECT Detach();
  void Release();
  BOOL AttachClipboard();

  void BeginEnumFormats();
  BOOL GetNextFormat(LPFORMATETC lpFormatEtc);

  BOOL IsDataAvailable(CLIPFORMAT cfFormat, LPFORMATETC lpFormatEtc = NULL);
  BOOL GetData(CLIPFORMAT cfFormat, LPSTGMEDIUM lpStgMedium, LPFORMATETC lpFormatEtc = NULL);
  // A new file over a copy of the data, which the caller deletes.
  CFile* GetFileData(CLIPFORMAT cfFormat, LPFORMATETC lpFormatEtc = NULL);
  // A copy of the data, which the caller frees.
  HGLOBAL GetGlobalData(CLIPFORMAT cfFormat, LPFORMATETC lpFormatEtc = NULL);

  LPDATAOBJECT m_lpDataObject;
  LPENUMFORMATETC m_lpEnumerator;
  BOOL m_bAutoRelease;
  BOOL m_bClipboard;

private:
  COleDataObject(const COleDataObject&);
  void operator=(const COleDataObject&);
};

// ---------------------------------------------------------------------
// COleDataSource: the sending side. It holds data, or renders it when it
// is asked for, and offers it on the clipboard or in a drag.

struct AFX_DATACACHE_ENTRY {
  FORMATETC m_formatEtc;
  STGMEDIUM m_stgMedium;  // TYMED_NULL for data rendered when asked for
  DATADIR m_nDataDir;     // DATADIR_GET, or DATADIR_SET for DelaySetData
};

class COleDataSource : public CCmdTarget {
  DECLARE_DYNAMIC(COleDataSource)
public:
  COleDataSource();
  virtual ~COleDataSource();

  // Takes over the medium.
  void CacheData(CLIPFORMAT cfFormat, LPSTGMEDIUM lpStgMedium, LPFORMATETC lpFormatEtc = NULL);
  void CacheGlobalData(CLIPFORMAT cfFormat, HGLOBAL hGlobal, LPFORMATETC lpFormatEtc = NULL);
  void DelayRenderData(CLIPFORMAT cfFormat, LPFORMATETC lpFormatEtc = NULL);
  void DelayRenderFileData(CLIPFORMAT cfFormat, LPFORMATETC lpFormatEtc = NULL);
  void DelaySetData(CLIPFORMAT cfFormat, LPFORMATETC lpFormatEtc = NULL);
  void Empty();

  // The clipboard takes over the object: do not delete it afterwards.
  void SetClipboard();
  static void PASCAL FlushClipboard();
  static COleDataSource* PASCAL GetClipboardOwner();

  DROPEFFECT DoDragDrop(DWORD dwEffects = DROPEFFECT_COPY | DROPEFFECT_MOVE | DROPEFFECT_LINK,
                        LPCRECT lpRectStartDrag = NULL, COleDropSource* pDropSource = NULL);

  virtual BOOL OnRenderData(LPFORMATETC lpFormatEtc, LPSTGMEDIUM lpStgMedium);
  virtual BOOL OnRenderFileData(LPFORMATETC lpFormatEtc, CFile* pFile);
  virtual BOOL OnRenderGlobalData(LPFORMATETC lpFormatEtc, HGLOBAL* phGlobal);
  virtual BOOL OnSetData(LPFORMATETC lpFormatEtc, LPSTGMEDIUM lpStgMedium, BOOL bRelease);

  AFX_DATACACHE_ENTRY* Lookup(LPFORMATETC lpFormatEtc, DATADIR nDataDir) const;

protected:
  AFX_DATACACHE_ENTRY* GetCacheEntry(LPFORMATETC lpFormatEtc, DATADIR nDataDir);
  CPtrArray m_cache;  // AFX_DATACACHE_ENTRY*

public:
  BEGIN_INTERFACE_PART(DataObject, IDataObject)
    STDMETHOD(GetData)(LPFORMATETC pformatetcIn, LPSTGMEDIUM pmedium);
    STDMETHOD(GetDataHere)(LPFORMATETC pformatetc, LPSTGMEDIUM pmedium);
    STDMETHOD(QueryGetData)(LPFORMATETC pformatetc);
    STDMETHOD(GetCanonicalFormatEtc)(LPFORMATETC pformatetcIn, LPFORMATETC pformatetcOut);
    STDMETHOD(SetData)(LPFORMATETC pformatetc, LPSTGMEDIUM pmedium, BOOL fRelease);
    STDMETHOD(EnumFormatEtc)(DWORD dwDirection, LPENUMFORMATETC* ppenumFormatEtc);
    STDMETHOD(DAdvise)(LPFORMATETC pformatetc, DWORD advf, LPADVISESINK pAdvSink,
                       DWORD* pdwConnection);
    STDMETHOD(DUnadvise)(DWORD dwConnection);
    STDMETHOD(EnumDAdvise)(LPENUMSTATDATA* ppenumAdvise);
  END_INTERFACE_PART(DataObject)

  DECLARE_INTERFACE_MAP()
};

// ---------------------------------------------------------------------
// COleDropSource: decides when a drag starts, continues and ends.

class COleDropSource : public CCmdTarget {
  DECLARE_DYNAMIC(COleDropSource)
public:
  COleDropSource();

  // Waits for the mouse to leave m_rectStartDrag; FALSE if the button
  // went up first.
  virtual BOOL OnBeginDrag(CWnd* pWnd);
  virtual SCODE QueryContinueDrag(BOOL bEscapePressed, DWORD dwKeyState);
  virtual SCODE GiveFeedback(DROPEFFECT dropEffect);

  BOOL m_bDragStarted;
  DWORD m_dwButtonCancel;
  DWORD m_dwButtonDrop;
  CRect m_rectStartDrag;

  static UINT nDragMinDist;
  static UINT nDragDelay;

  BEGIN_INTERFACE_PART(DropSource, IDropSource)
    STDMETHOD(QueryContinueDrag)(BOOL fEscapePressed, DWORD grfKeyState);
    STDMETHOD(GiveFeedback)(DWORD dwEffect);
  END_INTERFACE_PART(DropSource)

  DECLARE_INTERFACE_MAP()
};

// ---------------------------------------------------------------------
// COleDropTarget: receives drops on a window. For a CView it calls the
// view's OnDragEnter, OnDragOver, OnDrop and the rest.

class COleDropTarget : public CCmdTarget {
  DECLARE_DYNAMIC(COleDropTarget)
public:
  COleDropTarget();
  virtual ~COleDropTarget();

  BOOL Register(CWnd* pWnd);
  virtual void Revoke();

  virtual DROPEFFECT OnDragEnter(CWnd* pWnd, COleDataObject* pDataObject, DWORD dwKeyState,
                                 CPoint point);
  virtual DROPEFFECT OnDragOver(CWnd* pWnd, COleDataObject* pDataObject, DWORD dwKeyState,
                                CPoint point);
  virtual BOOL OnDrop(CWnd* pWnd, COleDataObject* pDataObject, DROPEFFECT dropEffect,
                      CPoint point);
  virtual DROPEFFECT OnDropEx(CWnd* pWnd, COleDataObject* pDataObject, DROPEFFECT dropDefault,
                              DROPEFFECT dropList, CPoint point);
  virtual void OnDragLeave(CWnd* pWnd);
  virtual DROPEFFECT OnDragScroll(CWnd* pWnd, DWORD dwKeyState, CPoint point);

  HWND m_hWnd;
  LPDATAOBJECT m_lpDataObject;  // during a drag
  DROPEFFECT m_dropEffectCurrent;

  static int nScrollInset;
  static UINT nScrollDelay;
  static UINT nScrollInterval;

  BEGIN_INTERFACE_PART(DropTarget, IDropTarget)
    STDMETHOD(DragEnter)(LPDATAOBJECT pDataObj, DWORD grfKeyState, POINTL pt, LPDWORD pdwEffect);
    STDMETHOD(DragOver)(DWORD grfKeyState, POINTL pt, LPDWORD pdwEffect);
    STDMETHOD(DragLeave)();
    STDMETHOD(Drop)(LPDATAOBJECT pDataObj, DWORD grfKeyState, POINTL pt, LPDWORD pdwEffect);
  END_INTERFACE_PART(DropTarget)

  DECLARE_INTERFACE_MAP()
};

// ---------------------------------------------------------------------
// COleStreamFile: a CFile over an IStream, such as a stream in a
// compound file.

class COleStreamFile : public CFile {
  DECLARE_DYNAMIC(COleStreamFile)
public:
  COleStreamFile(LPSTREAM lpStream = NULL);
  virtual ~COleStreamFile();

  void Attach(LPSTREAM lpStream);
  LPSTREAM Detach();
  IStream* GetStream() const { return m_lpStream; }

  // Streams in a storage are always opened for exclusive use.
  BOOL OpenStream(LPSTORAGE lpStorage, LPCTSTR lpszStreamName,
                  DWORD nOpenFlags = modeReadWrite | shareExclusive, CFileException* pError = NULL);
  BOOL CreateStream(LPSTORAGE lpStorage, LPCTSTR lpszStreamName,
                    DWORD nOpenFlags = modeReadWrite | shareExclusive | modeCreate,
                    CFileException* pError = NULL);
  BOOL CreateMemoryStream(CFileException* pError = NULL);

  virtual ULONGLONG GetPosition() const;
  virtual ULONGLONG Seek(LONGLONG lOff, UINT nFrom);
  virtual void SetLength(ULONGLONG dwNewLen);
  virtual ULONGLONG GetLength() const;
  virtual UINT Read(void* lpBuf, UINT nCount);
  virtual void Write(const void* lpBuf, UINT nCount);
  virtual void LockRange(ULONGLONG dwPos, ULONGLONG dwCount);
  virtual void UnlockRange(ULONGLONG dwPos, ULONGLONG dwCount);
  virtual void Abort();
  virtual void Flush();
  virtual void Close();
  virtual CFile* Duplicate() const;
  virtual BOOL GetStatus(CFileStatus& rStatus) const;

  LPSTREAM m_lpStream;
};

// ---------------------------------------------------------------------
// CDocItem: an item kept in a COleDocument.

class CDocItem : public CCmdTarget {
  DECLARE_SERIAL(CDocItem)
protected:
  CDocItem();

public:
  virtual ~CDocItem();
  COleDocument* GetDocument() const { return m_pDocument; }
  // TRUE for an item that has nothing to save.
  virtual BOOL IsBlank() const;

  COleDocument* m_pDocument;
};

// ---------------------------------------------------------------------
// COleDocument: a document holding CDocItems. With EnableCompoundFile it
// saves to a compound file, each object in its own storage beside the
// document's "Contents" stream; otherwise each object's storage is
// written into the document's archive.

class COleDocument : public CDocument {
  DECLARE_DYNAMIC(COleDocument)
public:
  COleDocument();
  virtual ~COleDocument();

  virtual void AddItem(CDocItem* pItem);
  virtual void RemoveItem(CDocItem* pItem);
  virtual POSITION GetStartPosition() const { return m_docItemList.GetHeadPosition(); }
  virtual CDocItem* GetNextItem(POSITION& pos) const { return (CDocItem*)m_docItemList.GetNext(pos); }
  COleClientItem* GetNextClientItem(POSITION& pos) const;
  virtual COleClientItem* GetInPlaceActiveItem(CWnd* pWnd);
  // The one item selected in pView, or NULL when none or several are.
  virtual COleClientItem* GetPrimarySelectedItem(CView* pView);

  // Call in the constructor.
  void EnableCompoundFile(BOOL bEnable = TRUE);
  BOOL HasBlankItems() const;
  virtual void UpdateModifiedFlag();
  virtual void OnShowViews(BOOL bVisible);
  BOOL ApplyPrintDevice(const DVTARGETDEVICE* ptd);
  BOOL ApplyPrintDevice(const PRINTDLG* ppd);

  // Saves and loads the items; derived classes call it from Serialize.
  virtual void Serialize(CArchive& ar);
  virtual void DeleteContents();
  virtual BOOL OnNewDocument();
  virtual BOOL OnOpenDocument(LPCTSTR lpszPathName);
  virtual BOOL OnSaveDocument(LPCTSTR lpszPathName);
  virtual void OnCloseDocument();
  virtual BOOL SaveModified();
  virtual void PreCloseFrame(CFrameWnd* pFrame);
  virtual BOOL OnCmdMsg(UINT nID, int nCode, void* pExtra, AFX_CMDHANDLERINFO* pHandlerInfo);

  virtual void LoadFromStorage();
  virtual void SaveToStorage(CObject* pObject = NULL);
  DWORD GetNewItemNumber() { return m_dwNextItemNumber++; }
  // The document's moniker, which links to it and its items start with;
  // NULL here, as only a COleLinkingDoc can be linked to.
  virtual LPMONIKER GetMoniker(OLEGETMONIKER nAssign);

  LPSTORAGE m_lpRootStg;  // the open compound file, or a temporary one
  BOOL m_bCompoundFile;
  BOOL m_bSameAsLoad;  // while saving: into the storage it was loaded from
  BOOL m_bRemember;    // while saving: the items move to the new storage
  BOOL m_bRootIsTemp;
  DWORD m_dwNextItemNumber;
  DVTARGETDEVICE* m_ptd;
  CPtrList m_docItemList;

  afx_msg void OnUpdatePasteMenu(CCmdUI* pCmdUI);
  afx_msg void OnUpdatePasteLinkMenu(CCmdUI* pCmdUI);
  afx_msg void OnUpdateObjectVerbMenu(CCmdUI* pCmdUI);
  afx_msg void OnUpdateEditLinksMenu(CCmdUI* pCmdUI);
  afx_msg void OnUpdateEditChangeIcon(CCmdUI* pCmdUI);
  // These show the OLE dialogs (afxodlgs.h) for the selected item.
  afx_msg void OnEditLinks();
  afx_msg void OnEditConvert();
  afx_msg void OnEditChangeIcon();

protected:
  void ReleaseRoot();
  DECLARE_MESSAGE_MAP()
};

// ---------------------------------------------------------------------
// COleClientItem: a linked, embedded or static object in a COleDocument.

enum OLE_OBJTYPE { OT_UNKNOWN = 0, OT_LINK = 1, OT_EMBEDDED = 2, OT_STATIC = 3 };

enum OLE_NOTIFICATION {
  OLE_CHANGED,         // dwParam: the aspect that changed
  OLE_SAVED,
  OLE_CLOSED,
  OLE_RENAMED,
  OLE_CHANGED_STATE,   // dwParam: the new item state
  OLE_CHANGED_ASPECT,  // dwParam: the new aspect
};

class COleClientItem : public CDocItem {
  DECLARE_DYNAMIC(COleClientItem)
public:
  COleClientItem(COleDocument* pContainerDoc = NULL);
  virtual ~COleClientItem();

  enum ItemState { emptyState, loadedState, openState, activeState, activeUIState };

  // Creation; each returns FALSE and sets GetLastStatus on failure.
  static BOOL PASCAL CanPaste();
  static BOOL PASCAL CanPasteLink();
  static BOOL PASCAL CanCreateFromData(const COleDataObject* pDataObject);
  static BOOL PASCAL CanCreateLinkFromData(const COleDataObject* pDataObject);
  BOOL CreateFromClipboard(OLERENDER render = OLERENDER_DRAW, CLIPFORMAT cfFormat = 0,
                           LPFORMATETC lpFormatEtc = NULL);
  BOOL CreateLinkFromClipboard(OLERENDER render = OLERENDER_DRAW, CLIPFORMAT cfFormat = 0,
                               LPFORMATETC lpFormatEtc = NULL);
  BOOL CreateStaticFromClipboard(OLERENDER render = OLERENDER_DRAW, CLIPFORMAT cfFormat = 0,
                                 LPFORMATETC lpFormatEtc = NULL);
  BOOL CreateFromData(COleDataObject* pDataObject, OLERENDER render = OLERENDER_DRAW,
                      CLIPFORMAT cfFormat = 0, LPFORMATETC lpFormatEtc = NULL);
  BOOL CreateLinkFromData(COleDataObject* pDataObject, OLERENDER render = OLERENDER_DRAW,
                          CLIPFORMAT cfFormat = 0, LPFORMATETC lpFormatEtc = NULL);
  BOOL CreateStaticFromData(COleDataObject* pDataObject, OLERENDER render = OLERENDER_DRAW,
                            CLIPFORMAT cfFormat = 0, LPFORMATETC lpFormatEtc = NULL);
  BOOL CreateFromFile(LPCTSTR lpszFileName, REFCLSID clsid = CLSID_NULL,
                      OLERENDER render = OLERENDER_DRAW, CLIPFORMAT cfFormat = 0,
                      LPFORMATETC lpFormatEtc = NULL);
  BOOL CreateLinkFromFile(LPCTSTR lpszFileName, OLERENDER render = OLERENDER_DRAW,
                          CLIPFORMAT cfFormat = 0, LPFORMATETC lpFormatEtc = NULL);
  BOOL CreateNewItem(REFCLSID clsid, OLERENDER render = OLERENDER_DRAW, CLIPFORMAT cfFormat = 0,
                     LPFORMATETC lpFormatEtc = NULL);
  BOOL CreateCloneFrom(const COleClientItem* pSrcItem);

  // Status
  SCODE GetLastStatus() const { return m_scLast; }
  OLE_OBJTYPE GetType() const { return m_nItemType; }
  void GetClassID(CLSID* pClassID) const;
  void GetUserType(USERCLASSTYPE nUserClassType, CString& rString);
  // In HIMETRIC; nDrawAspect -1 means the item's own aspect.
  BOOL GetExtent(LPSIZE lpSize, DVASPECT nDrawAspect = (DVASPECT)-1);
  BOOL GetCachedExtent(LPSIZE lpSize, DVASPECT nDrawAspect = (DVASPECT)-1);
  void SetExtent(const CSize& size, DVASPECT nDrawAspect = DVASPECT_CONTENT);
  DVASPECT GetDrawAspect() const { return m_nDrawAspect; }
  virtual void SetDrawAspect(DVASPECT nDrawAspect);
  HGLOBAL GetIconicMetafile();
  BOOL SetIconicMetafile(HGLOBAL hMetaPict);
  UINT GetItemState() const { return m_nItemState; }
  BOOL IsModified() const;
  BOOL IsRunning() const;
  BOOL IsOpen() const { return m_nItemState == openState; }
  BOOL IsInPlaceActive() const {
    return m_nItemState == activeState || m_nItemState == activeUIState;
  }
  CView* GetActiveView() const { return m_pView; }
  CWnd* GetInPlaceWindow();
  COleDocument* GetDocument() const { return m_pDocument; }
  OLEUPDATE GetLinkUpdateOptions();
  void SetLinkUpdateOptions(OLEUPDATE dwUpdateOpt);
  BOOL IsLinkUpToDate() const;
  virtual BOOL IsBlank() const { return FALSE; }

  // Operations
  virtual void Release(OLECLOSE dwCloseOption = OLECLOSE_NOSAVE);
  void Close(OLECLOSE dwCloseOption = OLECLOSE_SAVEIFDIRTY);
  void Delete(BOOL bAutoDelete = TRUE);
  void Run();
  BOOL Draw(CDC* pDC, LPCRECT lpBounds, DVASPECT nDrawAspect = (DVASPECT)-1);
  // Reports failures in a message box and returns FALSE.
  virtual BOOL DoVerb(LONG nVerb, CView* pView, LPMSG lpMsg = NULL);
  // Throws a COleException on failure.
  void Activate(LONG nVerb, CView* pView, LPMSG lpMsg = NULL);
  void Deactivate();
  void DeactivateUI();
  BOOL SetItemRects(LPCRECT lpPosRect = NULL, LPCRECT lpClipRect = NULL);
  BOOL UpdateLink();
  BOOL Reload();
  BOOL ConvertTo(REFCLSID clsidNew);
  BOOL ActivateAs(LPCTSTR lpszUserType, REFCLSID clsidOld, REFCLSID clsidNew);
  BOOL SetPrintDevice(const DVTARGETDEVICE* ptd);
  BOOL SetPrintDevice(const PRINTDLG* ppd);
  void SetHostNames(LPCTSTR lpszHost, LPCTSTR lpszHostObj);
  void CopyToClipboard(BOOL bIncludeLink = FALSE);
  // The caller deletes the item when the result is DROPEFFECT_MOVE.
  DROPEFFECT DoDragDrop(LPCRECT lpItemRect, CPoint ptOffset, BOOL bIncludeLink = FALSE,
                        DWORD dwEffects = DROPEFFECT_COPY | DROPEFFECT_MOVE,
                        LPCRECT lpRectStartDrag = NULL);
  void GetClipboardData(COleDataSource* pDataSource, BOOL bIncludeLink = FALSE,
                        LPPOINT lpOffset = NULL, LPSIZE lpSize = NULL);
  virtual void ReportError(SCODE sc) const;

  // Overridables
  virtual void OnChange(OLE_NOTIFICATION nCode, DWORD dwParam);
  virtual void OnGetItemPosition(CRect& rPosition);
  virtual BOOL OnChangeItemPosition(const CRect& rectPos);
  virtual void OnGetClipRect(CRect& rClipRect);
  virtual void OnShowItem();
  virtual BOOL CanActivate();
  virtual void OnActivate();
  virtual void OnActivateUI();
  virtual void OnDeactivate();
  virtual void OnDeactivateUI(BOOL bUndoable);
  virtual BOOL OnScrollBy(CSize sizeExtent);
  virtual void OnDiscardUndoState();
  virtual void OnDeactivateAndUndo();
  // The frames that hold the view (pDocFrame is NULL unless it is an MDI
  // child), and the in-place menu's accelerators.
  virtual BOOL OnGetWindowContext(CFrameWnd** ppMainFrame, CFrameWnd** ppDocFrame,
                                  LPOLEINPLACEFRAMEINFO lpFrameInfo);
  // Hides the container's bars while the object shows its own, and
  // shows them again after.
  virtual BOOL OnShowControlBars(CFrameWnd* pFrameWnd, BOOL bShow);
  virtual COleDataSource* OnGetClipboardData(BOOL bIncludeLink, LPPOINT lpOffset, LPSIZE lpSize);
  virtual void Serialize(CArchive& ar);

  LPOLECLIENTSITE GetClientSite() { return &m_xOleClientSite; }

  LPOLEOBJECT m_lpObject;
  LPVIEWOBJECT2 m_lpViewObject;
  LPSTORAGE m_lpStorage;
  LPLOCKBYTES m_lpLockBytes;  // under m_lpStorage when it is in memory
  DWORD m_dwItemNumber;
  DWORD m_dwConnection;
  DVASPECT m_nDrawAspect;
  OLE_OBJTYPE m_nItemType;
  UINT m_nItemState;
  SCODE m_scLast;
  CView* m_pView;
  // While the object is active in place.
  LPOLEINPLACEOBJECT m_lpInPlaceObject;
  COleFrameHook* m_pInPlaceFrame;
  COleFrameHook* m_pInPlaceDoc;

protected:
  void GetItemStorage();
  BOOL FinishCreate(SCODE sc);
  void FinishLoad();
  void ReleaseStorage();
  void GetItemName(LPOLESTR lpszName) const;
  virtual void WriteItem(CArchive& ar);
  virtual void ReadItem(CArchive& ar);
  void WriteItemFlat(CArchive& ar);
  void ReadItemFlat(CArchive& ar);
  void SetState(UINT nNewState);
  LPOLEINPLACEOBJECT GetInPlaceObject();
  void ReleaseInPlace();

public:
  BEGIN_INTERFACE_PART(OleClientSite, IOleClientSite)
    STDMETHOD(SaveObject)();
    STDMETHOD(GetMoniker)(DWORD dwAssign, DWORD dwWhichMoniker, LPMONIKER* ppmk);
    STDMETHOD(GetContainer)(LPOLECONTAINER* ppContainer);
    STDMETHOD(ShowObject)();
    STDMETHOD(OnShowWindow)(BOOL fShow);
    STDMETHOD(RequestNewObjectLayout)();
  END_INTERFACE_PART(OleClientSite)

  BEGIN_INTERFACE_PART(AdviseSink, IAdviseSink)
    STDMETHOD_(void, OnDataChange)(LPFORMATETC pFormatetc, LPSTGMEDIUM pStgmed);
    STDMETHOD_(void, OnViewChange)(DWORD dwAspect, LONG lindex);
    STDMETHOD_(void, OnRename)(LPMONIKER pmk);
    STDMETHOD_(void, OnSave)();
    STDMETHOD_(void, OnClose)();
  END_INTERFACE_PART(AdviseSink)

  BEGIN_INTERFACE_PART(OleIPSite, IOleInPlaceSite)
    STDMETHOD(GetWindow)(HWND* phwnd);
    STDMETHOD(ContextSensitiveHelp)(BOOL fEnterMode);
    STDMETHOD(CanInPlaceActivate)();
    STDMETHOD(OnInPlaceActivate)();
    STDMETHOD(OnUIActivate)();
    STDMETHOD(GetWindowContext)(LPOLEINPLACEFRAME* ppFrame, LPOLEINPLACEUIWINDOW* ppDoc,
                                LPRECT lprcPosRect, LPRECT lprcClipRect,
                                LPOLEINPLACEFRAMEINFO lpFrameInfo);
    STDMETHOD(Scroll)(SIZE scrollExtent);
    STDMETHOD(OnUIDeactivate)(BOOL fUndoable);
    STDMETHOD(OnInPlaceDeactivate)();
    STDMETHOD(DiscardUndoState)();
    STDMETHOD(DeactivateAndUndo)();
    STDMETHOD(OnPosRectChange)(LPCRECT lprcPosRect);
  END_INTERFACE_PART(OleIPSite)

  DECLARE_INTERFACE_MAP()
  friend class COleFrameHook;
  friend class COleLinkingDoc;
};

// COleFrameHook: a container frame as an object active in place sees it
// (IOleInPlaceFrame for the main frame, IOleInPlaceUIWindow for an MDI
// child). The frame calls it for its accelerators, activation and
// layout while an object is active in it.
class COleFrameHook : public CCmdTarget {
public:
  COleFrameHook(CFrameWnd* pFrameWnd, COleClientItem* pItem);
  virtual ~COleFrameHook();

  CFrameWnd* m_pFrameWnd;
  HWND m_hWnd;
  COleClientItem* m_pActiveItem;  // the item that last asked for the frame
  LPOLEINPLACEACTIVEOBJECT m_lpActiveObject;
  HMENU m_hSharedMenu;  // the object's menu while it is on the frame
  HOLEMENU m_hOleMenu;
  CString m_strObjName;
  BOOL m_bInModalState;
  BOOL m_bInBorderChange;
  CPtrList m_listHiddenBars;  // the frame's bars hidden for the object's

  // From the frame.
  virtual BOOL OnPreTranslateMessage(MSG* pMsg);
  virtual void OnActivate(BOOL bActive);
  virtual void OnDocActivate(BOOL bActive);
  virtual void OnRecalcLayout();
  virtual BOOL OnUpdateFrameMenu();
  virtual void OnFrameDestroy();

  HACCEL GetInPlaceAccel() const;
  HMENU GetInPlaceMenu() const;

  BEGIN_INTERFACE_PART(OleInPlaceFrame, IOleInPlaceFrame)
    STDMETHOD(GetWindow)(HWND* phwnd);
    STDMETHOD(ContextSensitiveHelp)(BOOL fEnterMode);
    STDMETHOD(GetBorder)(LPRECT lprectBorder);
    STDMETHOD(RequestBorderSpace)(LPCBORDERWIDTHS pborderwidths);
    STDMETHOD(SetBorderSpace)(LPCBORDERWIDTHS pborderwidths);
    STDMETHOD(SetActiveObject)(LPOLEINPLACEACTIVEOBJECT pActiveObject, LPCOLESTR pszObjName);
    STDMETHOD(InsertMenus)(HMENU hmenuShared, LPOLEMENUGROUPWIDTHS lpMenuWidths);
    STDMETHOD(SetMenu)(HMENU hmenuShared, HOLEMENU holemenu, HWND hwndActiveObject);
    STDMETHOD(RemoveMenus)(HMENU hmenuShared);
    STDMETHOD(SetStatusText)(LPCOLESTR pszStatusText);
    STDMETHOD(EnableModeless)(BOOL fEnable);
    STDMETHOD(TranslateAccelerator)(LPMSG lpmsg, WORD wID);
  END_INTERFACE_PART(OleInPlaceFrame)

  DECLARE_INTERFACE_MAP()
};

// ---------------------------------------------------------------------
// Servers

// What COleTemplateServer::UpdateRegistry registers the program as.
enum OLE_APPTYPE {
  OAT_UNKNOWN = -1,
  OAT_INPLACE_SERVER = 0,  // a full server, active in place
  OAT_SERVER = 1,          // a server that opens its objects in its own window
  OAT_CONTAINER = 2,       // a container that links to its embeddings
  OAT_DISPATCH_OBJECT = 3, // an automation object
  OAT_DOC_OBJECT_SERVER = 4,
  OAT_DOC_OBJECT_CONTAINER = 5
};

// COleLinkingDoc: a document that other programs can find in the running
// object table while it is open.
class COleLinkingDoc : public COleDocument {
  DECLARE_DYNAMIC(COleLinkingDoc)
public:
  COleLinkingDoc();
  virtual ~COleLinkingDoc();

  // Puts the document in the running object table under its path; the
  // factory's class is the one other programs see.
  BOOL Register(COleObjectFactory* pFactory, LPCTSTR lpszPathName);
  void Revoke();

  // The item a link names ("!name" after the document's moniker): the
  // server item of that name, or the embedded object of that name in a
  // container. Override OnGetLinkedItem to make items as links ask.
  virtual COleServerItem* OnGetLinkedItem(LPCTSTR lpszItemName);
  virtual COleClientItem* OnFindEmbeddedItem(LPCTSTR lpszItemName);

  // A file moniker of the document's path; NULL while it has none.
  virtual LPMONIKER GetMoniker(OLEGETMONIKER nAssign);

  virtual BOOL OnNewDocument();
  virtual BOOL OnOpenDocument(LPCTSTR lpszPathName);
  virtual BOOL OnSaveDocument(LPCTSTR lpszPathName);
  virtual void OnCloseDocument();

  COleObjectFactory* m_pFactory;
  DWORD m_dwRegister;  // the running object table's cookie
  // What the table holds: it hands out the document's interfaces without
  // holding the document open itself.
  LPUNKNOWN m_lpRunning;

public:
  BEGIN_INTERFACE_PART(PersistFile, IPersistFile)
    STDMETHOD(GetClassID)(LPCLSID lpClassID);
    STDMETHOD(IsDirty)();
    STDMETHOD(Load)(LPCOLESTR lpszFileName, DWORD dwMode);
    STDMETHOD(Save)(LPCOLESTR lpszFileName, BOOL fRemember);
    STDMETHOD(SaveCompleted)(LPCOLESTR lpszFileName);
    STDMETHOD(GetCurFile)(LPOLESTR* lplpszFileName);
  END_INTERFACE_PART(PersistFile)

  // What an item moniker binds through: the document finds its items.
  BEGIN_INTERFACE_PART(OleItemContainer, IOleItemContainer)
    STDMETHOD(ParseDisplayName)(LPBC pbc, LPOLESTR pszDisplayName, ULONG* pchEaten,
                                LPMONIKER* ppmkOut);
    STDMETHOD(EnumObjects)(DWORD grfFlags, LPENUMUNKNOWN* ppenum);
    STDMETHOD(LockContainer)(BOOL fLock);
    STDMETHOD(GetObject)(LPOLESTR pszItem, DWORD dwSpeedNeeded, LPBINDCTX pbc, REFIID riid,
                         LPVOID* ppvObject);
    STDMETHOD(GetObjectStorage)(LPOLESTR pszItem, LPBINDCTX pbc, REFIID riid,
                                LPVOID* ppvStorage);
    STDMETHOD(IsRunning)(LPOLESTR pszItem);
  END_INTERFACE_PART(OleItemContainer)

  DECLARE_INTERFACE_MAP()
};

// COleServerDoc: the document of a server. COM creates one for each
// embedded object (through COleTemplateServer); the container sees the
// document as the object, and OnGetEmbeddedItem gives the item that
// draws it and hands out its data.
class COleServerDoc : public COleLinkingDoc {
  DECLARE_DYNAMIC(COleServerDoc)
public:
  COleServerDoc();
  virtual ~COleServerDoc();

  COleServerItem* GetEmbeddedItem();
  BOOL IsEmbedded() const { return m_bEmbedded; }
  // FALSE: the container has no client site, or it has been closed.
  LPOLECLIENTSITE GetClientSite() const { return m_lpClientSite; }

  // Calls OnUpdate on each item, which sends the new picture to the
  // containers by default.
  void UpdateAllItems(COleServerItem* pSender, LPARAM lHint = 0L, CObject* pHint = NULL,
                      DVASPECT nDrawAspect = DVASPECT_CONTENT);
  void NotifyChanged();
  void NotifyClosed();
  void NotifySaved();
  void NotifyRename(LPCTSTR lpszNewName);
  // Has the container save the object (IOleClientSite::SaveObject).
  void SaveEmbedding();

  virtual BOOL OnUpdateDocument();
  virtual void OnShowDocument(BOOL bShow);
  // An embedded document's moniker comes from its container.
  virtual LPMONIKER GetMoniker(OLEGETMONIKER nAssign);
  virtual BOOL OnSaveDocument(LPCTSTR lpszPathName);

  // In-place activation. ActivateInPlace makes the in-place frame in the
  // container's window and puts the menus and bars up; FALSE when the
  // container cannot have the object active in place.
  BOOL IsInPlaceActive() const { return m_pInPlaceFrame != NULL; }
  virtual BOOL ActivateInPlace();
  void RequestPositionChange(LPCRECT lpPosRect);
  BOOL ScrollContainerBy(CSize sizeScroll);
  BOOL DeactivateAndUndo();
  BOOL DiscardUndoState();
  void GetItemPosition(LPRECT lpPosRect) const;
  void GetItemClipRect(LPRECT lpClipRect) const;
  // The ratio of the item's size in the container to its natural size;
  // FALSE when it is 1.
  BOOL GetZoomFactor(LPSIZE lpSizeNum = NULL, LPSIZE lpSizeDenom = NULL,
                     LPCRECT lpPosRect = NULL) const;
  virtual void OnSetHostNames(LPCTSTR lpszHost, LPCTSTR lpszHostObj);
  virtual void OnClose(OLECLOSE dwCloseOption);

  // Active Documents (afxdocob.h): a document that makes a
  // CDocObjectServer here is one when its container is a document site.
  virtual CDocObjectServer* GetDocObjectServer(LPOLEDOCUMENTSITE pDocSite);
  BOOL IsDocObject() const { return m_pDocObjectServer != NULL; }
  void ActivateDocObject();
  CDocObjectServer* m_pDocObjectServer;
  virtual LPUNKNOWN GetInterfaceHook(const void* piid);

  virtual void OnCloseDocument();
  virtual BOOL SaveModified();
  virtual HMENU GetDefaultMenu();
  virtual HACCEL GetDefaultAccelerator();
  virtual void OnFinalRelease();

  CString m_strHostObj;  // the container's name for the object
  CString m_strHostApp;
  LPOLECLIENTSITE m_lpClientSite;
  LPOLEADVISEHOLDER m_lpOleAdviseHolder;
  LPDATAADVISEHOLDER m_lpDataAdviseHolder;
  COleServerItem* m_pEmbeddedItem;
  BOOL m_bCntrVisible;  // the container shows the object open
  BOOL m_bClosing;
  BOOL m_bOwnRef;  // the reference the document starts with is its own
  BOOL m_bShownBefore;
  COleIPFrameWnd* m_pInPlaceFrame;
  CWnd* m_pOrigParent;  // where the view was before the in-place frame
  DWORD m_dwOrigStyle;
  DWORD m_dwOrigStyleEx;
  LPOLEINPLACESITE m_lpInPlaceSite;

protected:
  // Make the item that stands for the whole document.
  virtual COleServerItem* OnGetEmbeddedItem() = 0;
  virtual COleIPFrameWnd* CreateInPlaceFrame(CWnd* pParentWnd);
  virtual void DestroyInPlaceFrame(COleIPFrameWnd* pFrameWnd);
  virtual void OnDeactivate();
  virtual void OnDeactivateUI(BOOL bUndoable);
  virtual void OnSetItemRects(LPCRECT lpPosRect, LPCRECT lpClipRect);
  virtual BOOL OnReactivateAndUndo();
  virtual void OnFrameWindowActivate(BOOL bActivate);
  virtual void OnDocWindowActivate(BOOL bActivate);
  virtual void OnShowControlBars(CFrameWnd* pFrameWnd, BOOL bShow);
  virtual void OnResizeBorder(LPCRECT lpRectBorder, LPOLEINPLACEUIWINDOW lpUIWindow,
                              BOOL bFrame);
  BOOL UIActivate();
  virtual void OnNewEmbedding(LPSTORAGE lpStorage);
  virtual void OnOpenEmbedding(LPSTORAGE lpStorage);
  virtual void OnSaveEmbedding(LPSTORAGE lpStorage);
  void GetClassIDOf(LPCLSID lpClassID);
  void UpdateMenuText(CMenu* pMenu);

  afx_msg void OnFileUpdate();
  afx_msg void OnFileSaveCopyAs();
  afx_msg void OnUpdateFileUpdate(CCmdUI* pCmdUI);

public:
  BEGIN_INTERFACE_PART(PersistStorage, IPersistStorage)
    STDMETHOD(GetClassID)(LPCLSID lpClassID);
    STDMETHOD(IsDirty)();
    STDMETHOD(InitNew)(LPSTORAGE pStg);
    STDMETHOD(Load)(LPSTORAGE pStg);
    STDMETHOD(Save)(LPSTORAGE pStgSave, BOOL fSameAsLoad);
    STDMETHOD(SaveCompleted)(LPSTORAGE pStgNew);
    STDMETHOD(HandsOffStorage)();
  END_INTERFACE_PART(PersistStorage)

  BEGIN_INTERFACE_PART(OleObject, IOleObject)
    STDMETHOD(SetClientSite)(LPOLECLIENTSITE pClientSite);
    STDMETHOD(GetClientSite)(LPOLECLIENTSITE* ppClientSite);
    STDMETHOD(SetHostNames)(LPCOLESTR szContainerApp, LPCOLESTR szContainerObj);
    STDMETHOD(Close)(DWORD dwSaveOption);
    STDMETHOD(SetMoniker)(DWORD dwWhichMoniker, LPMONIKER pmk);
    STDMETHOD(GetMoniker)(DWORD dwAssign, DWORD dwWhichMoniker, LPMONIKER* ppmk);
    STDMETHOD(InitFromData)(LPDATAOBJECT pDataObject, BOOL fCreation, DWORD dwReserved);
    STDMETHOD(GetClipboardData)(DWORD dwReserved, LPDATAOBJECT* ppDataObject);
    STDMETHOD(DoVerb)(LONG iVerb, LPMSG lpmsg, LPOLECLIENTSITE pActiveSite, LONG lindex,
                      HWND hwndParent, LPCRECT lprcPosRect);
    STDMETHOD(EnumVerbs)(LPENUMOLEVERB* ppEnumOleVerb);
    STDMETHOD(Update)();
    STDMETHOD(IsUpToDate)();
    STDMETHOD(GetUserClassID)(LPCLSID pClsid);
    STDMETHOD(GetUserType)(DWORD dwFormOfType, LPOLESTR* pszUserType);
    STDMETHOD(SetExtent)(DWORD dwDrawAspect, LPSIZEL psizel);
    STDMETHOD(GetExtent)(DWORD dwDrawAspect, LPSIZEL psizel);
    STDMETHOD(Advise)(LPADVISESINK pAdvSink, DWORD* pdwConnection);
    STDMETHOD(Unadvise)(DWORD dwConnection);
    STDMETHOD(EnumAdvise)(LPENUMSTATDATA* ppenumAdvise);
    STDMETHOD(GetMiscStatus)(DWORD dwAspect, DWORD* pdwStatus);
    STDMETHOD(SetColorScheme)(LPLOGPALETTE pLogpal);
  END_INTERFACE_PART(OleObject)

  BEGIN_INTERFACE_PART(DataObject, IDataObject)
    STDMETHOD(GetData)(LPFORMATETC pformatetcIn, LPSTGMEDIUM pmedium);
    STDMETHOD(GetDataHere)(LPFORMATETC pformatetc, LPSTGMEDIUM pmedium);
    STDMETHOD(QueryGetData)(LPFORMATETC pformatetc);
    STDMETHOD(GetCanonicalFormatEtc)(LPFORMATETC pformatetcIn, LPFORMATETC pformatetcOut);
    STDMETHOD(SetData)(LPFORMATETC pformatetc, LPSTGMEDIUM pmedium, BOOL fRelease);
    STDMETHOD(EnumFormatEtc)(DWORD dwDirection, LPENUMFORMATETC* ppenumFormatEtc);
    STDMETHOD(DAdvise)(LPFORMATETC pformatetc, DWORD advf, LPADVISESINK pAdvSink,
                       DWORD* pdwConnection);
    STDMETHOD(DUnadvise)(DWORD dwConnection);
    STDMETHOD(EnumDAdvise)(LPENUMSTATDATA* ppenumAdvise);
  END_INTERFACE_PART(DataObject)

  BEGIN_INTERFACE_PART(OleInPlaceObject, IOleInPlaceObject)
    STDMETHOD(GetWindow)(HWND* phwnd);
    STDMETHOD(ContextSensitiveHelp)(BOOL fEnterMode);
    STDMETHOD(InPlaceDeactivate)();
    STDMETHOD(UIDeactivate)();
    STDMETHOD(SetObjectRects)(LPCRECT lprcPosRect, LPCRECT lprcClipRect);
    STDMETHOD(ReactivateAndUndo)();
  END_INTERFACE_PART(OleInPlaceObject)

  BEGIN_INTERFACE_PART(OleInPlaceActiveObject, IOleInPlaceActiveObject)
    STDMETHOD(GetWindow)(HWND* phwnd);
    STDMETHOD(ContextSensitiveHelp)(BOOL fEnterMode);
    STDMETHOD(TranslateAccelerator)(LPMSG lpmsg);
    STDMETHOD(OnFrameWindowActivate)(BOOL fActivate);
    STDMETHOD(OnDocWindowActivate)(BOOL fActivate);
    STDMETHOD(ResizeBorder)(LPCRECT prcBorder, LPOLEINPLACEUIWINDOW pUIWindow, BOOL fFrameWindow);
    STDMETHOD(EnableModeless)(BOOL fEnable);
  END_INTERFACE_PART(OleInPlaceActiveObject)

  DECLARE_INTERFACE_MAP()
  DECLARE_MESSAGE_MAP()
  friend class COleServerItem;
  friend class CDocObjectServer;
  friend class COleIPFrameWnd;
  friend class COleResizeBar;
};

// COleCntrFrameWnd: a container's frame as the server sees it while its
// object is active in place there. Its window is the container's; the
// server's toolbars dock on it, and its layout asks the container for
// the border space they take.
class COleCntrFrameWnd : public CFrameWnd {
  DECLARE_DYNAMIC(COleCntrFrameWnd)
public:
  COleCntrFrameWnd(COleIPFrameWnd* pInPlaceFrame, HWND hWnd, LPOLEINPLACEUIWINDOW lpWindow);
  virtual ~COleCntrFrameWnd();

  virtual void RecalcLayout(BOOL bNotify = TRUE);
  virtual CFrameWnd* GetActiveFrame();
  virtual void PostNcDestroy() {}

  COleIPFrameWnd* m_pInPlaceFrame;
  LPOLEINPLACEUIWINDOW m_lpWindow;
  CPtrList m_listHiddenBars;  // bars hidden while the object is not UI active
};

// COleIPFrameWnd: the frame a server's view sits in while the object is
// active in place: a child of the container's window. Programs derive
// their in-place frame from it and make their toolbars in
// OnCreateControlBars, on the container's frames.
class COleIPFrameWnd : public CFrameWnd {
  DECLARE_DYNCREATE(COleIPFrameWnd)
public:
  COleIPFrameWnd();
  virtual ~COleIPFrameWnd();

  virtual BOOL OnCreateControlBars(CFrameWnd* pWndFrame, CFrameWnd* pWndDoc);
  // Places the frame over the item's rectangle in the container's window,
  // cut to the visible part.
  virtual void RepositionFrame(LPCRECT lpPosRect, LPCRECT lpClipRect);

  virtual BOOL LoadFrame(UINT nIDResource, DWORD dwDefaultStyle, CWnd* pParentWnd,
                         CCreateContext* pContext = NULL);
  virtual BOOL PreCreateWindow(CREATESTRUCT& cs);
  virtual BOOL PreTranslateMessage(MSG* pMsg);
  virtual void RecalcLayout(BOOL bNotify = TRUE);
  virtual void OnIdleUpdateCmdUI();
  virtual void OnUpdateFrameMenu(HMENU) {}

  BOOL m_bUIActive;
  BOOL m_bInsideRecalc;
  BOOL m_bBarsCreated;
  HMENU m_hSharedMenu;
  HOLEMENU m_hOleMenu;
  CRect m_rectPos;
  CRect m_rectClip;
  LPOLEINPLACEFRAME m_lpFrame;
  LPOLEINPLACEUIWINDOW m_lpDocFrame;
  OLEINPLACEFRAMEINFO m_frameInfo;
  COleCntrFrameWnd* m_pMainFrame;
  COleCntrFrameWnd* m_pDocFrame;

  // The shared menu: the container's groups and the in-place menu's.
  BOOL BuildSharedMenu();
  void DestroySharedMenu();

protected:
  afx_msg void OnSize(UINT nType, int cx, int cy);
  afx_msg void OnDestroy();
  // Status text goes to the container's status bar.
  afx_msg LRESULT OnSetMessageString(WPARAM wParam, LPARAM lParam);
  DECLARE_MESSAGE_MAP()
  friend class COleServerDoc;
};

// COleResizeBar: the hatched border with handles around an object active
// in place; dragging a handle asks the container for a new size.
class COleResizeBar : public CControlBar {
  DECLARE_DYNAMIC(COleResizeBar)
public:
  COleResizeBar();
  virtual ~COleResizeBar();
  BOOL Create(CWnd* pParentWnd, DWORD dwStyle = WS_CHILD | WS_VISIBLE,
              UINT nID = AFX_IDW_RESIZE_BAR);
  virtual void OnUpdateCmdUI(CFrameWnd*, BOOL) {}
  int GetBorderWidth() const { return m_tracker.m_nHandleSize; }

protected:
  CRectTracker m_tracker;
  afx_msg LRESULT OnSizeParent(WPARAM wParam, LPARAM lParam);
  afx_msg void OnPaint();
  afx_msg BOOL OnEraseBkgnd(CDC* pDC);
  afx_msg BOOL OnSetCursor(CWnd* pWnd, UINT nHitTest, UINT message);
  afx_msg void OnLButtonDown(UINT nFlags, CPoint point);
  DECLARE_MESSAGE_MAP()
};

// COleServerItem: what a server shows a container: the whole document
// (the embedded item) or a part of it. OnDraw draws it, its data source
// hands out its data, and NotifyChanged tells the containers.
class COleServerItem : public CDocItem {
  DECLARE_DYNAMIC(COleServerItem)
public:
  COleServerItem(COleServerDoc* pServerDoc, BOOL bAutoDelete);
  virtual ~COleServerItem();

  COleServerDoc* GetDocument() const { return (COleServerDoc*)m_pDocument; }
  COleDataSource* GetDataSource() { return &m_dataSource; }
  BOOL IsConnected() const;
  BOOL IsLinkedItem() const;
  const CString& GetItemName() const { return m_strItemName; }
  void SetItemName(LPCTSTR lpszItemName) { m_strItemName = lpszItemName; }

  void NotifyChanged(DVASPECT nDrawAspect = DVASPECT_CONTENT);
  void CopyToClipboard(BOOL bIncludeLink = FALSE);
  DROPEFFECT DoDragDrop(LPCRECT lpItemRect, CPoint ptOffset, BOOL bIncludeLink = FALSE,
                        DWORD dwEffects = DROPEFFECT_COPY | DROPEFFECT_MOVE,
                        LPCRECT lpRectStartDrag = NULL);
  // Puts the item's data in pDataSource, as CopyToClipboard does.
  void GetClipboardData(COleDataSource* pDataSource, BOOL bIncludeLink = FALSE,
                        LPPOINT lpOffset = NULL, LPSIZE lpSize = NULL);
  // The document saved into a new storage ("Embed Source").
  void GetEmbedSourceData(LPSTGMEDIUM lpStgMedium);
  void GetObjectDescriptorData(LPPOINT lpOffset, LPSIZE lpSize, LPSTGMEDIUM lpStgMedium);
  // The item's moniker and class in a stream ("Link Source"): what a
  // container makes a link from. FALSE when the item has no moniker.
  BOOL GetLinkSourceData(LPSTGMEDIUM lpStgMedium);
  // The document's moniker, then "!" and the item's name for a linked item.
  LPMONIKER GetMoniker(OLEGETMONIKER nAssign);
  BOOL GetMetafileData(LPFORMATETC lpFormatEtc, LPSTGMEDIUM lpStgMedium);
  virtual void AddOtherClipboardData(COleDataSource* pDataSource);

  // Overridables
  // Draws the item; rSize is its extent in HIMETRIC.
  virtual BOOL OnDraw(CDC* pDC, CSize& rSize) = 0;
  virtual BOOL OnDrawEx(CDC* pDC, DVASPECT nDrawAspect, CSize& rSize);
  virtual BOOL OnGetExtent(DVASPECT nDrawAspect, CSize& rSize);
  virtual BOOL OnSetExtent(DVASPECT nDrawAspect, const CSize& size);
  virtual void OnUpdate(COleServerItem* pSender, LPARAM lHint, CObject* pHint,
                        DVASPECT nDrawAspect);
  virtual void OnDoVerb(LONG iVerb);
  virtual void OnShow();
  virtual void OnOpen();
  virtual void OnHide();
  virtual BOOL OnRenderData(LPFORMATETC lpFormatEtc, LPSTGMEDIUM lpStgMedium);
  virtual BOOL OnRenderGlobalData(LPFORMATETC lpFormatEtc, HGLOBAL* phGlobal);
  virtual BOOL OnRenderFileData(LPFORMATETC lpFormatEtc, CFile* pFile);
  virtual BOOL OnSetData(LPFORMATETC lpFormatEtc, LPSTGMEDIUM lpStgMedium, BOOL bRelease);
  virtual void OnFinalRelease();

  BOOL m_bAutoDelete;
  CString m_strItemName;
  CSize m_sizeExtent;  // HIMETRIC, for OnGetExtent's default
  // A linked item's own connections; the embedded item uses its document's.
  LPOLEADVISEHOLDER m_lpOleAdviseHolder;
  LPDATAADVISEHOLDER m_lpDataAdviseHolder;
  BOOL m_bLinkLocked;  // a link holds the item, and the item its document
  DWORD m_dwRegister;  // while linked to: its running object table cookie
  LPUNKNOWN m_lpRunning;

protected:
  // The data source that asks the item to render.
  class CItemDataSource : public COleDataSource {
  public:
    COleServerItem* m_pItem;
    virtual BOOL OnRenderData(LPFORMATETC lpFormatEtc, LPSTGMEDIUM lpStgMedium);
    virtual BOOL OnRenderFileData(LPFORMATETC lpFormatEtc, CFile* pFile);
    virtual BOOL OnRenderGlobalData(LPFORMATETC lpFormatEtc, HGLOBAL* phGlobal);
    virtual BOOL OnSetData(LPFORMATETC lpFormatEtc, LPSTGMEDIUM lpStgMedium, BOOL bRelease);
    virtual void OnFinalRelease() {}  // part of the item
  };
  CItemDataSource m_dataSource;
  friend class CItemDataSource;
  friend class COleServerDoc;
  friend class COleLinkingDoc;
  void Disconnect();
  void RegisterLinked();
  void RevokeLinked();

public:
  // What a link to the item binds to.
  BEGIN_INTERFACE_PART(OleObject, IOleObject)
    STDMETHOD(SetClientSite)(LPOLECLIENTSITE pClientSite);
    STDMETHOD(GetClientSite)(LPOLECLIENTSITE* ppClientSite);
    STDMETHOD(SetHostNames)(LPCOLESTR szContainerApp, LPCOLESTR szContainerObj);
    STDMETHOD(Close)(DWORD dwSaveOption);
    STDMETHOD(SetMoniker)(DWORD dwWhichMoniker, LPMONIKER pmk);
    STDMETHOD(GetMoniker)(DWORD dwAssign, DWORD dwWhichMoniker, LPMONIKER* ppmk);
    STDMETHOD(InitFromData)(LPDATAOBJECT pDataObject, BOOL fCreation, DWORD dwReserved);
    STDMETHOD(GetClipboardData)(DWORD dwReserved, LPDATAOBJECT* ppDataObject);
    STDMETHOD(DoVerb)(LONG iVerb, LPMSG lpmsg, LPOLECLIENTSITE pActiveSite, LONG lindex,
                      HWND hwndParent, LPCRECT lprcPosRect);
    STDMETHOD(EnumVerbs)(LPENUMOLEVERB* ppEnumOleVerb);
    STDMETHOD(Update)();
    STDMETHOD(IsUpToDate)();
    STDMETHOD(GetUserClassID)(LPCLSID pClsid);
    STDMETHOD(GetUserType)(DWORD dwFormOfType, LPOLESTR* pszUserType);
    STDMETHOD(SetExtent)(DWORD dwDrawAspect, LPSIZEL psizel);
    STDMETHOD(GetExtent)(DWORD dwDrawAspect, LPSIZEL psizel);
    STDMETHOD(Advise)(LPADVISESINK pAdvSink, DWORD* pdwConnection);
    STDMETHOD(Unadvise)(DWORD dwConnection);
    STDMETHOD(EnumAdvise)(LPENUMSTATDATA* ppenumAdvise);
    STDMETHOD(GetMiscStatus)(DWORD dwAspect, DWORD* pdwStatus);
    STDMETHOD(SetColorScheme)(LPLOGPALETTE pLogpal);
  END_INTERFACE_PART(OleObject)

  BEGIN_INTERFACE_PART(DataObject, IDataObject)
    STDMETHOD(GetData)(LPFORMATETC pformatetcIn, LPSTGMEDIUM pmedium);
    STDMETHOD(GetDataHere)(LPFORMATETC pformatetc, LPSTGMEDIUM pmedium);
    STDMETHOD(QueryGetData)(LPFORMATETC pformatetc);
    STDMETHOD(GetCanonicalFormatEtc)(LPFORMATETC pformatetcIn, LPFORMATETC pformatetcOut);
    STDMETHOD(SetData)(LPFORMATETC pformatetc, LPSTGMEDIUM pmedium, BOOL fRelease);
    STDMETHOD(EnumFormatEtc)(DWORD dwDirection, LPENUMFORMATETC* ppenumFormatEtc);
    STDMETHOD(DAdvise)(LPFORMATETC pformatetc, DWORD advf, LPADVISESINK pAdvSink,
                       DWORD* pdwConnection);
    STDMETHOD(DUnadvise)(DWORD dwConnection);
    STDMETHOD(EnumDAdvise)(LPENUMSTATDATA* ppenumAdvise);
  END_INTERFACE_PART(DataObject)

  DECLARE_INTERFACE_MAP()
};

// COleTemplateServer: the class factory that makes a new document of a
// template for each object a container creates.
class COleTemplateServer : public COleObjectFactory {
  DECLARE_DYNAMIC(COleTemplateServer)
public:
  COleTemplateServer();

  void ConnectTemplate(REFCLSID clsid, CDocTemplate* pDocTemplate, BOOL bMultiInstance);
  // Registers the template's document type as an OLE server under the
  // ProgID, type name and short name of its document strings.
  void UpdateRegistry(OLE_APPTYPE nAppType = OAT_INPLACE_SERVER,
                      LPCTSTR* rglpszRegister = NULL, LPCTSTR* rglpszOverwrite = NULL,
                      BOOL bRegister = TRUE);
  virtual BOOL UpdateRegistry(BOOL bRegister);
  BOOL Unregister();

  CDocTemplate* m_pDocTemplate;
  OLE_APPTYPE m_nAppType;

protected:
  virtual CCmdTarget* OnCreateObject();
};

// Fills the object verb item of an Edit menu ("&Object" at nPosition) with
// the item's verbs, from nIDVerbMin on; NULL disables it.
void AFXAPI AfxOleSetEditMenu(COleClientItem* pClient, CMenu* pMenu, UINT iMenuItem,
                              UINT nIDVerbMin, UINT nIDVerbMax = 0, UINT nIDConvert = 0);

#endif  // __AFXOLE_H__
