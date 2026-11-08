// oledlgs.cpp - the OLE dialogs over oledlg, and the COleDocument commands
// that show them.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include "afximpl.h"
#include <afxodlgs.h>

IMPLEMENT_DYNAMIC(COleDialog, CCommonDialog)
IMPLEMENT_DYNAMIC(COleInsertDialog, COleDialog)
IMPLEMENT_DYNAMIC(COlePasteSpecialDialog, COleDialog)
IMPLEMENT_DYNAMIC(COleLinksDialog, COleDialog)
IMPLEMENT_DYNAMIC(COleUpdateDialog, COleLinksDialog)
IMPLEMENT_DYNAMIC(COleChangeIconDialog, COleDialog)
IMPLEMENT_DYNAMIC(COleConvertDialog, COleDialog)
IMPLEMENT_DYNAMIC(COleBusyDialog, COleDialog)
IMPLEMENT_DYNAMIC(COleChangeSourceDialog, COleDialog)
IMPLEMENT_DYNAMIC(COlePropertiesDialog, COleDialog)

// Runs a dialog with the safe owner as its owner, as CDialog::DoModal does.
template <typename F>
static UINT RunOleDialog(CWnd* pParentWnd, HWND* phWndOwner, F run) {
  HWND hWndTop = NULL;
  *phWndOwner = _AfxGetSafeOwner(pParentWnd->GetSafeHwnd(), &hWndTop);
  UINT nResult = run();
  if (hWndTop != NULL) ::EnableWindow(hWndTop, TRUE);
  return nResult;
}

static void FreeMetafilePict(HGLOBAL& hMetaPict) {
  if (hMetaPict == NULL) return;
  METAFILEPICT* pmfp = (METAFILEPICT*)::GlobalLock(hMetaPict);
  if (pmfp != NULL) {
    if (pmfp->hMF != NULL) ::DeleteMetaFile(pmfp->hMF);
    ::GlobalUnlock(hMetaPict);
  }
  ::GlobalFree(hMetaPict);
  hMetaPict = NULL;
}

static LPTSTR TaskString(LPCTSTR lpsz) {
  size_t cb = (lstrlen(lpsz) + 1) * sizeof(TCHAR);
  LPTSTR p = (LPTSTR)::CoTaskMemAlloc(cb);
  if (p != NULL) memcpy(p, lpsz, cb);
  return p;
}

int COleDialog::MapResult(UINT nResult) {
  m_nLastError = nResult;
  switch (nResult) {
  case OLEUI_OK:
    return IDOK;
  case OLEUI_CANCEL:
    return IDCANCEL;
  default:
    return IDABORT;
  }
}

// ---------------------------------------------------------------------
// Insert Object

COleInsertDialog::COleInsertDialog(DWORD dwFlags, CWnd* pParentWnd) : COleDialog(pParentWnd) {
  memset(&m_io, 0, sizeof(m_io));
  m_io.cbStruct = sizeof(m_io);
  m_io.dwFlags = dwFlags;
  m_szFileName[0] = 0;
  m_io.lpszFile = m_szFileName;
  m_io.cchFile = MAX_PATH;
}

COleInsertDialog::~COleInsertDialog() { FreeMetafilePict(m_io.hMetaPict); }

INT_PTR COleInsertDialog::DoModal() {
  FreeMetafilePict(m_io.hMetaPict);
  m_io.hInstance = NULL;
  UINT nResult =
      RunOleDialog(m_pParentWnd, &m_io.hWndOwner, [this]() { return ::OleUIInsertObject(&m_io); });
  return MapResult(nResult);
}

// DocObjectsOnly and ControlsOnly narrow the list on systems that know
// those kinds of object; oledlg shows the insertable classes.
INT_PTR COleInsertDialog::DoModal(DWORD dwFlags) {
  if (dwFlags & ControlsOnly) m_io.dwFlags |= IOF_SHOWINSERTCONTROL;
  return DoModal();
}

UINT COleInsertDialog::GetSelectionType() const {
  if (m_io.dwFlags & IOF_SELECTCREATEFROMFILE)
    return (m_io.dwFlags & IOF_CHECKLINK) ? linkToFile : insertFromFile;
  return createNewItem;
}

BOOL COleInsertDialog::CreateItem(COleClientItem* pItem) {
  BOOL bResult;
  switch (GetSelectionType()) {
  case linkToFile:
    bResult = pItem->CreateLinkFromFile(m_szFileName);
    break;
  case insertFromFile:
    bResult = pItem->CreateFromFile(m_szFileName);
    break;
  default:
    bResult = pItem->CreateNewItem(m_io.clsid);
    break;
  }
  if (bResult && GetDrawAspect() == DVASPECT_ICON && m_io.hMetaPict != NULL) {
    pItem->SetIconicMetafile(m_io.hMetaPict);
    pItem->SetDrawAspect(DVASPECT_ICON);
  }
  return bResult;
}

// ---------------------------------------------------------------------
// Paste Special

static CLIPFORMAT Format(LPCTSTR lpszName) {
  return (CLIPFORMAT)::RegisterClipboardFormat(lpszName);
}

