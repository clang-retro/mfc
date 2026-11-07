// afxole.h - OLE: data transfer through the clipboard and drag and drop
// (COleDataSource, COleDataObject, COleDropSource, COleDropTarget), and
// documents that contain linked and embedded objects (COleDocument,
// COleClientItem), with COleStreamFile for streams in a storage.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
#ifndef __AFXOLE_H__
#define __AFXOLE_H__

#include "afxwin.h"
#include "afxext.h"
#include "afxdisp.h"

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
  virtual BOOL OnCmdMsg(UINT nID, int nCode, void* pExtra, AFX_CMDHANDLERINFO* pHandlerInfo);

  virtual void LoadFromStorage();
  virtual void SaveToStorage(CObject* pObject = NULL);
  DWORD GetNewItemNumber() { return m_dwNextItemNumber++; }

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

  DECLARE_INTERFACE_MAP()
};

// Fills the object verb item of an Edit menu ("&Object" at nPosition) with
// the item's verbs, from nIDVerbMin on; NULL disables it.
void AFXAPI AfxOleSetEditMenu(COleClientItem* pClient, CMenu* pMenu, UINT iMenuItem,
                              UINT nIDVerbMin, UINT nIDVerbMax = 0, UINT nIDConvert = 0);

#endif  // __AFXOLE_H__
