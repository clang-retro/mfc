// afxodlgs.h - the OLE dialogs: Insert Object, Paste Special, Links,
// Change Icon, Convert, Server Busy, Update Links, Change Source and
// Object Properties, over oledlg.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#ifndef __AFXODLGS_H__
#define __AFXODLGS_H__

#include "afxole.h"
#include "afxdlgs.h"
#include "afxtempl.h"
#include <oledlg.h>

// ---------------------------------------------------------------------
// COleDialog: what the OLE dialogs share. DoModal returns IDOK, IDCANCEL
// or, when the dialog failed, IDABORT; GetLastError gives the OLEUI_
// result.

class COleDialog : public CCommonDialog {
  DECLARE_DYNAMIC(COleDialog)
public:
  explicit COleDialog(CWnd* pParentWnd) : CCommonDialog(pParentWnd), m_nLastError(OLEUI_OK) {}
  UINT GetLastError() const { return m_nLastError; }

protected:
  int MapResult(UINT nResult);
  UINT m_nLastError;
};

// ---------------------------------------------------------------------
// Insert Object

class COleInsertDialog : public COleDialog {
  DECLARE_DYNAMIC(COleInsertDialog)
public:
  explicit COleInsertDialog(DWORD dwFlags = IOF_SELECTCREATENEW, CWnd* pParentWnd = NULL);
  virtual ~COleInsertDialog();

  enum Selection { createNewItem, insertFromFile, linkToFile };
  enum { DocObjectsOnly = 1, ControlsOnly = 2 };

  virtual INT_PTR DoModal();
  INT_PTR DoModal(DWORD dwFlags);
  // Makes the object the user chose in pItem.
  BOOL CreateItem(COleClientItem* pItem);

  UINT GetSelectionType() const;
  REFCLSID GetClassID() const { return m_io.clsid; }
  CString GetPathName() const { return m_szFileName; }
  DVASPECT GetDrawAspect() const {
    return (m_io.dwFlags & IOF_CHECKDISPLAYASICON) ? DVASPECT_ICON : DVASPECT_CONTENT;
  }
  HGLOBAL GetIconicMetafile() const { return m_io.hMetaPict; }

  OLEUIINSERTOBJECT m_io;
  TCHAR m_szFileName[MAX_PATH];
};

// ---------------------------------------------------------------------
// Paste Special

class COlePasteSpecialDialog : public COleDialog {
  DECLARE_DYNAMIC(COlePasteSpecialDialog)
public:
  explicit COlePasteSpecialDialog(DWORD dwFlags = PSF_SELECTPASTE,
                                  COleDataObject* pDataObject = NULL, CWnd* pParentWnd = NULL);
  virtual ~COlePasteSpecialDialog();

  enum Selection { pasteLink = 1, pasteNormal = 2, pasteStatic = 3, pasteOther = 4 };

  void AddFormat(const FORMATETC& formatEtc, LPTSTR lpszFormat, LPTSTR lpszResult,
                 DWORD flags);
  void AddFormat(UINT cf, DWORD tymed, UINT nFormatID, BOOL bEnableIcon, BOOL bLink);
  // Embedded objects, links, metafiles and bitmaps.
  void AddStandardFormats(BOOL bEnableLink = TRUE);
  int AddLinkEntry(UINT cf);

  virtual INT_PTR DoModal();
  BOOL CreateItem(COleClientItem* pNewItem);

  UINT GetSelectionType() const;
  int GetPasteIndex() const { return m_ps.nSelectedIndex; }
  DVASPECT GetDrawAspect() const {
    return (m_ps.dwFlags & PSF_CHECKDISPLAYASICON) ? DVASPECT_ICON : DVASPECT_CONTENT;
  }
  HGLOBAL GetIconicMetafile() const { return m_ps.hMetaPict; }

  OLEUIPASTESPECIAL m_ps;

protected:
  CArray<OLEUIPASTEENTRY, OLEUIPASTEENTRY&> m_arrEntries;
  CStringArray m_arrStrings;  // the entries' text
  UINT m_arrLinkTypes[8];
  COleDataObject m_dataObject;
};

// ---------------------------------------------------------------------
// Links: the linked items of a document.