COlePasteSpecialDialog::COlePasteSpecialDialog(DWORD dwFlags, COleDataObject* pDataObject,
                                               CWnd* pParentWnd)
    : COleDialog(pParentWnd) {
  memset(&m_ps, 0, sizeof(m_ps));
  m_ps.cbStruct = sizeof(m_ps);
  m_ps.dwFlags = dwFlags;
  memset(m_arrLinkTypes, 0, sizeof(m_arrLinkTypes));
  if (pDataObject != NULL && pDataObject->m_lpDataObject != NULL) {
    pDataObject->m_lpDataObject->AddRef();
    m_dataObject.Attach(pDataObject->m_lpDataObject, TRUE);
  } else {
    m_dataObject.AttachClipboard();
  }
  m_ps.lpSrcDataObj = m_dataObject.m_lpDataObject;
}

COlePasteSpecialDialog::~COlePasteSpecialDialog() { FreeMetafilePict(m_ps.hMetaPict); }

void COlePasteSpecialDialog::AddFormat(const FORMATETC& formatEtc, LPTSTR lpszFormat,
                                       LPTSTR lpszResult, DWORD flags) {
  OLEUIPASTEENTRY entry;
  memset(&entry, 0, sizeof(entry));
  entry.fmtetc = formatEtc;
  entry.dwFlags = flags;
  // The text is kept here; DoModal points the entries at it.
  m_arrStrings.Add(lpszFormat);
  m_arrStrings.Add(lpszResult);
  m_arrEntries.Add(entry);
}

void COlePasteSpecialDialog::AddFormat(UINT cf, DWORD tymed, UINT nFormatID, BOOL bEnableIcon,
                                       BOOL bLink) {
  CString str;
  AfxLoadString(nFormatID, str);
  CString strFormat = str, strResult = str;
  int i = str.Find(_T('\n'));
  if (i >= 0) {
    strFormat = str.Left(i);
    strResult = str.Mid(i + 1);
  }
  FORMATETC fe = {(CLIPFORMAT)cf, NULL, DVASPECT_CONTENT, -1, tymed};
  DWORD flags;
  if (bLink) {
    flags = (DWORD)(OLEUIPASTE_LINKTYPE1 << AddLinkEntry(cf));
  } else {
    flags = (cf == CF_METAFILEPICT || cf == CF_DIB || cf == CF_BITMAP || cf == CF_ENHMETAFILE)
                ? OLEUIPASTE_PASTEONLY
                : OLEUIPASTE_PASTE;
  }
  if (bEnableIcon) flags |= OLEUIPASTE_ENABLEICON;
  AddFormat(fe, strFormat.GetBuffer(), strResult.GetBuffer(), flags);
}

int COlePasteSpecialDialog::AddLinkEntry(UINT cf) {
  int i;
  for (i = 0; i < m_ps.cLinkTypes; i++)
    if (m_arrLinkTypes[i] == cf) return i;
  if (i == 8) return 7;
  m_arrLinkTypes[i] = cf;
  m_ps.cLinkTypes = i + 1;
  return i;
}

void COlePasteSpecialDialog::AddStandardFormats(BOOL bEnableLink) {
  AddFormat(Format(_T("Embed Source")), TYMED_ISTORAGE, AFX_IDS_EMBED_FORMAT, TRUE, FALSE);
  AddFormat(Format(_T("Embedded Object")), TYMED_ISTORAGE, AFX_IDS_EMBED_FORMAT, TRUE, FALSE);
  if (bEnableLink)
    AddFormat(Format(_T("Link Source")), TYMED_ISTREAM, AFX_IDS_LINKSOURCE_FORMAT, TRUE, TRUE);
  AddFormat(CF_METAFILEPICT, TYMED_MFPICT, AFX_IDS_METAFILE_FORMAT, FALSE, FALSE);
  AddFormat(CF_DIB, TYMED_HGLOBAL, AFX_IDS_DIB_FORMAT, FALSE, FALSE);
  AddFormat(CF_BITMAP, TYMED_GDI, AFX_IDS_BITMAP_FORMAT, FALSE, FALSE);
}

INT_PTR COlePasteSpecialDialog::DoModal() {
  FreeMetafilePict(m_ps.hMetaPict);
  for (int i = 0; i < m_arrEntries.GetSize(); i++) {
    m_arrEntries[i].lpstrFormatName = m_arrStrings[i * 2];
    m_arrEntries[i].lpstrResultText = m_arrStrings[i * 2 + 1];
  }
  m_ps.arrPasteEntries = m_arrEntries.GetSize() ? m_arrEntries.GetData() : NULL;
  m_ps.cPasteEntries = (int)m_arrEntries.GetSize();
  m_ps.arrLinkTypes = m_arrLinkTypes;
  m_ps.lpSrcDataObj = m_dataObject.m_lpDataObject;
  UINT nResult = RunOleDialog(m_pParentWnd, &m_ps.hWndOwner,
                              [this]() { return ::OleUIPasteSpecial(&m_ps); });
  return MapResult(nResult);
}

UINT COlePasteSpecialDialog::GetSelectionType() const {
  if (m_ps.fLink) return pasteLink;
  int i = m_ps.nSelectedIndex;
  if (i < 0 || i >= m_arrEntries.GetSize()) return pasteOther;
  CLIPFORMAT cf = m_arrEntries[i].fmtetc.cfFormat;
  if (cf == Format(_T("Embed Source")) || cf == Format(_T("Embedded Object"))) return pasteNormal;
  if (cf == CF_METAFILEPICT || cf == CF_DIB || cf == CF_BITMAP || cf == CF_ENHMETAFILE)
    return pasteStatic;
  return pasteOther;
}

