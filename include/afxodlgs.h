// afxodlgs.h - the OLE dialogs: Insert Object, Paste Special, Links,
// Change Icon, Convert, Server Busy and Update Links, over oledlg.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
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
  BEGIN_INTERFACE_PART(OleUILinkContainer, IOleUILinkContainer)
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
  END_INTERFACE_PART(OleUILinkContainer)

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

#endif  // __AFXODLGS_H__