class COleLinksDialog : public COleDialog {
  DECLARE_DYNAMIC(COleLinksDialog)
public:
  COleLinksDialog(COleDocument* pDoc, CView* pView, DWORD dwFlags = 0,
                  CWnd* pParentWnd = NULL);
  virtual ~COleLinksDialog();
  virtual INT_PTR DoModal();

  OLEUIEDITLINKS m_el;

  // The links, numbered from 1 in document order.
  COleClientItem* GetLink(DWORD dwLink) const;

  COleDocument* m_pDocument;
  CView* m_pView;

public:
  // The links' IOleUILinkContainer, which is also their IOleUILinkInfo
  // (for Object Properties).
  BEGIN_INTERFACE_PART(OleUILinkContainer, IOleUILinkInfo)
    STDMETHOD_(DWORD, GetNextLink)(DWORD dwLink);
    STDMETHOD(SetLinkUpdateOptions)(DWORD dwLink, DWORD dwUpdateOpt);
    STDMETHOD(GetLinkUpdateOptions)(DWORD dwLink, DWORD* lpdwUpdateOpt);
    STDMETHOD(SetLinkSource)(DWORD dwLink, LPTSTR lpszDisplayName, ULONG lenFileName,
                             ULONG* pchEaten, BOOL fValidateSource);
    STDMETHOD(GetLinkSource)(DWORD dwLink, LPTSTR* lplpszDisplayName, ULONG* lplenFileName,
                             LPTSTR* lplpszFullLinkType, LPTSTR* lplpszShortLinkType,
                             BOOL* lpfSourceAvailable, BOOL* lpfIsSelected);
    STDMETHOD(OpenLinkSource)(DWORD dwLink);
    STDMETHOD(UpdateLink)(DWORD dwLink, BOOL fErrorMessage, BOOL fReserved);
    STDMETHOD(CancelLink)(DWORD dwLink);
    STDMETHOD(GetLastUpdate)(DWORD dwLink, FILETIME* lpLastUpdate);
  END_INTERFACE_PART(OleUILinkContainer)

  // The number of pItem among the links, or 0.
  DWORD FindLink(COleClientItem* pItem) const;

  DECLARE_INTERFACE_MAP()
};

// Update Links: brings every automatic link of a document up to date,
// showing progress.
class COleUpdateDialog : public COleLinksDialog {
  DECLARE_DYNAMIC(COleUpdateDialog)
public:
  explicit COleUpdateDialog(COleDocument* pDoc, BOOL bUpdateLinks = TRUE,
                            BOOL bUpdateEmbeddings = FALSE, CWnd* pParentWnd = NULL);
  virtual INT_PTR DoModal();

  CString m_strCaption;
};

// ---------------------------------------------------------------------
// Change Icon

class COleChangeIconDialog : public COleDialog {
  DECLARE_DYNAMIC(COleChangeIconDialog)
public:
  explicit COleChangeIconDialog(COleClientItem* pItem, DWORD dwFlags = CIF_SELECTCURRENT,
                                CWnd* pParentWnd = NULL);
  virtual ~COleChangeIconDialog();
  virtual INT_PTR DoModal();
  // Gives the item the chosen icon.
  BOOL DoChangeIcon(COleClientItem* pItem);
  HGLOBAL GetIconicMetafile() const { return m_ci.hMetaPict; }

  OLEUICHANGEICON m_ci;
};

// ---------------------------------------------------------------------
// Convert

class COleConvertDialog : public COleDialog {
  DECLARE_DYNAMIC(COleConvertDialog)
public:
  explicit COleConvertDialog(COleClientItem* pItem, DWORD dwFlags = CF_SELECTCONVERTTO,
                             CLSID* pClassID = NULL, CWnd* pParentWnd = NULL);
  virtual ~COleConvertDialog();

  enum Selection { noConversion, convertItem, activateAs };

  virtual INT_PTR DoModal();
  // Converts the item, or makes its class run as the new one.
  BOOL DoConvert(COleClientItem* pItem);
  UINT GetSelectionType() const;
  REFCLSID GetClassID() const { return m_cv.clsidNew; }
  DVASPECT GetDrawAspect() const { return (DVASPECT)m_cv.dvAspect; }
  HGLOBAL GetIconicMetafile() const { return m_cv.hMetaPict; }

  OLEUICONVERT m_cv;
  CString m_strUserType;
};