BOOL COlePasteSpecialDialog::CreateItem(COleClientItem* pNewItem) {
  COleDataObject data;
  if (m_ps.lpSrcDataObj == NULL) return FALSE;
  m_ps.lpSrcDataObj->AddRef();
  data.Attach(m_ps.lpSrcDataObj, TRUE);
  int i = m_ps.nSelectedIndex;
  CLIPFORMAT cf = (i >= 0 && i < m_arrEntries.GetSize()) ? m_arrEntries[i].fmtetc.cfFormat : 0;
  BOOL bResult;
  switch (GetSelectionType()) {
  case pasteLink:
    bResult = pNewItem->CreateLinkFromData(&data);
    break;
  case pasteNormal:
    bResult = pNewItem->CreateFromData(&data);
    break;
  case pasteStatic:
    bResult = pNewItem->CreateStaticFromData(&data, OLERENDER_FORMAT, cf);
    break;
  default:
    bResult = pNewItem->CreateFromData(&data, OLERENDER_FORMAT, cf);
    break;
  }
  if (bResult && GetDrawAspect() == DVASPECT_ICON && m_ps.hMetaPict != NULL) {
    pNewItem->SetIconicMetafile(m_ps.hMetaPict);
    pNewItem->SetDrawAspect(DVASPECT_ICON);
  }
  return bResult;
}

// ---------------------------------------------------------------------
// Links

BEGIN_INTERFACE_MAP(COleLinksDialog, COleDialog)
  INTERFACE_PART(COleLinksDialog, IID_IUnknown, OleUILinkContainer)
END_INTERFACE_MAP()

COleLinksDialog::COleLinksDialog(COleDocument* pDoc, CView* pView, DWORD dwFlags,
                                 CWnd* pParentWnd)
    : COleDialog(pParentWnd), m_pDocument(pDoc), m_pView(pView) {
  memset(&m_el, 0, sizeof(m_el));
  m_el.cbStruct = sizeof(m_el);
  m_el.dwFlags = dwFlags;
  m_el.lpOleUILinkContainer = &m_xOleUILinkContainer;
}

COleLinksDialog::~COleLinksDialog() {}

COleClientItem* COleLinksDialog::GetLink(DWORD dwLink) const {
  if (m_pDocument == NULL || dwLink == 0) return NULL;
  POSITION pos = m_pDocument->GetStartPosition();
  while (pos != NULL) {
    COleClientItem* pItem = m_pDocument->GetNextClientItem(pos);
    if (pItem != NULL && pItem->GetType() == OT_LINK && --dwLink == 0) return pItem;
  }
  return NULL;
}

INT_PTR COleLinksDialog::DoModal() {
  UINT nResult =
      RunOleDialog(m_pParentWnd, &m_el.hWndOwner, [this]() { return ::OleUIEditLinks(&m_el); });
  return MapResult(nResult);
}

STDMETHODIMP_(ULONG) COleLinksDialog::XOleUILinkContainer::AddRef() {
  METHOD_PROLOGUE_EX_(COleLinksDialog, OleUILinkContainer)
  return pThis->ExternalAddRef();
}

STDMETHODIMP_(ULONG) COleLinksDialog::XOleUILinkContainer::Release() {
  METHOD_PROLOGUE_EX_(COleLinksDialog, OleUILinkContainer)
  return pThis->ExternalRelease();
}

STDMETHODIMP COleLinksDialog::XOleUILinkContainer::QueryInterface(REFIID iid, LPVOID* ppvObj) {
  METHOD_PROLOGUE_EX_(COleLinksDialog, OleUILinkContainer)
  return pThis->ExternalQueryInterface(&iid, ppvObj);
}

STDMETHODIMP_(DWORD) COleLinksDialog::XOleUILinkContainer::GetNextLink(DWORD dwLink) {
  METHOD_PROLOGUE_EX_(COleLinksDialog, OleUILinkContainer)
  return pThis->GetLink(dwLink + 1) != NULL ? dwLink + 1 : 0;
}

STDMETHODIMP COleLinksDialog::XOleUILinkContainer::SetLinkUpdateOptions(DWORD dwLink,
                                                                        DWORD dwUpdateOpt) {
  METHOD_PROLOGUE_EX_(COleLinksDialog, OleUILinkContainer)
  COleClientItem* pItem = pThis->GetLink(dwLink);
  if (pItem == NULL) return E_INVALIDARG;
  pItem->SetLinkUpdateOptions((OLEUPDATE)dwUpdateOpt);
  if (pThis->m_pDocument != NULL) pThis->m_pDocument->SetModifiedFlag();
  return pItem->GetLastStatus();
}

STDMETHODIMP COleLinksDialog::XOleUILinkContainer::GetLinkUpdateOptions(DWORD dwLink,
                                                                        DWORD* lpdwUpdateOpt) {
  METHOD_PROLOGUE_EX_(COleLinksDialog, OleUILinkContainer)
  COleClientItem* pItem = pThis->GetLink(dwLink);
  if (pItem == NULL) return E_INVALIDARG;
  *lpdwUpdateOpt = (DWORD)pItem->GetLinkUpdateOptions();
  return pItem->GetLastStatus();
}

