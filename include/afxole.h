// afxole.h - OLE: data transfer through the clipboard and drag and drop
// (COleDataSource, COleDataObject, COleDropSource, COleDropTarget).
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
#ifndef __AFXOLE_H__
#define __AFXOLE_H__

#include "afxwin.h"
#include "afxext.h"
#include "afxdisp.h"

class COleDataSource;
class COleDataObject;
class COleDropSource;
class COleDropTarget;

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

#endif  // __AFXOLE_H__