// ---------------------------------------------------------------------
// Server Busy

class COleBusyDialog : public COleDialog {
  DECLARE_DYNAMIC(COleBusyDialog)
public:
  explicit COleBusyDialog(HTASK htaskBusy, BOOL bNotResponding = FALSE, DWORD dwFlags = 0,
                          CWnd* pParentWnd = NULL);
  virtual ~COleBusyDialog();

  enum Selection { switchTo = 1, retry = 2, callUnblocked = 3 };

  virtual INT_PTR DoModal();
  UINT GetSelectionType() const { return m_selection; }

  OLEUIBUSY m_bz;

protected:
  Selection m_selection;
};

// ---------------------------------------------------------------------
// Change Source: a new source for a link. On OK with a valid source, the
// link takes it.

class COleChangeSourceDialog : public COleDialog {
  DECLARE_DYNAMIC(COleChangeSourceDialog)
public:
  explicit COleChangeSourceDialog(COleClientItem* pItem, CWnd* pParentWnd = NULL);
  virtual ~COleChangeSourceDialog();
  virtual INT_PTR DoModal();

  // The source's whole name, its file part and its item part.
  CString GetDisplayName() const { return CString(m_cs.lpszDisplayName); }
  CString GetFileName() const { return GetDisplayName().Left((int)m_cs.nFileLength); }
  CString GetItemName() const { return GetDisplayName().Mid((int)m_cs.nFileLength); }
  // What the source changed from and to, when only their ends differ.
  CString GetFromPrefix() const { return CString(m_cs.lpszFrom); }
  CString GetToPrefix() const { return CString(m_cs.lpszTo); }
  BOOL IsValidSource() const { return (m_cs.dwFlags & CSF_VALIDSOURCE) != 0; }

  OLEUICHANGESOURCE m_cs;

protected:
  COleLinksDialog m_xLinkInfo;  // the item's document's links
  COleClientItem* m_pItem;
};

// ---------------------------------------------------------------------
// Object Properties: the General, View and (for a link) Link pages of an
// item.

class COlePropertiesDialog : public COleDialog {
  DECLARE_DYNAMIC(COlePropertiesDialog)
public:
  explicit COlePropertiesDialog(COleClientItem* pItem, UINT nScaleMin = 10,
                                UINT nScaleMax = 500, CWnd* pParentWnd = NULL);
  virtual ~COlePropertiesDialog();
  virtual INT_PTR DoModal();

  // The View page changed the item's scale (in percent); TRUE when the
  // program applied it.
  virtual BOOL OnApplyScale(COleClientItem* pItem, int nCurrentScale, BOOL bRelativeToOrig);

  OLEUIOBJECTPROPS m_op;
  OLEUIGNRLPROPS m_gp;
  OLEUIVIEWPROPS m_vp;
  OLEUILINKPROPS m_lp;
  PROPSHEETHEADER m_psh;

  BEGIN_INTERFACE_PART(OleUIObjInfo, IOleUIObjInfo)
    STDMETHOD(GetObjectInfo)(DWORD dwObject, DWORD* lpdwObjSize, LPTSTR* lplpszLabel,
                             LPTSTR* lplpszType, LPTSTR* lplpszShortType,
                             LPTSTR* lplpszLocation);
    STDMETHOD(GetConvertInfo)(DWORD dwObject, CLSID* lpClassID, WORD* lpwFormat,
                              CLSID* lpConvertDefaultClassID, LPCLSID* lplpClsidExclude,
                              UINT* lpcClsidExclude);
    STDMETHOD(ConvertObject)(DWORD dwObject, REFCLSID clsidNew);
    STDMETHOD(GetViewInfo)(DWORD dwObject, HGLOBAL* phMetaPict, DWORD* pdvAspect,
                           int* pnCurrentScale);
    STDMETHOD(SetViewInfo)(DWORD dwObject, HGLOBAL hMetaPict, DWORD dvAspect,
                           int nCurrentScale, BOOL bRelativeToOrig);
  END_INTERFACE_PART(OleUIObjInfo)

  DECLARE_INTERFACE_MAP()

protected:
  COleLinksDialog m_xLinkInfo;  // the item's document's links
  COleClientItem* m_pItem;
};

#endif  // __AFXODLGS_H__