STDMETHODIMP COleLinksDialog::XOleUILinkContainer::SetLinkSource(DWORD dwLink,
                                                                 LPTSTR lpszDisplayName, ULONG,
                                                                 ULONG* pchEaten, BOOL) {
  METHOD_PROLOGUE_EX_(COleLinksDialog, OleUILinkContainer)
  if (pchEaten != NULL) *pchEaten = 0;
  COleClientItem* pItem = pThis->GetLink(dwLink);
  if (pItem == NULL || pItem->m_lpObject == NULL) return E_INVALIDARG;
  LPOLELINK lpLink = NULL;
  HRESULT hr = pItem->m_lpObject->QueryInterface(IID_IOleLink, (void**)&lpLink);
  if (FAILED(hr)) return hr;
  CStringW strName(lpszDisplayName);
  hr = lpLink->SetSourceDisplayName(strName);
  lpLink->Release();
  if (SUCCEEDED(hr)) {
    if (pchEaten != NULL) *pchEaten = (ULONG)lstrlen(lpszDisplayName);
    if (pThis->m_pDocument != NULL) pThis->m_pDocument->SetModifiedFlag();
  }
  return hr;
}

STDMETHODIMP COleLinksDialog::XOleUILinkContainer::GetLinkSource(
    DWORD dwLink, LPTSTR* lplpszDisplayName, ULONG* lplenFileName, LPTSTR* lplpszFullLinkType,
    LPTSTR* lplpszShortLinkType, BOOL* lpfSourceAvailable, BOOL* lpfIsSelected) {
  METHOD_PROLOGUE_EX_(COleLinksDialog, OleUILinkContainer)
  if (lplpszDisplayName != NULL) *lplpszDisplayName = NULL;
  if (lplpszFullLinkType != NULL) *lplpszFullLinkType = NULL;
  if (lplpszShortLinkType != NULL) *lplpszShortLinkType = NULL;
  COleClientItem* pItem = pThis->GetLink(dwLink);
  if (pItem == NULL || pItem->m_lpObject == NULL) return E_INVALIDARG;
  LPOLELINK lpLink = NULL;
  HRESULT hr = pItem->m_lpObject->QueryInterface(IID_IOleLink, (void**)&lpLink);
  if (FAILED(hr)) return hr;
  LPOLESTR pszName = NULL;
  hr = lpLink->GetSourceDisplayName(&pszName);
  lpLink->Release();
  if (FAILED(hr)) return hr;
  CString strName(pszName);
  ::CoTaskMemFree(pszName);
  // The file part ends where an item name starts.
  if (lplenFileName != NULL) {
    int i = strName.Find(_T('!'));
    *lplenFileName = (ULONG)(i >= 0 ? i : strName.GetLength());
  }
  if (lplpszDisplayName != NULL) *lplpszDisplayName = TaskString(strName);
  CString strType;
  if (lplpszFullLinkType != NULL) {
    pItem->GetUserType(USERCLASSTYPE_FULL, strType);
    *lplpszFullLinkType = TaskString(strType);
  }
  if (lplpszShortLinkType != NULL) {
    pItem->GetUserType(USERCLASSTYPE_SHORT, strType);
    *lplpszShortLinkType = TaskString(strType);
  }
  if (lpfSourceAvailable != NULL) *lpfSourceAvailable = TRUE;
  if (lpfIsSelected != NULL)
    *lpfIsSelected = pThis->m_pView != NULL && pThis->m_pView->IsSelected(pItem);
  return S_OK;
}

STDMETHODIMP COleLinksDialog::XOleUILinkContainer::OpenLinkSource(DWORD dwLink) {
  METHOD_PROLOGUE_EX_(COleLinksDialog, OleUILinkContainer)
  COleClientItem* pItem = pThis->GetLink(dwLink);
  if (pItem == NULL) return E_INVALIDARG;
  return pItem->DoVerb(OLEIVERB_SHOW, pThis->m_pView) ? S_OK : pItem->GetLastStatus();
}

STDMETHODIMP COleLinksDialog::XOleUILinkContainer::UpdateLink(DWORD dwLink, BOOL fErrorMessage,
                                                              BOOL) {
  METHOD_PROLOGUE_EX_(COleLinksDialog, OleUILinkContainer)
  COleClientItem* pItem = pThis->GetLink(dwLink);
  if (pItem == NULL) return E_INVALIDARG;
  if (pItem->UpdateLink()) return S_OK;
  SCODE sc = pItem->GetLastStatus();
  if (fErrorMessage) pItem->ReportError(sc);
  return sc;
}

// Breaking a link leaves a picture of what it showed.
STDMETHODIMP COleLinksDialog::XOleUILinkContainer::CancelLink(DWORD dwLink) {
  METHOD_PROLOGUE_EX_(COleLinksDialog, OleUILinkContainer)
  COleClientItem* pItem = pThis->GetLink(dwLink);
  if (pItem == NULL || pItem->m_lpObject == NULL) return E_INVALIDARG;
  LPDATAOBJECT lpData = NULL;
  HRESULT hr = pItem->m_lpObject->QueryInterface(IID_IDataObject, (void**)&lpData);
  if (FAILED(hr)) return hr;
  COleDataObject data;
  data.Attach(lpData, TRUE);
  if (!pItem->CreateStaticFromData(&data)) return pItem->GetLastStatus();
  if (pThis->m_pDocument != NULL) pThis->m_pDocument->SetModifiedFlag();
  return S_OK;
}

// ---------------------------------------------------------------------
// Update Links

COleUpdateDialog::COleUpdateDialog(COleDocument* pDoc, BOOL, BOOL, CWnd* pParentWnd)
    : COleLinksDialog(pDoc, NULL, 0, pParentWnd) {
  AfxLoadString(AFX_IDS_UPDATING_ITEMS, m_strCaption);
}

INT_PTR COleUpdateDialog::DoModal() {
  int nLinks = 0;
  while (GetLink(nLinks + 1) != NULL) nLinks++;
  if (nLinks == 0) return MapResult(OLEUI_OK);
  HWND hWndOwner = NULL;
  UINT nResult = RunOleDialog(m_pParentWnd, &hWndOwner, [&]() -> UINT {
    return ::OleUIUpdateLinks(&m_xOleUILinkContainer, hWndOwner, m_strCaption.GetBuffer(),
                              nLinks)
               ? OLEUI_OK
               : OLEUI_FALSE;
  });
  return MapResult(nResult);
}

// ---------------------------------------------------------------------
// Change Icon

COleChangeIconDialog::COleChangeIconDialog(COleClientItem* pItem, DWORD dwFlags,
                                           CWnd* pParentWnd)
    : COleDialog(pParentWnd) {
  memset(&m_ci, 0, sizeof(m_ci));
  m_ci.cbStruct = sizeof(m_ci);
  m_ci.dwFlags = dwFlags;
  if (pItem != NULL) {
    pItem->GetClassID(&m_ci.clsid);
    m_ci.hMetaPict = pItem->GetIconicMetafile();
  }
}

COleChangeIconDialog::~COleChangeIconDialog() { FreeMetafilePict(m_ci.hMetaPict); }

INT_PTR COleChangeIconDialog::DoModal() {
  UINT nResult =
      RunOleDialog(m_pParentWnd, &m_ci.hWndOwner, [this]() { return ::OleUIChangeIcon(&m_ci); });
  return MapResult(nResult);
}

BOOL COleChangeIconDialog::DoChangeIcon(COleClientItem* pItem) {
  if (m_ci.hMetaPict == NULL || !pItem->SetIconicMetafile(m_ci.hMetaPict)) return FALSE;
  pItem->SetDrawAspect(DVASPECT_ICON);
  return TRUE;
}

// ---------------------------------------------------------------------
// Convert

COleConvertDialog::COleConvertDialog(COleClientItem* pItem, DWORD dwFlags, CLSID* pClassID,
                                     CWnd* pParentWnd)
    : COleDialog(pParentWnd) {
  memset(&m_cv, 0, sizeof(m_cv));
  m_cv.cbStruct = sizeof(m_cv);
  m_cv.dwFlags = dwFlags;
  if (pClassID != NULL)
    m_cv.clsid = *pClassID;
  else if (pItem != NULL)
    pItem->GetClassID(&m_cv.clsid);
  m_cv.dvAspect = pItem != NULL ? pItem->GetDrawAspect() : DVASPECT_CONTENT;
  if (pItem != NULL) {
    m_cv.fIsLinkedObject = pItem->GetType() == OT_LINK;
    pItem->GetUserType(USERCLASSTYPE_FULL, m_strUserType);
    if (m_cv.dvAspect == DVASPECT_ICON) m_cv.hMetaPict = pItem->GetIconicMetafile();
    // The format the object's data is stored in.
    if (pItem->m_lpStorage != NULL) {
      CLIPFORMAT cf = 0;
      if (SUCCEEDED(::ReadFmtUserTypeStg(pItem->m_lpStorage, &cf, NULL))) m_cv.wFormat = cf;
    }
  }
}

COleConvertDialog::~COleConvertDialog() { FreeMetafilePict(m_cv.hMetaPict); }

INT_PTR COleConvertDialog::DoModal() {
  m_cv.lpszUserType = m_strUserType.IsEmpty() ? NULL : m_strUserType.GetBuffer();
  UINT nResult =
      RunOleDialog(m_pParentWnd, &m_cv.hWndOwner, [this]() { return ::OleUIConvert(&m_cv); });
  return MapResult(nResult);
}

UINT COleConvertDialog::GetSelectionType() const {
  if (IsEqualCLSID(m_cv.clsid, m_cv.clsidNew) || IsEqualCLSID(m_cv.clsidNew, CLSID_NULL))
    return noConversion;
  return (m_cv.dwFlags & CF_SELECTACTIVATEAS) ? activateAs : convertItem;
}

BOOL COleConvertDialog::DoConvert(COleClientItem* pItem) {
  BOOL bResult = TRUE;
  switch (GetSelectionType()) {
  case convertItem:
    bResult = pItem->ConvertTo(m_cv.clsidNew);
    break;
  case activateAs:
    bResult = pItem->ActivateAs(m_strUserType, m_cv.clsid, m_cv.clsidNew);
    break;
  default:
    break;
  }
  if (!bResult) return FALSE;
  // The dialog can also switch between the icon and the content.
  if ((DVASPECT)m_cv.dvAspect == DVASPECT_ICON && m_cv.hMetaPict != NULL)
    pItem->SetIconicMetafile(m_cv.hMetaPict);
  pItem->SetDrawAspect((DVASPECT)m_cv.dvAspect);
  return TRUE;
}

// ---------------------------------------------------------------------
// Server Busy

COleBusyDialog::COleBusyDialog(HTASK htaskBusy, BOOL bNotResponding, DWORD dwFlags,
                               CWnd* pParentWnd)
    : COleDialog(pParentWnd), m_selection(retry) {
  memset(&m_bz, 0, sizeof(m_bz));
  m_bz.cbStruct = sizeof(m_bz);
  m_bz.dwFlags = dwFlags | (bNotResponding ? BZ_NOTRESPONDINGDIALOG : 0);
  m_bz.hTask = htaskBusy;
}

COleBusyDialog::~COleBusyDialog() {}

INT_PTR COleBusyDialog::DoModal() {
  UINT nResult =
      RunOleDialog(m_pParentWnd, &m_bz.hWndOwner, [this]() { return ::OleUIBusy(&m_bz); });
  m_nLastError = nResult;
  switch (nResult) {
  case OLEUI_BZ_SWITCHTOSELECTED:
    m_selection = switchTo;
    return IDOK;
  case OLEUI_BZ_RETRYSELECTED:
    m_selection = retry;
    return IDOK;
  case OLEUI_BZ_CALLUNBLOCKED:
    m_selection = callUnblocked;
    return IDOK;
  default:
    return MapResult(nResult);
  }
}

// ---------------------------------------------------------------------
// COleDocument's dialog commands

static CView* RoutingView(COleDocument* pDoc) {
  CView* pView = AfxGetThreadState()->m_pRoutingView;
  return (pView != NULL && pView->GetDocument() == pDoc) ? pView : NULL;
}

void COleDocument::OnUpdateEditChangeIcon(CCmdUI* pCmdUI) {
  CView* pView = RoutingView(this);
  pCmdUI->Enable(pView != NULL && GetPrimarySelectedItem(pView) != NULL);
}

void COleDocument::OnEditLinks() {
  COleLinksDialog dlg(this, RoutingView(this));
  dlg.DoModal();
}

void COleDocument::OnEditConvert() {
  CView* pView = RoutingView(this);
  COleClientItem* pItem = pView != NULL ? GetPrimarySelectedItem(pView) : NULL;
  if (pItem == NULL) return;
  COleConvertDialog dlg(pItem);
  if (dlg.DoModal() != IDOK) return;
  if (!dlg.DoConvert(pItem)) {
    CString str;
    AfxLoadString(AFX_IDP_FAILED_TO_CONVERT, str);
    AfxMessageBox(str, MB_OK | MB_ICONEXCLAMATION);
    return;
  }
  SetModifiedFlag();
  UpdateAllViews(NULL);
}

void COleDocument::OnEditChangeIcon() {
  CView* pView = RoutingView(this);
  COleClientItem* pItem = pView != NULL ? GetPrimarySelectedItem(pView) : NULL;
  if (pItem == NULL) return;
  COleChangeIconDialog dlg(pItem);
  if (dlg.DoModal() != IDOK) return;
  if (dlg.DoChangeIcon(pItem)) {
    SetModifiedFlag();
    UpdateAllViews(NULL);
  }
}

// ---------------------------------------------------------------------
// The links' last update: when their source last changed.

DWORD COleLinksDialog::FindLink(COleClientItem* pItem) const {
  for (DWORD dwLink = 1;; dwLink++) {
    COleClientItem* p = GetLink(dwLink);
    if (p == NULL) return 0;
    if (p == pItem) return dwLink;
  }
}

STDMETHODIMP COleLinksDialog::XOleUILinkContainer::GetLastUpdate(DWORD dwLink,
                                                                 FILETIME* lpLastUpdate) {
  METHOD_PROLOGUE_EX_(COleLinksDialog, OleUILinkContainer)
  if (lpLastUpdate == NULL) return E_POINTER;
  memset(lpLastUpdate, 0, sizeof(*lpLastUpdate));
  COleClientItem* pItem = pThis->GetLink(dwLink);
  if (pItem == NULL || pItem->m_lpObject == NULL) return E_INVALIDARG;
  LPOLELINK lpLink = NULL;
  HRESULT hr = pItem->m_lpObject->QueryInterface(IID_IOleLink, (void**)&lpLink);
  if (FAILED(hr)) return hr;
  LPMONIKER lpMoniker = NULL;
  hr = lpLink->GetSourceMoniker(&lpMoniker);
  lpLink->Release();
  if (FAILED(hr) || lpMoniker == NULL) return FAILED(hr) ? hr : E_FAIL;
  LPBC lpbc = NULL;
  hr = ::CreateBindCtx(0, &lpbc);
  if (SUCCEEDED(hr)) {
    hr = lpMoniker->GetTimeOfLastChange(lpbc, NULL, lpLastUpdate);
    lpbc->Release();
  }
  lpMoniker->Release();
  return hr;
}

// ---------------------------------------------------------------------
// Change Source

static COleDocument* ItemDocument(COleClientItem* pItem) {
  return pItem != NULL ? pItem->GetDocument() : NULL;
}

COleChangeSourceDialog::COleChangeSourceDialog(COleClientItem* pItem, CWnd* pParentWnd)
    : COleDialog(pParentWnd), m_xLinkInfo(ItemDocument(pItem), NULL), m_pItem(pItem) {
  memset(&m_cs, 0, sizeof(m_cs));
  m_cs.cbStruct = sizeof(m_cs);
  m_cs.lpOleUILinkContainer = &m_xLinkInfo.m_xOleUILinkContainer;
  m_cs.dwLink = m_xLinkInfo.FindLink(pItem);
  // The link's source now.
  if (m_cs.dwLink != 0) {
    LPTSTR lpszName = NULL;
    ULONG nFileLength = 0;
    if (SUCCEEDED(m_xLinkInfo.m_xOleUILinkContainer.GetLinkSource(
            m_cs.dwLink, &lpszName, &nFileLength, NULL, NULL, NULL, NULL))) {
      m_cs.lpszDisplayName = lpszName;
      m_cs.nFileLength = nFileLength;
    }
  }
}

COleChangeSourceDialog::~COleChangeSourceDialog() {
  ::CoTaskMemFree(m_cs.lpszDisplayName);
  ::CoTaskMemFree(m_cs.lpszFrom);
  ::CoTaskMemFree(m_cs.lpszTo);
}

INT_PTR COleChangeSourceDialog::DoModal() {
  UINT nResult = RunOleDialog(m_pParentWnd, &m_cs.hWndOwner,
                              [this]() { return ::OleUIChangeSource(&m_cs); });
  INT_PTR nRet = MapResult(nResult);
  if (nRet == IDOK && IsValidSource() && m_cs.dwLink != 0 && m_cs.lpszDisplayName != NULL) {
    ULONG nEaten = 0;
    m_xLinkInfo.m_xOleUILinkContainer.SetLinkSource(m_cs.dwLink, m_cs.lpszDisplayName,
                                                    m_cs.nFileLength, &nEaten, FALSE);
  }
  return nRet;
}

// ---------------------------------------------------------------------
// Object Properties: one object (dwObject 1), the item.

BEGIN_INTERFACE_MAP(COlePropertiesDialog, COleDialog)
END_INTERFACE_MAP()

COlePropertiesDialog::COlePropertiesDialog(COleClientItem* pItem, UINT nScaleMin,
                                           UINT nScaleMax, CWnd* pParentWnd)
    : COleDialog(pParentWnd), m_xLinkInfo(ItemDocument(pItem), NULL), m_pItem(pItem) {
  memset(&m_op, 0, sizeof(m_op));
  memset(&m_gp, 0, sizeof(m_gp));
  memset(&m_vp, 0, sizeof(m_vp));
  memset(&m_lp, 0, sizeof(m_lp));
  memset(&m_psh, 0, sizeof(m_psh));
  m_op.cbStruct = sizeof(m_op);
  m_gp.cbStruct = sizeof(m_gp);
  m_vp.cbStruct = sizeof(m_vp);
  m_lp.cbStruct = sizeof(m_lp);
  m_psh.dwSize = sizeof(m_psh);
  m_vp.nScaleMin = (int)nScaleMin;
  m_vp.nScaleMax = (int)nScaleMax;
  m_op.lpPS = &m_psh;
  m_op.dwObject = 1;
  m_op.lpObjInfo = &m_xOleUIObjInfo;
  m_op.lpGP = &m_gp;
  m_op.lpVP = &m_vp;
  m_gp.lpOP = m_vp.lpOP = m_lp.lpOP = &m_op;
  if (pItem != NULL && pItem->GetType() == OT_LINK) {
    m_op.dwFlags |= OPF_OBJECTISLINK;
    m_op.dwLink = m_xLinkInfo.FindLink(pItem);
    m_op.lpLinkInfo = &m_xLinkInfo.m_xOleUILinkContainer;
    m_op.lpLP = &m_lp;
  }
}

COlePropertiesDialog::~COlePropertiesDialog() {}

INT_PTR COlePropertiesDialog::DoModal() {
  UINT nResult = RunOleDialog(m_pParentWnd, &m_psh.hwndParent,
                              [this]() { return ::OleUIObjectProperties(&m_op); });
  return MapResult(nResult);
}

BOOL COlePropertiesDialog::OnApplyScale(COleClientItem*, int, BOOL) { return FALSE; }

STDMETHODIMP_(ULONG) COlePropertiesDialog::XOleUIObjInfo::AddRef() {
  METHOD_PROLOGUE_EX_(COlePropertiesDialog, OleUIObjInfo)
  return pThis->ExternalAddRef();
}

STDMETHODIMP_(ULONG) COlePropertiesDialog::XOleUIObjInfo::Release() {
  METHOD_PROLOGUE_EX_(COlePropertiesDialog, OleUIObjInfo)
  return pThis->ExternalRelease();
}

STDMETHODIMP COlePropertiesDialog::XOleUIObjInfo::QueryInterface(REFIID iid, LPVOID* ppvObj) {
  METHOD_PROLOGUE_EX_(COlePropertiesDialog, OleUIObjInfo)
  return pThis->ExternalQueryInterface(&iid, ppvObj);
}

// The object's size is its storage's, when that is in memory. Its label
// and type are its full type name, its location the document's (or, for
// a link, the source's).
STDMETHODIMP COlePropertiesDialog::XOleUIObjInfo::GetObjectInfo(
    DWORD dwObject, DWORD* lpdwObjSize, LPTSTR* lplpszLabel, LPTSTR* lplpszType,
    LPTSTR* lplpszShortType, LPTSTR* lplpszLocation) {
  METHOD_PROLOGUE_EX_(COlePropertiesDialog, OleUIObjInfo)
  if (lplpszLabel != NULL) *lplpszLabel = NULL;
  if (lplpszType != NULL) *lplpszType = NULL;
  if (lplpszShortType != NULL) *lplpszShortType = NULL;
  if (lplpszLocation != NULL) *lplpszLocation = NULL;
  COleClientItem* pItem = pThis->m_pItem;
  if (dwObject != 1 || pItem == NULL) return E_INVALIDARG;
  if (lpdwObjSize != NULL) {
    *lpdwObjSize = (DWORD)-1;  // unknown
    STATSTG stat;
    if (pItem->m_lpLockBytes != NULL &&
        SUCCEEDED(pItem->m_lpLockBytes->Stat(&stat, STATFLAG_NONAME)))
      *lpdwObjSize = stat.cbSize.HighPart != 0 ? (DWORD)-2 : stat.cbSize.LowPart;
  }
  CString strFull, strShort;
  pItem->GetUserType(USERCLASSTYPE_FULL, strFull);
  pItem->GetUserType(USERCLASSTYPE_SHORT, strShort);
  if (lplpszLabel != NULL) *lplpszLabel = TaskString(strFull);
  if (lplpszType != NULL) *lplpszType = TaskString(strFull);
  if (lplpszShortType != NULL) *lplpszShortType = TaskString(strShort);
  if (lplpszLocation != NULL) {
    CString strLocation;
    DWORD dwLink = pThis->m_op.dwLink;
    LPTSTR lpszSource = NULL;
    if (dwLink != 0 && SUCCEEDED(pThis->m_xLinkInfo.m_xOleUILinkContainer.GetLinkSource(
                           dwLink, &lpszSource, NULL, NULL, NULL, NULL, NULL))) {
      strLocation = lpszSource;
      ::CoTaskMemFree(lpszSource);
    } else if (COleDocument* pDoc = pItem->GetDocument()) {
      strLocation = pDoc->GetPathName();
      if (strLocation.IsEmpty()) strLocation = pDoc->GetTitle();
    }
    *lplpszLocation = TaskString(strLocation);
  }
  return S_OK;
}

STDMETHODIMP COlePropertiesDialog::XOleUIObjInfo::GetConvertInfo(
    DWORD dwObject, CLSID* lpClassID, WORD* lpwFormat, CLSID* lpConvertDefaultClassID,
    LPCLSID* lplpClsidExclude, UINT* lpcClsidExclude) {
  METHOD_PROLOGUE_EX_(COlePropertiesDialog, OleUIObjInfo)
  COleClientItem* pItem = pThis->m_pItem;
  if (dwObject != 1 || pItem == NULL) return E_INVALIDARG;
  if (lpClassID != NULL) pItem->GetClassID(lpClassID);
  if (lpwFormat != NULL) {
    *lpwFormat = 0;
    CLIPFORMAT cf = 0;
    if (pItem->m_lpStorage != NULL && SUCCEEDED(::ReadFmtUserTypeStg(pItem->m_lpStorage, &cf, NULL)))
      *lpwFormat = cf;
  }
  if (lpConvertDefaultClassID != NULL) *lpConvertDefaultClassID = CLSID_NULL;
  if (lplpClsidExclude != NULL) *lplpClsidExclude = NULL;
  if (lpcClsidExclude != NULL) *lpcClsidExclude = 0;
  return S_OK;
}

STDMETHODIMP COlePropertiesDialog::XOleUIObjInfo::ConvertObject(DWORD dwObject,
                                                                REFCLSID clsidNew) {
  METHOD_PROLOGUE_EX_(COlePropertiesDialog, OleUIObjInfo)
  COleClientItem* pItem = pThis->m_pItem;
  if (dwObject != 1 || pItem == NULL) return E_INVALIDARG;
  if (!pItem->ConvertTo(clsidNew)) return pItem->GetLastStatus();
  if (COleDocument* pDoc = pItem->GetDocument()) {
    pDoc->SetModifiedFlag();
    pDoc->UpdateAllViews(NULL);
  }
  return S_OK;
}

STDMETHODIMP COlePropertiesDialog::XOleUIObjInfo::GetViewInfo(DWORD dwObject,
                                                             HGLOBAL* phMetaPict,
                                                             DWORD* pdvAspect,
                                                             int* pnCurrentScale) {
  METHOD_PROLOGUE_EX_(COlePropertiesDialog, OleUIObjInfo)
  COleClientItem* pItem = pThis->m_pItem;
  if (dwObject != 1 || pItem == NULL) return E_INVALIDARG;
  if (phMetaPict != NULL) *phMetaPict = pItem->GetIconicMetafile();
  if (pdvAspect != NULL) *pdvAspect = (DWORD)pItem->GetDrawAspect();
  if (pnCurrentScale != NULL) *pnCurrentScale = 100;
  return S_OK;
}

STDMETHODIMP COlePropertiesDialog::XOleUIObjInfo::SetViewInfo(DWORD dwObject, HGLOBAL hMetaPict,
                                                             DWORD dvAspect, int nCurrentScale,
                                                             BOOL bRelativeToOrig) {
  METHOD_PROLOGUE_EX_(COlePropertiesDialog, OleUIObjInfo)
  COleClientItem* pItem = pThis->m_pItem;
  if (dwObject != 1 || pItem == NULL) return E_INVALIDARG;
  BOOL bChanged = FALSE;
  if ((DVASPECT)dvAspect == DVASPECT_ICON && hMetaPict != NULL) {
    pItem->SetIconicMetafile(hMetaPict);
    bChanged = TRUE;
  }
  if ((DVASPECT)dvAspect != pItem->GetDrawAspect()) {
    pItem->SetDrawAspect((DVASPECT)dvAspect);
    bChanged = TRUE;
  }
  if (pThis->OnApplyScale(pItem, nCurrentScale, bRelativeToOrig)) bChanged = TRUE;
  if (bChanged) {
    if (COleDocument* pDoc = pItem->GetDocument()) {
      pDoc->SetModifiedFlag();
      pDoc->UpdateAllViews(NULL);
    }
  }
  return S_OK;
}
