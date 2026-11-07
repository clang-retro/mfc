// olecli.cpp - COleClientItem: a linked, embedded or static object in a
// container document, and the client site it gives the object.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include "afximpl.h"
#include <afxole.h>
// oledlg.h needs <tchar.h>, which some C libraries lack; this is the one
// function used here.
extern "C" BOOL WINAPI OleUIAddVerbMenuW(LPOLEOBJECT lpOleObj, LPCWSTR lpszShortType,
                                         HMENU hMenu, UINT uPos, UINT uIDVerbMin,
                                         UINT uIDVerbMax, BOOL bAddConvert, UINT idConvert,
                                         HMENU* lphMenu);

IMPLEMENT_DYNAMIC(COleClientItem, CDocItem)

BEGIN_INTERFACE_MAP(COleClientItem, CDocItem)
  INTERFACE_PART(COleClientItem, IID_IOleClientSite, OleClientSite)
  INTERFACE_PART(COleClientItem, IID_IAdviseSink, AdviseSink)
  INTERFACE_PART(COleClientItem, IID_IOleWindow, OleIPSite)
  INTERFACE_PART(COleClientItem, IID_IOleInPlaceSite, OleIPSite)
END_INTERFACE_MAP()

void AFXAPI _AfxThrowOleFileException(SCODE sc);

static const CLSID s_clsidStaticMetafile = {
    0x00000315, 0, 0, {0xC0, 0, 0, 0, 0, 0, 0, 0x46}};
static const CLSID s_clsidStaticDib = {0x00000316, 0, 0, {0xC0, 0, 0, 0, 0, 0, 0, 0x46}};
static const CLSID s_clsidStaticEnhMetafile = {
    0x00000319, 0, 0, {0xC0, 0, 0, 0, 0, 0, 0, 0x46}};

static CLIPFORMAT RegisteredFormat(CLIPFORMAT& cf, LPCTSTR lpszName) {
  if (cf == 0) cf = (CLIPFORMAT)::RegisterClipboardFormat(lpszName);
  return cf;
}
static CLIPFORMAT s_cfEmbeddedObject, s_cfObjectDescriptor, s_cfLinkSource,
    s_cfLinkSrcDescriptor;
#define CF_EMBEDDEDOBJECT_ RegisteredFormat(s_cfEmbeddedObject, _T("Embedded Object"))
#define CF_OBJECTDESCRIPTOR_ RegisteredFormat(s_cfObjectDescriptor, _T("Object Descriptor"))
#define CF_LINKSOURCE_ RegisteredFormat(s_cfLinkSource, _T("Link Source"))
#define CF_LINKSRCDESCRIPTOR_ \
  RegisteredFormat(s_cfLinkSrcDescriptor, _T("Link Source Descriptor"))

// ---------------------------------------------------------------------
// Construction

COleClientItem::COleClientItem(COleDocument* pContainerDoc)
    : m_lpObject(NULL), m_lpViewObject(NULL), m_lpStorage(NULL), m_lpLockBytes(NULL),
      m_dwItemNumber(0), m_dwConnection(0), m_nDrawAspect(DVASPECT_CONTENT),
      m_nItemType(OT_UNKNOWN), m_nItemState(emptyState), m_scLast(S_OK), m_pView(NULL),
      m_lpInPlaceObject(NULL), m_pInPlaceFrame(NULL), m_pInPlaceDoc(NULL) {
  if (pContainerDoc != NULL) pContainerDoc->AddItem(this);
}

COleClientItem::~COleClientItem() {
  Release();
  if (m_pDocument != NULL) m_pDocument->RemoveItem(this);
}

void COleClientItem::GetItemName(LPOLESTR lpszName) const {
  wsprintfW(lpszName, L"Embedding %lu", m_dwItemNumber);
}

void COleClientItem::ReleaseStorage() {
  if (m_lpStorage != NULL) {
    m_lpStorage->Release();
    m_lpStorage = NULL;
  }
  if (m_lpLockBytes != NULL) {
    m_lpLockBytes->Release();
    m_lpLockBytes = NULL;
  }
}

// A storage for a new object: a substorage of a compound file, or one in
// memory that is written into the document's archive.
void COleClientItem::GetItemStorage() {
  COleDocument* pDoc = GetDocument();
  ReleaseStorage();
  if (m_dwItemNumber == 0) m_dwItemNumber = pDoc->GetNewItemNumber();
  SCODE sc;
  if (pDoc->m_bCompoundFile && pDoc->m_lpRootStg != NULL) {
    OLECHAR szName[32];
    GetItemName(szName);
    sc = pDoc->m_lpRootStg->CreateStorage(
        szName, STGM_CREATE | STGM_READWRITE | STGM_SHARE_EXCLUSIVE, 0, 0, &m_lpStorage);
  } else {
    sc = ::CreateILockBytesOnHGlobal(NULL, TRUE, &m_lpLockBytes);
    if (SUCCEEDED(sc))
      sc = ::StgCreateDocfileOnILockBytes(
          m_lpLockBytes, STGM_CREATE | STGM_READWRITE | STGM_SHARE_EXCLUSIVE, 0, &m_lpStorage);
  }
  if (FAILED(sc)) {
    m_lpStorage = NULL;
    ReleaseStorage();
    AfxThrowOleException(sc);
  }
}

static OLE_OBJTYPE ObjectType(LPOLEOBJECT lpObject) {
  LPOLELINK lpLink = NULL;
  if (SUCCEEDED(lpObject->QueryInterface(IID_IOleLink, (void**)&lpLink))) {
    lpLink->Release();
    return OT_LINK;
  }
  DWORD dwStatus = 0;
  if (SUCCEEDED(lpObject->GetMiscStatus(DVASPECT_CONTENT, &dwStatus)) &&
      (dwStatus & OLEMISC_STATIC))
    return OT_STATIC;
  CLSID clsid;
  if (SUCCEEDED(lpObject->GetUserClassID(&clsid)) &&
      (IsEqualCLSID(clsid, s_clsidStaticMetafile) || IsEqualCLSID(clsid, s_clsidStaticDib) ||
       IsEqualCLSID(clsid, s_clsidStaticEnhMetafile)))
    return OT_STATIC;
  return OT_EMBEDDED;
}

// Connects a new or loaded object to this item.
void COleClientItem::FinishLoad() {
  m_nItemType = ObjectType(m_lpObject);
  if (m_lpViewObject == NULL)
    m_lpObject->QueryInterface(IID_IViewObject2, (void**)&m_lpViewObject);
  if (m_lpViewObject != NULL) m_lpViewObject->SetAdvise(m_nDrawAspect, 0, &m_xAdviseSink);
  if (m_dwConnection == 0) m_lpObject->Advise(&m_xAdviseSink, &m_dwConnection);
  if (m_nItemType == OT_EMBEDDED) ::OleSetContainedObject(m_lpObject, TRUE);

  CString strHost = AfxGetAppName();
  CString strTitle;
  if (GetDocument() != NULL) strTitle = GetDocument()->GetTitle();
  SetHostNames(strHost, strTitle);
  m_nItemState = loadedState;
}

BOOL COleClientItem::FinishCreate(SCODE sc) {
  m_scLast = sc;
  if (FAILED(sc) || m_lpObject == NULL) {
    if (m_lpObject != NULL) {
      m_lpObject->Release();
      m_lpObject = NULL;
    }
    ReleaseStorage();
    if (GetDocument() != NULL && GetDocument()->m_bCompoundFile &&
        GetDocument()->m_lpRootStg != NULL && m_dwItemNumber != 0) {
      OLECHAR szName[32];
      GetItemName(szName);
      GetDocument()->m_lpRootStg->DestroyElement(szName);
    }
    m_nItemState = emptyState;
    return FALSE;
  }
  FinishLoad();
  return TRUE;
}

static LPFORMATETC MakeFormatEtc(FORMATETC& fe, CLIPFORMAT cfFormat, LPFORMATETC lpFormatEtc) {
  if (lpFormatEtc != NULL) {
    if (cfFormat != 0) lpFormatEtc->cfFormat = cfFormat;
    return lpFormatEtc;
  }
  if (cfFormat == 0) return NULL;
  fe.cfFormat = cfFormat;
  fe.ptd = NULL;
  fe.dwAspect = DVASPECT_CONTENT;
  fe.lindex = -1;
  fe.tymed = cfFormat == CF_METAFILEPICT    ? TYMED_MFPICT
             : cfFormat == CF_ENHMETAFILE   ? TYMED_ENHMF
             : cfFormat == CF_BITMAP        ? TYMED_GDI
                                            : TYMED_HGLOBAL;
  return &fe;
}

// ---------------------------------------------------------------------
// Creation

BOOL PASCAL COleClientItem::CanCreateFromData(const COleDataObject* pDataObject) {
  if (pDataObject == NULL || pDataObject->m_lpDataObject == NULL) return FALSE;
  SCODE sc = ::OleQueryCreateFromData(pDataObject->m_lpDataObject);
  return sc == S_OK || sc == OLE_S_STATIC;
}

BOOL PASCAL COleClientItem::CanCreateLinkFromData(const COleDataObject* pDataObject) {
  if (pDataObject == NULL || pDataObject->m_lpDataObject == NULL) return FALSE;
  if (::OleQueryLinkFromData(pDataObject->m_lpDataObject) == S_OK) return TRUE;
  // What OleQueryLinkFromData looks for (Wine's always says no).
  return const_cast<COleDataObject*>(pDataObject)->IsDataAvailable(CF_LINKSOURCE_);
}

BOOL PASCAL COleClientItem::CanPaste() {
  COleDataObject data;
  return data.AttachClipboard() && CanCreateFromData(&data);
}

BOOL PASCAL COleClientItem::CanPasteLink() {
  COleDataObject data;
  return data.AttachClipboard() && CanCreateLinkFromData(&data);
}

BOOL COleClientItem::CreateFromClipboard(OLERENDER render, CLIPFORMAT cfFormat,
                                         LPFORMATETC lpFormatEtc) {
  COleDataObject data;
  if (!data.AttachClipboard()) return FALSE;
  return CreateFromData(&data, render, cfFormat, lpFormatEtc);
}

BOOL COleClientItem::CreateLinkFromClipboard(OLERENDER render, CLIPFORMAT cfFormat,
                                             LPFORMATETC lpFormatEtc) {
  COleDataObject data;
  if (!data.AttachClipboard()) return FALSE;
  return CreateLinkFromData(&data, render, cfFormat, lpFormatEtc);
}

BOOL COleClientItem::CreateStaticFromClipboard(OLERENDER render, CLIPFORMAT cfFormat,
                                               LPFORMATETC lpFormatEtc) {
  COleDataObject data;
  if (!data.AttachClipboard()) return FALSE;
  return CreateStaticFromData(&data, render, cfFormat, lpFormatEtc);
}

BOOL COleClientItem::CreateFromData(COleDataObject* pDataObject, OLERENDER render,
                                    CLIPFORMAT cfFormat, LPFORMATETC lpFormatEtc) {
  Release();
  GetItemStorage();
  FORMATETC fe;
  SCODE sc = ::OleCreateFromData(pDataObject->m_lpDataObject, IID_IOleObject, render,
                                 MakeFormatEtc(fe, cfFormat, lpFormatEtc), GetClientSite(),
                                 m_lpStorage, (void**)&m_lpObject);
  return FinishCreate(sc);
}

BOOL COleClientItem::CreateLinkFromData(COleDataObject* pDataObject, OLERENDER render,
                                        CLIPFORMAT cfFormat, LPFORMATETC lpFormatEtc) {
  Release();
  GetItemStorage();
  FORMATETC fe;
  SCODE sc = ::OleCreateLinkFromData(pDataObject->m_lpDataObject, IID_IOleObject, render,
                                     MakeFormatEtc(fe, cfFormat, lpFormatEtc), GetClientSite(),
                                     m_lpStorage, (void**)&m_lpObject);
  return FinishCreate(sc);
}

BOOL COleClientItem::CreateStaticFromData(COleDataObject* pDataObject, OLERENDER render,
                                          CLIPFORMAT cfFormat, LPFORMATETC lpFormatEtc) {
  Release();
  GetItemStorage();
  FORMATETC fe;
  SCODE sc = ::OleCreateStaticFromData(pDataObject->m_lpDataObject, IID_IOleObject, render,
                                       MakeFormatEtc(fe, cfFormat, lpFormatEtc),
                                       GetClientSite(), m_lpStorage, (void**)&m_lpObject);
  return FinishCreate(sc);
}

BOOL COleClientItem::CreateFromFile(LPCTSTR lpszFileName, REFCLSID clsid, OLERENDER render,
                                    CLIPFORMAT cfFormat, LPFORMATETC lpFormatEtc) {
  Release();
  GetItemStorage();
  FORMATETC fe;
  CStringW strFile(lpszFileName);
  SCODE sc = ::OleCreateFromFile(clsid, strFile, IID_IOleObject, render,
                                 MakeFormatEtc(fe, cfFormat, lpFormatEtc), GetClientSite(),
                                 m_lpStorage, (void**)&m_lpObject);
  return FinishCreate(sc);
}

BOOL COleClientItem::CreateLinkFromFile(LPCTSTR lpszFileName, OLERENDER render,
                                        CLIPFORMAT cfFormat, LPFORMATETC lpFormatEtc) {
  Release();
  GetItemStorage();
  FORMATETC fe;
  CStringW strFile(lpszFileName);
  SCODE sc = ::OleCreateLinkToFile(strFile, IID_IOleObject, render,
                                   MakeFormatEtc(fe, cfFormat, lpFormatEtc), GetClientSite(),
                                   m_lpStorage, (void**)&m_lpObject);
  return FinishCreate(sc);
}

BOOL COleClientItem::CreateNewItem(REFCLSID clsid, OLERENDER render, CLIPFORMAT cfFormat,
                                   LPFORMATETC lpFormatEtc) {
  Release();
  GetItemStorage();
  FORMATETC fe;
  SCODE sc = ::OleCreate(clsid, IID_IOleObject, render, MakeFormatEtc(fe, cfFormat, lpFormatEtc),
                         GetClientSite(), m_lpStorage, (void**)&m_lpObject);
  return FinishCreate(sc);
}

BOOL COleClientItem::CreateCloneFrom(const COleClientItem* pSrcItem) {
  Release();
  if (pSrcItem->m_lpObject == NULL) return FALSE;
  GetItemStorage();
  m_nDrawAspect = pSrcItem->m_nDrawAspect;
  SCODE sc;
  if (pSrcItem->m_lpStorage != NULL && !::OleIsRunning(pSrcItem->m_lpObject)) {
    // Not running: its storage is all there is to it. (Wine's object
    // handler saves only the pictures of an object that is not running.)
    sc = pSrcItem->m_lpStorage->CopyTo(0, NULL, NULL, m_lpStorage);
  } else {
    LPPERSISTSTORAGE lpPersist = NULL;
    sc = pSrcItem->m_lpObject->QueryInterface(IID_IPersistStorage, (void**)&lpPersist);
    if (SUCCEEDED(sc)) {
      sc = ::OleSave(lpPersist, m_lpStorage, FALSE);
      lpPersist->SaveCompleted(NULL);
      lpPersist->Release();
    }
  }
  if (SUCCEEDED(sc))
    sc = ::OleLoad(m_lpStorage, IID_IOleObject, GetClientSite(), (void**)&m_lpObject);
  return FinishCreate(sc);
}

// ---------------------------------------------------------------------
// Status

void COleClientItem::GetClassID(CLSID* pClassID) const {
  *pClassID = CLSID_NULL;
  if (m_lpObject != NULL) m_lpObject->GetUserClassID(pClassID);
}

void COleClientItem::GetUserType(USERCLASSTYPE nUserClassType, CString& rString) {
  rString.Empty();
  LPOLESTR psz = NULL;
  if (m_lpObject != NULL) {
    SCODE sc = m_lpObject->GetUserType(nUserClassType, &psz);
    // OLE_S_USEREG: the object leaves it to the registry.
    if (sc == OLE_S_USEREG || (SUCCEEDED(sc) && psz == NULL)) {
      CLSID clsid;
      GetClassID(&clsid);
      if (FAILED(::OleRegGetUserType(clsid, nUserClassType, &psz))) psz = NULL;
    } else if (FAILED(sc)) {
      psz = NULL;
    }
  }
  if (psz != NULL) {
    rString = psz;
    ::CoTaskMemFree(psz);
    return;
  }
  AfxLoadString(AFX_IDS_UNKNOWNTYPE, rString);
}

BOOL COleClientItem::GetExtent(LPSIZE lpSize, DVASPECT nDrawAspect) {
  if (m_lpObject == NULL) return FALSE;
  if (nDrawAspect == (DVASPECT)-1) nDrawAspect = m_nDrawAspect;
  SIZEL size;
  m_scLast = m_lpObject->GetExtent(nDrawAspect, &size);
  if (FAILED(m_scLast)) return GetCachedExtent(lpSize, nDrawAspect);
  lpSize->cx = size.cx;
  lpSize->cy = size.cy;
  return TRUE;
}

BOOL COleClientItem::GetCachedExtent(LPSIZE lpSize, DVASPECT nDrawAspect) {
  if (m_lpViewObject == NULL) return FALSE;
  if (nDrawAspect == (DVASPECT)-1) nDrawAspect = m_nDrawAspect;
  SIZEL size;
  m_scLast = m_lpViewObject->GetExtent(nDrawAspect, -1, NULL, &size);
  if (FAILED(m_scLast)) return FALSE;
  lpSize->cx = size.cx;
  lpSize->cy = size.cy;
  return TRUE;
}

void COleClientItem::SetExtent(const CSize& size, DVASPECT nDrawAspect) {
  if (m_lpObject == NULL) return;
  SIZEL sizel = {size.cx, size.cy};
  m_scLast = m_lpObject->SetExtent(nDrawAspect, &sizel);
}

void COleClientItem::SetDrawAspect(DVASPECT nDrawAspect) {
  if (m_nDrawAspect == nDrawAspect) return;
  // The presentation for the new aspect has to be cached for drawing
  // while the object is not running.
  LPOLECACHE lpCache = NULL;
  if (m_lpObject != NULL &&
      SUCCEEDED(m_lpObject->QueryInterface(IID_IOleCache, (void**)&lpCache))) {
    FORMATETC fe = {0, NULL, (DWORD)nDrawAspect, -1, TYMED_NULL};
    DWORD dwConnection;
    lpCache->Cache(&fe, ADVF_PRIMEFIRST, &dwConnection);
    lpCache->Release();
  }
  m_nDrawAspect = nDrawAspect;
  if (m_lpViewObject != NULL) m_lpViewObject->SetAdvise(m_nDrawAspect, 0, &m_xAdviseSink);
  OnChange(OLE_CHANGED_ASPECT, (DWORD)nDrawAspect);
}

HGLOBAL COleClientItem::GetIconicMetafile() {
  if (m_lpObject == NULL) return NULL;
  LPDATAOBJECT lpData = NULL;
  if (SUCCEEDED(m_lpObject->QueryInterface(IID_IDataObject, (void**)&lpData))) {
    FORMATETC fe = {CF_METAFILEPICT, NULL, DVASPECT_ICON, -1, TYMED_MFPICT};
    STGMEDIUM medium;
    HRESULT hr = lpData->GetData(&fe, &medium);
    lpData->Release();
    if (SUCCEEDED(hr) && medium.tymed == TYMED_MFPICT) {
      if (medium.pUnkForRelease == NULL) return medium.hMetaFilePict;
      HGLOBAL h = (HGLOBAL)::OleDuplicateData(medium.hMetaFilePict, CF_METAFILEPICT, 0);
      ::ReleaseStgMedium(&medium);
      return h;
    }
  }
  // The icon of the object's class.
  CLSID clsid;
  GetClassID(&clsid);
  return ::OleGetIconOfClass(clsid, NULL, TRUE);
}

BOOL COleClientItem::SetIconicMetafile(HGLOBAL hMetaPict) {
  if (m_lpObject == NULL) return FALSE;
  LPOLECACHE lpCache = NULL;
  if (FAILED(m_lpObject->QueryInterface(IID_IOleCache, (void**)&lpCache))) return FALSE;
  FORMATETC fe = {CF_METAFILEPICT, NULL, DVASPECT_ICON, -1, TYMED_MFPICT};
  DWORD dwConnection;
  SCODE sc = lpCache->Cache(&fe, ADVF_NODATA, &dwConnection);
  if (SUCCEEDED(sc) || sc == CACHE_S_SAMECACHE) {
    STGMEDIUM medium;
    medium.tymed = TYMED_MFPICT;
    medium.hMetaFilePict = hMetaPict;
    medium.pUnkForRelease = NULL;
    sc = lpCache->SetData(&fe, &medium, FALSE);
  }
  lpCache->Release();
  m_scLast = sc;
  if (FAILED(sc)) return FALSE;
  if (m_nDrawAspect == DVASPECT_ICON) OnChange(OLE_CHANGED, DVASPECT_ICON);
  return TRUE;
}

BOOL COleClientItem::IsModified() const {
  if (m_lpObject == NULL) return FALSE;
  LPPERSISTSTORAGE lpPersist = NULL;
  if (FAILED(m_lpObject->QueryInterface(IID_IPersistStorage, (void**)&lpPersist)))
    return FALSE;
  BOOL bDirty = lpPersist->IsDirty() == S_OK;
  lpPersist->Release();
  return bDirty;
}

BOOL COleClientItem::IsRunning() const {
  return m_lpObject != NULL && ::OleIsRunning(m_lpObject);
}


OLEUPDATE COleClientItem::GetLinkUpdateOptions() {
  DWORD dw = 0;
  LPOLELINK lpLink = NULL;
  if (m_lpObject != NULL &&
      SUCCEEDED(m_lpObject->QueryInterface(IID_IOleLink, (void**)&lpLink))) {
    m_scLast = lpLink->GetUpdateOptions(&dw);
    lpLink->Release();
  }
  return (OLEUPDATE)dw;
}

void COleClientItem::SetLinkUpdateOptions(OLEUPDATE dwUpdateOpt) {
  LPOLELINK lpLink = NULL;
  if (m_lpObject != NULL &&
      SUCCEEDED(m_lpObject->QueryInterface(IID_IOleLink, (void**)&lpLink))) {
    m_scLast = lpLink->SetUpdateOptions(dwUpdateOpt);
    lpLink->Release();
  }
}

BOOL COleClientItem::IsLinkUpToDate() const {
  return m_lpObject == NULL || m_lpObject->IsUpToDate() == S_OK;
}

// ---------------------------------------------------------------------
// Operations

void COleClientItem::Release(OLECLOSE dwCloseOption) {
  if (IsInPlaceActive() || m_pInPlaceFrame != NULL) Deactivate();
  ReleaseInPlace();
  if (m_lpObject != NULL) {
    if (m_lpViewObject != NULL) {
      m_lpViewObject->SetAdvise(m_nDrawAspect, 0, NULL);
      m_lpViewObject->Release();
      m_lpViewObject = NULL;
    }
    if (m_dwConnection != 0) {
      m_lpObject->Unadvise(m_dwConnection);
      m_dwConnection = 0;
    }
    m_lpObject->Close(dwCloseOption);
    m_lpObject->SetClientSite(NULL);
    LPOLEOBJECT lpObject = m_lpObject;
    m_lpObject = NULL;
    lpObject->Release();
  }
  ReleaseStorage();
  m_nItemState = emptyState;
  m_pView = NULL;
}

void COleClientItem::Close(OLECLOSE dwCloseOption) {
  if (m_lpObject == NULL) return;
  if (IsInPlaceActive()) Deactivate();
  m_scLast = m_lpObject->Close(dwCloseOption);
  SetState(loadedState);
}

void COleClientItem::Delete(BOOL bAutoDelete) {
  Release();
  COleDocument* pDoc = GetDocument();
  if (pDoc != NULL && pDoc->m_bCompoundFile && pDoc->m_lpRootStg != NULL &&
      m_dwItemNumber != 0) {
    OLECHAR szName[32];
    GetItemName(szName);
    pDoc->m_lpRootStg->DestroyElement(szName);
  }
  if (pDoc != NULL) pDoc->SetModifiedFlag();
  if (bAutoDelete) delete this;
}

void COleClientItem::Run() {
  if (m_lpObject == NULL) return;
  m_scLast = ::OleRun(m_lpObject);
  if (FAILED(m_scLast)) AfxThrowOleException(m_scLast);
}

// Plays a METAFILEPICT into a rectangle of a DC.
static void PlayMetafilePict(CDC* pDC, const METAFILEPICT* pmfp, LPCRECT lpBounds) {
  int nSaved = pDC->SaveDC();
  pDC->SetMapMode(MM_ANISOTROPIC);
  pDC->SetWindowOrg(0, 0);
  pDC->SetWindowExt(pmfp->xExt, pmfp->yExt);
  pDC->SetViewportOrg(lpBounds->left, lpBounds->top);
  pDC->SetViewportExt(lpBounds->right - lpBounds->left, lpBounds->bottom - lpBounds->top);
  ::PlayMetaFile(pDC->m_hDC, pmfp->hMF);
  pDC->RestoreDC(nSaved);
}

BOOL COleClientItem::Draw(CDC* pDC, LPCRECT lpBounds, DVASPECT nDrawAspect) {
  if (nDrawAspect == (DVASPECT)-1) nDrawAspect = m_nDrawAspect;
  if (m_lpViewObject == NULL) {
    // No view object: the object is the server's own, as when container
    // and server share a process. Ask it for a picture instead.
    if (m_lpObject == NULL) return FALSE;
    LPDATAOBJECT lpDataObject = NULL;
    m_scLast = m_lpObject->QueryInterface(IID_IDataObject, (void**)&lpDataObject);
    if (FAILED(m_scLast)) return FALSE;
    FORMATETC fmt = {CF_METAFILEPICT, NULL, (DWORD)nDrawAspect, -1, TYMED_MFPICT};
    STGMEDIUM stg = {};
    m_scLast = lpDataObject->GetData(&fmt, &stg);
    lpDataObject->Release();
    if (FAILED(m_scLast)) return FALSE;
    METAFILEPICT* pmfp = (METAFILEPICT*)::GlobalLock(stg.hMetaFilePict);
    if (pmfp != NULL) {
      PlayMetafilePict(pDC, pmfp, lpBounds);
      ::GlobalUnlock(stg.hMetaFilePict);
    }
    ::ReleaseStgMedium(&stg);
    return pmfp != NULL;
  }
  RECTL rcl = {lpBounds->left, lpBounds->top, lpBounds->right, lpBounds->bottom};
  RECTL rclW;
  LPRECTL lprclW = NULL;
  if (::GetObjectType(pDC->m_hDC) == OBJ_METADC) {
    // A metafile needs its window bounds.
    POINT org;
    SIZE ext;
    ::GetWindowOrgEx(pDC->m_hDC, &org);
    ::GetWindowExtEx(pDC->m_hDC, &ext);
    rclW.left = org.x;
    rclW.top = org.y;
    rclW.right = ext.cx;
    rclW.bottom = ext.cy;
    lprclW = &rclW;
  }
  DVTARGETDEVICE* ptd = NULL;
  if (pDC->IsPrinting() && GetDocument() != NULL) ptd = GetDocument()->m_ptd;
  HDC hAttribDC = pDC->m_hAttribDC != pDC->m_hDC ? pDC->m_hAttribDC : NULL;
  m_scLast = m_lpViewObject->Draw(nDrawAspect, -1, NULL, ptd, hAttribDC, pDC->m_hDC, &rcl,
                                  lprclW, NULL, 0);
  if (FAILED(m_scLast) && nDrawAspect == DVASPECT_ICON) {
    // Nothing cached for the icon: draw the class icon.
    HGLOBAL hMetaPict = GetIconicMetafile();
    if (hMetaPict != NULL) {
      METAFILEPICT* pmfp = (METAFILEPICT*)::GlobalLock(hMetaPict);
      PlayMetafilePict(pDC, pmfp, lpBounds);
      ::DeleteMetaFile(pmfp->hMF);
      ::GlobalUnlock(hMetaPict);
      ::GlobalFree(hMetaPict);
      m_scLast = S_OK;
    }
  }
  return SUCCEEDED(m_scLast);
}

BOOL COleClientItem::DoVerb(LONG nVerb, CView* pView, LPMSG lpMsg) {
  if (m_lpObject == NULL) return FALSE;
  if (GetType() == OT_STATIC) {
    ReportError(OLE_E_STATIC);
    return FALSE;
  }
  try {
    Activate(nVerb, pView, lpMsg);
  } catch (COleException* e) {
    ReportError(e->m_sc);
    e->Delete();
    return FALSE;
  } catch (CException* e) {
    e->ReportError();
    e->Delete();
    return FALSE;
  }
  return TRUE;
}

void COleClientItem::Activate(LONG nVerb, CView* pView, LPMSG lpMsg) {
  if (m_lpObject == NULL) AfxThrowOleException(E_UNEXPECTED);
  if (pView != NULL) m_pView = pView;
  CRect rect;
  rect.SetRectEmpty();
  OnGetItemPosition(rect);
  HWND hWnd = m_pView != NULL ? m_pView->GetSafeHwnd() : NULL;
  m_scLast = m_lpObject->DoVerb(nVerb, lpMsg, GetClientSite(), -1, hWnd, &rect);
  // A verb the object does not know runs its primary verb.
  if (m_scLast == OLEOBJ_S_INVALIDVERB) m_scLast = S_OK;
  if (FAILED(m_scLast)) AfxThrowOleException(m_scLast);
}


BOOL COleClientItem::UpdateLink() {
  if (m_lpObject == NULL) return FALSE;
  m_scLast = m_lpObject->Update();
  return SUCCEEDED(m_scLast);
}

// Loads the object again from its storage, as after ActivateAs.
BOOL COleClientItem::Reload() {
  if (m_lpObject == NULL || m_lpStorage == NULL) return FALSE;
  LPPERSISTSTORAGE lpPersist = NULL;
  SCODE sc = m_lpObject->QueryInterface(IID_IPersistStorage, (void**)&lpPersist);
  if (SUCCEEDED(sc)) {
    sc = ::OleSave(lpPersist, m_lpStorage, TRUE);
    lpPersist->SaveCompleted(NULL);
    lpPersist->Release();
  }
  if (FAILED(sc)) {
    m_scLast = sc;
    return FALSE;
  }
  // Keep the storage while the old object goes.
  LPSTORAGE lpStorage = m_lpStorage;
  LPLOCKBYTES lpLockBytes = m_lpLockBytes;
  lpStorage->AddRef();
  if (lpLockBytes != NULL) lpLockBytes->AddRef();
  Release();
  m_lpStorage = lpStorage;
  m_lpLockBytes = lpLockBytes;
  sc = ::OleLoad(m_lpStorage, IID_IOleObject, GetClientSite(), (void**)&m_lpObject);
  return FinishCreate(sc);
}

BOOL COleClientItem::ConvertTo(REFCLSID clsidNew) {
  if (m_lpStorage == NULL) return FALSE;
  CString strUserType;
  LPOLESTR pszUserType = NULL;
  ::OleRegGetUserType(clsidNew, USERCLASSTYPE_FULL, &pszUserType);
  CLIPFORMAT cf = 0;
  ::ReadFmtUserTypeStg(m_lpStorage, &cf, NULL);
  m_scLast = ::OleDoAutoConvert(m_lpStorage, (LPCLSID)&clsidNew);
  if (m_scLast != S_OK) {
    m_scLast = ::WriteClassStg(m_lpStorage, clsidNew);
    if (SUCCEEDED(m_scLast)) ::WriteFmtUserTypeStg(m_lpStorage, cf, pszUserType);
    if (SUCCEEDED(m_scLast)) ::SetConvertStg(m_lpStorage, TRUE);
  }
  if (pszUserType != NULL) ::CoTaskMemFree(pszUserType);
  if (FAILED(m_scLast)) return FALSE;
  return Reload();
}

BOOL COleClientItem::ActivateAs(LPCTSTR, REFCLSID clsidOld, REFCLSID clsidNew) {
  m_scLast = ::CoTreatAsClass(clsidOld, clsidNew);
  if (FAILED(m_scLast)) return FALSE;
  return Reload();
}

DVTARGETDEVICE* AFXAPI _AfxOleCreateTargetDevice(const PRINTDLG* ppd) {
  if (ppd == NULL || ppd->hDevNames == NULL) return NULL;
  DEVNAMES* pdn = (DEVNAMES*)::GlobalLock(ppd->hDevNames);
  if (pdn == NULL) return NULL;
  CStringW strDriver((LPCTSTR)pdn + pdn->wDriverOffset);
  CStringW strDevice((LPCTSTR)pdn + pdn->wDeviceOffset);
  CStringW strPort((LPCTSTR)pdn + pdn->wOutputOffset);
  ::GlobalUnlock(ppd->hDevNames);

  // The device mode, in its wide form.
  DEVMODEW* pdmW = NULL;
  UINT cbDevMode = 0;
  if (ppd->hDevMode != NULL) {
    DEVMODE* pdm = (DEVMODE*)::GlobalLock(ppd->hDevMode);
    if (pdm != NULL) {
#ifdef _UNICODE
      cbDevMode = pdm->dmSize + pdm->dmDriverExtra;
      pdmW = (DEVMODEW*)malloc(cbDevMode);
      memcpy(pdmW, pdm, cbDevMode);
#else
      cbDevMode = sizeof(DEVMODEW) + pdm->dmDriverExtra;
      pdmW = (DEVMODEW*)calloc(1, cbDevMode);
      ::MultiByteToWideChar(CP_ACP, 0, (LPCSTR)pdm->dmDeviceName, -1, pdmW->dmDeviceName,
                            CCHDEVICENAME);
      // The fields between the two names, and those after the form name
      // that the ANSI structure has.
      memcpy(&pdmW->dmSpecVersion, &pdm->dmSpecVersion,
             offsetof(DEVMODEA, dmFormName) - offsetof(DEVMODEA, dmSpecVersion));
      ::MultiByteToWideChar(CP_ACP, 0, (LPCSTR)pdm->dmFormName, -1, pdmW->dmFormName,
                            CCHFORMNAME);
      UINT cbTail = pdm->dmSize > offsetof(DEVMODEA, dmLogPixels)
                        ? pdm->dmSize - offsetof(DEVMODEA, dmLogPixels)
                        : 0;
      if (cbTail > sizeof(DEVMODEW) - offsetof(DEVMODEW, dmLogPixels))
        cbTail = sizeof(DEVMODEW) - offsetof(DEVMODEW, dmLogPixels);
      memcpy(&pdmW->dmLogPixels, &pdm->dmLogPixels, cbTail);
      pdmW->dmSize = (WORD)(offsetof(DEVMODEW, dmLogPixels) + cbTail);
      memcpy((BYTE*)pdmW + pdmW->dmSize, (BYTE*)pdm + pdm->dmSize, pdm->dmDriverExtra);
      cbDevMode = pdmW->dmSize + pdm->dmDriverExtra;
#endif
      ::GlobalUnlock(ppd->hDevMode);
    }
  }

  UINT cbHeader = offsetof(DVTARGETDEVICE, tdData);
  UINT cbDriver = (strDriver.GetLength() + 1) * sizeof(OLECHAR);
  UINT cbDevice = (strDevice.GetLength() + 1) * sizeof(OLECHAR);
  UINT cbPort = (strPort.GetLength() + 1) * sizeof(OLECHAR);
  UINT cbTotal = cbHeader + cbDriver + cbDevice + cbPort + cbDevMode;
  DVTARGETDEVICE* ptd = (DVTARGETDEVICE*)::CoTaskMemAlloc(cbTotal);
  if (ptd != NULL) {
    ptd->tdSize = cbTotal;
    ptd->tdDriverNameOffset = (WORD)cbHeader;
    ptd->tdDeviceNameOffset = (WORD)(cbHeader + cbDriver);
    ptd->tdPortNameOffset = (WORD)(cbHeader + cbDriver + cbDevice);
    ptd->tdExtDevmodeOffset = cbDevMode ? (WORD)(cbHeader + cbDriver + cbDevice + cbPort) : 0;
    memcpy((BYTE*)ptd + ptd->tdDriverNameOffset, (LPCWSTR)strDriver, cbDriver);
    memcpy((BYTE*)ptd + ptd->tdDeviceNameOffset, (LPCWSTR)strDevice, cbDevice);
    memcpy((BYTE*)ptd + ptd->tdPortNameOffset, (LPCWSTR)strPort, cbPort);
    if (cbDevMode) memcpy((BYTE*)ptd + ptd->tdExtDevmodeOffset, pdmW, cbDevMode);
  }
  free(pdmW);
  return ptd;
}

BOOL COleClientItem::SetPrintDevice(const DVTARGETDEVICE* ptd) {
  if (m_lpObject == NULL) return FALSE;
  LPOLECACHE2 lpCache = NULL;
  if (FAILED(m_lpObject->QueryInterface(IID_IOleCache2, (void**)&lpCache))) return FALSE;
  // Each presentation cached for the old device is cached again for the
  // new one.
  LPENUMSTATDATA lpEnum = NULL;
  SCODE sc = lpCache->EnumCache(&lpEnum);
  if (SUCCEEDED(sc) && lpEnum != NULL) {
    STATDATA sd;
    while (lpEnum->Next(1, &sd, NULL) == S_OK) {
      if (sd.pAdvSink != NULL) sd.pAdvSink->Release();
      if (sd.formatetc.ptd == NULL && ptd == NULL) continue;
      if (sd.formatetc.ptd != NULL && ptd != NULL && sd.formatetc.ptd->tdSize == ptd->tdSize &&
          memcmp(sd.formatetc.ptd, ptd, ptd->tdSize) == 0) {
        ::CoTaskMemFree(sd.formatetc.ptd);
        continue;
      }
      lpCache->Uncache(sd.dwConnection);
      if (sd.formatetc.ptd != NULL) ::CoTaskMemFree(sd.formatetc.ptd);
      sd.formatetc.ptd = (DVTARGETDEVICE*)ptd;
      DWORD dwConnection;
      lpCache->Cache(&sd.formatetc, sd.advf, &dwConnection);
    }
    lpEnum->Release();
  }
  lpCache->Release();
  m_scLast = sc;
  return SUCCEEDED(sc);
}

BOOL COleClientItem::SetPrintDevice(const PRINTDLG* ppd) {
  DVTARGETDEVICE* ptd = _AfxOleCreateTargetDevice(ppd);
  BOOL bResult = SetPrintDevice(ptd);
  if (ptd != NULL) ::CoTaskMemFree(ptd);
  return bResult;
}

void COleClientItem::SetHostNames(LPCTSTR lpszHost, LPCTSTR lpszHostObj) {
  if (m_lpObject == NULL) return;
  CStringW strHost(lpszHost);
  CStringW strHostObj(lpszHostObj);
  m_scLast = m_lpObject->SetHostNames(strHost, strHostObj);
}

void COleClientItem::ReportError(SCODE sc) const {
  UINT nID;
  switch (sc) {
  case OLE_E_STATIC:
    nID = AFX_IDP_STATIC_OBJECT;
    break;
  case E_OUTOFMEMORY:
    nID = AFX_IDP_FAILED_MEMORY_ALLOC;
    break;
  case RPC_E_SERVERCALL_REJECTED:
  case RPC_E_SERVERCALL_RETRYLATER:
    nID = AFX_IDP_SERVER_BUSY;
    break;
  case OLE_E_CANT_BINDTOSOURCE:
  case MK_E_UNAVAILABLE:
  case MK_E_CONNECTMANUALLY:
  case MK_E_EXCEEDEDDEADLINE:
  case MK_E_INTERMEDIATEINTERFACENOTSUPPORTED:
  case MK_E_NOOBJECT:
  case MK_E_SYNTAX:
    nID = AFX_IDP_FAILED_TO_CONNECT;
    break;
  case OLEOBJ_E_NOVERBS:
  case OLEOBJ_E_INVALIDVERB:
    nID = AFX_IDP_BAD_VERB;
    break;
  default:
    nID = AFX_IDP_FAILED_TO_LAUNCH;
    break;
  }
  CString str;
  AfxLoadString(nID, str);
  AfxMessageBox(str, MB_OK | MB_ICONEXCLAMATION);
}

// ---------------------------------------------------------------------
// Data transfer

void COleClientItem::GetClipboardData(COleDataSource* pDataSource, BOOL bIncludeLink,
                                      LPPOINT lpOffset, LPSIZE lpSize) {
  if (m_lpObject == NULL) return;

  // The object itself, saved into a storage in memory.
  LPLOCKBYTES lpLockBytes = NULL;
  LPSTORAGE lpStorage = NULL;
  SCODE sc = ::CreateILockBytesOnHGlobal(NULL, TRUE, &lpLockBytes);
  if (SUCCEEDED(sc))
    sc = ::StgCreateDocfileOnILockBytes(
        lpLockBytes, STGM_CREATE | STGM_READWRITE | STGM_SHARE_EXCLUSIVE, 0, &lpStorage);
  if (SUCCEEDED(sc)) {
    LPPERSISTSTORAGE lpPersist = NULL;
    sc = m_lpObject->QueryInterface(IID_IPersistStorage, (void**)&lpPersist);
    if (SUCCEEDED(sc)) {
      sc = ::OleSave(lpPersist, lpStorage, FALSE);
      lpPersist->SaveCompleted(NULL);
      lpPersist->Release();
    }
  }
  if (lpLockBytes != NULL) lpLockBytes->Release();
  if (SUCCEEDED(sc)) {
    STGMEDIUM medium;
    medium.tymed = TYMED_ISTORAGE;
    medium.pstg = lpStorage;
    medium.pUnkForRelease = NULL;
    FORMATETC fe = {CF_EMBEDDEDOBJECT_, NULL, DVASPECT_CONTENT, -1, TYMED_ISTORAGE};
    pDataSource->CacheData(fe.cfFormat, &medium, &fe);
  } else if (lpStorage != NULL) {
    lpStorage->Release();
  }

  // Its size, in HIMETRIC.
  CSize size(0, 0);
  if (lpSize != NULL && (lpSize->cx != 0 || lpSize->cy != 0))
    size = *lpSize;
  else
    GetExtent(&size);

  // The object descriptor.
  CLSID clsid;
  GetClassID(&clsid);
  CString strType;
  GetUserType(USERCLASSTYPE_FULL, strType);
  CStringW strTypeW(strType);
  CStringW strSource;
  if (GetDocument() != NULL) strSource = GetDocument()->GetTitle();
  UINT cbType = (strTypeW.GetLength() + 1) * sizeof(WCHAR);
  UINT cbSource = (strSource.GetLength() + 1) * sizeof(WCHAR);
  HGLOBAL hDesc = ::GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT,
                                sizeof(OBJECTDESCRIPTOR) + cbType + cbSource);
  if (hDesc != NULL) {
    OBJECTDESCRIPTOR* pod = (OBJECTDESCRIPTOR*)::GlobalLock(hDesc);
    pod->cbSize = sizeof(OBJECTDESCRIPTOR) + cbType + cbSource;
    pod->clsid = clsid;
    pod->dwDrawAspect = m_nDrawAspect;
    pod->sizel.cx = size.cx;
    pod->sizel.cy = size.cy;
    if (lpOffset != NULL) {
      // The offset is in device units; the descriptor wants HIMETRIC.
      CClientDC dc(NULL);
      CSize offset(lpOffset->x, lpOffset->y);
      dc.DPtoHIMETRIC(&offset);
      pod->pointl.x = offset.cx;
      pod->pointl.y = offset.cy;
    }
    m_lpObject->GetMiscStatus(m_nDrawAspect, &pod->dwStatus);
    pod->dwFullUserTypeName = sizeof(OBJECTDESCRIPTOR);
    pod->dwSrcOfCopy = sizeof(OBJECTDESCRIPTOR) + cbType;
    memcpy((BYTE*)pod + pod->dwFullUserTypeName, (LPCWSTR)strTypeW, cbType);
    memcpy((BYTE*)pod + pod->dwSrcOfCopy, (LPCWSTR)strSource, cbSource);
    ::GlobalUnlock(hDesc);
    pDataSource->CacheGlobalData(CF_OBJECTDESCRIPTOR_, hDesc);
  }

  // A picture, for programs that only take pictures.
  if (m_lpViewObject != NULL && size.cx > 0 && size.cy > 0) {
    HDC hMetaDC = ::CreateMetaFile(NULL);
    if (hMetaDC != NULL) {
      ::SetMapMode(hMetaDC, MM_ANISOTROPIC);
      ::SetWindowOrgEx(hMetaDC, 0, 0, NULL);
      ::SetWindowExtEx(hMetaDC, size.cx, size.cy, NULL);
      RECTL rcl = {0, 0, size.cx, size.cy};
      RECTL rclW = {0, 0, size.cx, size.cy};
      m_lpViewObject->Draw(m_nDrawAspect, -1, NULL, NULL, NULL, hMetaDC, &rcl, &rclW, NULL, 0);
      HMETAFILE hMF = ::CloseMetaFile(hMetaDC);
      HGLOBAL hPict = hMF != NULL ? ::GlobalAlloc(GMEM_MOVEABLE, sizeof(METAFILEPICT)) : NULL;
      if (hPict != NULL) {
        METAFILEPICT* pmfp = (METAFILEPICT*)::GlobalLock(hPict);
        pmfp->mm = MM_ANISOTROPIC;
        pmfp->xExt = size.cx;
        pmfp->yExt = size.cy;
        pmfp->hMF = hMF;
        ::GlobalUnlock(hPict);
        STGMEDIUM medium;
        medium.tymed = TYMED_MFPICT;
        medium.hMetaFilePict = hPict;
        medium.pUnkForRelease = NULL;
        FORMATETC fe = {CF_METAFILEPICT, NULL, m_nDrawAspect, -1, TYMED_MFPICT};
        pDataSource->CacheData(CF_METAFILEPICT, &medium, &fe);
      } else if (hMF != NULL) {
        ::DeleteMetaFile(hMF);
      }
    }
  }

  // A link to the source of a linked item.
  if (bIncludeLink && GetType() == OT_LINK) {
    LPOLELINK lpLink = NULL;
    if (SUCCEEDED(m_lpObject->QueryInterface(IID_IOleLink, (void**)&lpLink))) {
      LPMONIKER lpMoniker = NULL;
      if (SUCCEEDED(lpLink->GetSourceMoniker(&lpMoniker)) && lpMoniker != NULL) {
        LPSTREAM lpStream = NULL;
        if (SUCCEEDED(::CreateStreamOnHGlobal(NULL, TRUE, &lpStream))) {
          if (SUCCEEDED(::OleSaveToStream(lpMoniker, lpStream)) &&
              SUCCEEDED(::WriteClassStm(lpStream, clsid))) {
            LARGE_INTEGER zero;
            zero.QuadPart = 0;
            lpStream->Seek(zero, STREAM_SEEK_SET, NULL);
            STGMEDIUM medium;
            medium.tymed = TYMED_ISTREAM;
            medium.pstm = lpStream;
            medium.pUnkForRelease = NULL;
            FORMATETC fe = {CF_LINKSOURCE_, NULL, DVASPECT_CONTENT, -1, TYMED_ISTREAM};
            pDataSource->CacheData(fe.cfFormat, &medium, &fe);
            lpStream = NULL;
            // The descriptor of the link source is the object's own.
            if (hDesc != NULL) {
              HGLOBAL hLinkDesc = (HGLOBAL)::OleDuplicateData(hDesc, CF_OBJECTDESCRIPTOR_, 0);
              if (hLinkDesc != NULL) pDataSource->CacheGlobalData(CF_LINKSRCDESCRIPTOR_, hLinkDesc);
            }
          }
          if (lpStream != NULL) lpStream->Release();
        }
        lpMoniker->Release();
      }
      lpLink->Release();
    }
  }
}

COleDataSource* COleClientItem::OnGetClipboardData(BOOL bIncludeLink, LPPOINT lpOffset,
                                                   LPSIZE lpSize) {
  COleDataSource* pDataSource = new COleDataSource;
  try {
    GetClipboardData(pDataSource, bIncludeLink, lpOffset, lpSize);
  } catch (CException*) {
    delete pDataSource;
    throw;
  }
  return pDataSource;
}

void COleClientItem::CopyToClipboard(BOOL bIncludeLink) {
  COleDataSource* pDataSource = OnGetClipboardData(bIncludeLink, NULL, NULL);
  pDataSource->SetClipboard();
}

DROPEFFECT COleClientItem::DoDragDrop(LPCRECT lpItemRect, CPoint ptOffset, BOOL bIncludeLink,
                                      DWORD dwEffects, LPCRECT lpRectStartDrag) {
  CSize size(0, 0);
  if (lpItemRect != NULL) {
    // The item's size on screen, in HIMETRIC.
    CClientDC dc(NULL);
    size = CSize(lpItemRect->right - lpItemRect->left, lpItemRect->bottom - lpItemRect->top);
    dc.DPtoHIMETRIC(&size);
  }
  COleDataSource* pDataSource = OnGetClipboardData(bIncludeLink, &ptOffset, &size);
  DROPEFFECT dropEffect = pDataSource->DoDragDrop(dwEffects, lpRectStartDrag);
  pDataSource->InternalRelease();
  return dropEffect;
}

// ---------------------------------------------------------------------
// Overridables

void COleClientItem::OnChange(OLE_NOTIFICATION nCode, DWORD) {
  switch (nCode) {
  case OLE_CHANGED:
  case OLE_SAVED:
  case OLE_CHANGED_ASPECT:
    if (GetDocument() != NULL) GetDocument()->SetModifiedFlag();
    break;
  default:
    break;
  }
}

void COleClientItem::OnGetItemPosition(CRect& rPosition) { rPosition.SetRectEmpty(); }


void COleClientItem::OnGetClipRect(CRect& rClipRect) {
  rClipRect.SetRectEmpty();
  if (m_pView != NULL) m_pView->GetClientRect(&rClipRect);
}

void COleClientItem::OnShowItem() {}

// In-place activation is in oleipcli.cpp.

void COleClientItem::SetState(UINT nNewState) {
  if (m_nItemState == nNewState) return;
  m_nItemState = nNewState;
  OnChange(OLE_CHANGED_STATE, (DWORD)nNewState);
}

// ---------------------------------------------------------------------
// Serialization: the item's number and aspect, then its storage, either
// copied into the archive or in its own storage of the compound file.

void COleClientItem::Serialize(CArchive& ar) {
  CDocItem::Serialize(ar);
  if (ar.IsStoring()) {
    COleDocument* pDoc = GetDocument();
    BOOL bFlat = ar.m_bForceFlat || pDoc == NULL || !pDoc->m_bCompoundFile ||
                 pDoc->m_lpRootStg == NULL;
    ar << m_dwItemNumber;
    ar << (LONG)m_nDrawAspect;
    // 0: in the compound file, 1: in the archive, 2: no object.
    BYTE nWhere = m_lpObject == NULL ? 2 : bFlat ? 1 : 0;
    ar << nWhere;
    if (nWhere == 1)
      WriteItemFlat(ar);
    else if (nWhere == 0)
      WriteItem(ar);
  } else {
    if (m_pDocument == NULL && ar.m_pDocument != NULL &&
        ar.m_pDocument->IsKindOf(RUNTIME_CLASS(COleDocument)))
      ((COleDocument*)ar.m_pDocument)->AddItem(this);
    Release();
    LONG lAspect;
    BYTE bFlat;
    ar >> m_dwItemNumber;
    ar >> lAspect;
    ar >> bFlat;
    m_nDrawAspect = (DVASPECT)lAspect;
    COleDocument* pDoc = GetDocument();
    if (pDoc != NULL && m_dwItemNumber >= pDoc->m_dwNextItemNumber)
      pDoc->m_dwNextItemNumber = m_dwItemNumber + 1;
    if (bFlat == 2) return;
    if (bFlat)
      ReadItemFlat(ar);
    else
      ReadItem(ar);
    FinishLoad();
  }
}

static void SaveItemObject(LPOLEOBJECT lpObject, LPSTORAGE lpStorage, BOOL bSameAsLoad,
                       LPSTORAGE lpNewStorage) {
  LPPERSISTSTORAGE lpPersist = NULL;
  SCODE sc = lpObject->QueryInterface(IID_IPersistStorage, (void**)&lpPersist);
  if (FAILED(sc)) AfxThrowOleException(sc);
  sc = ::OleSave(lpPersist, lpStorage, bSameAsLoad);
  lpPersist->SaveCompleted(lpNewStorage);
  lpPersist->Release();
  if (FAILED(sc)) AfxThrowOleException(sc);
}

void COleClientItem::WriteItemFlat(CArchive& ar) {
  LPLOCKBYTES lpLockBytes = m_lpLockBytes;
  LPSTORAGE lpStorage = NULL;
  if (lpLockBytes != NULL) {
    // The item's storage is in memory already: bring it up to date.
    SaveItemObject(m_lpObject, m_lpStorage, TRUE, NULL);
    lpLockBytes->AddRef();
  } else {
    // Its storage is in a compound file: save a copy into memory.
    SCODE sc = ::CreateILockBytesOnHGlobal(NULL, TRUE, &lpLockBytes);
    if (FAILED(sc)) AfxThrowOleException(sc);
    sc = ::StgCreateDocfileOnILockBytes(
        lpLockBytes, STGM_CREATE | STGM_READWRITE | STGM_SHARE_EXCLUSIVE, 0, &lpStorage);
    if (FAILED(sc)) {
      lpLockBytes->Release();
      AfxThrowOleException(sc);
    }
    try {
      SaveItemObject(m_lpObject, lpStorage, FALSE, NULL);
    } catch (CException*) {
      lpStorage->Release();
      lpLockBytes->Release();
      throw;
    }
  }
  if (lpStorage != NULL) lpStorage->Release();

  STATSTG st;
  SCODE sc = lpLockBytes->Stat(&st, STATFLAG_NONAME);
  HGLOBAL hGlobal = NULL;
  if (SUCCEEDED(sc)) sc = ::GetHGlobalFromILockBytes(lpLockBytes, &hGlobal);
  if (FAILED(sc)) {
    lpLockBytes->Release();
    AfxThrowOleException(sc);
  }
  DWORD cb = (DWORD)st.cbSize.QuadPart;
  ar << cb;
  void* p = ::GlobalLock(hGlobal);
  try {
    ar.Write(p, cb);
  } catch (CException*) {
    ::GlobalUnlock(hGlobal);
    lpLockBytes->Release();
    throw;
  }
  ::GlobalUnlock(hGlobal);
  lpLockBytes->Release();
}

void COleClientItem::ReadItemFlat(CArchive& ar) {
  DWORD cb;
  ar >> cb;
  HGLOBAL hGlobal = ::GlobalAlloc(GMEM_MOVEABLE, cb ? cb : 1);
  if (hGlobal == NULL) AfxThrowMemoryException();
  void* p = ::GlobalLock(hGlobal);
  UINT nRead;
  try {
    nRead = ar.Read(p, cb);
  } catch (CException*) {
    ::GlobalUnlock(hGlobal);
    ::GlobalFree(hGlobal);
    throw;
  }
  ::GlobalUnlock(hGlobal);
  if (nRead != cb) {
    ::GlobalFree(hGlobal);
    AfxThrowArchiveException(CArchiveException::endOfFile);
  }
  SCODE sc = ::CreateILockBytesOnHGlobal(hGlobal, TRUE, &m_lpLockBytes);
  if (FAILED(sc)) {
    ::GlobalFree(hGlobal);
    AfxThrowOleException(sc);
  }
  sc = ::StgOpenStorageOnILockBytes(m_lpLockBytes, NULL, STGM_READWRITE | STGM_SHARE_EXCLUSIVE,
                                    NULL, 0, &m_lpStorage);
  if (SUCCEEDED(sc))
    sc = ::OleLoad(m_lpStorage, IID_IOleObject, GetClientSite(), (void**)&m_lpObject);
  if (FAILED(sc)) {
    m_lpObject = NULL;
    ReleaseStorage();
    AfxThrowOleException(sc);
  }
}

void COleClientItem::WriteItem(CArchive&) {
  COleDocument* pDoc = GetDocument();
  LPSTORAGE lpRoot = pDoc->m_lpRootStg;
  if (pDoc->m_bSameAsLoad && m_lpLockBytes == NULL && m_lpStorage != NULL) {
    // Into its own storage in the file it came from.
    SaveItemObject(m_lpObject, m_lpStorage, TRUE, NULL);
    return;
  }
  // Into a new storage in the new file; with m_bRemember the object then
  // keeps that storage.
  OLECHAR szName[32];
  GetItemName(szName);
  LPSTORAGE lpStorage = NULL;
  SCODE sc = lpRoot->CreateStorage(szName, STGM_CREATE | STGM_READWRITE | STGM_SHARE_EXCLUSIVE,
                                   0, 0, &lpStorage);
  if (FAILED(sc)) AfxThrowOleException(sc);
  try {
    SaveItemObject(m_lpObject, lpStorage, FALSE, pDoc->m_bRemember ? lpStorage : NULL);
  } catch (CException*) {
    lpStorage->Release();
    throw;
  }
  if (pDoc->m_bRemember) {
    ReleaseStorage();
    m_lpStorage = lpStorage;
  } else {
    lpStorage->Release();
  }
}

void COleClientItem::ReadItem(CArchive&) {
  COleDocument* pDoc = GetDocument();
  if (pDoc == NULL || pDoc->m_lpRootStg == NULL) AfxThrowOleException(E_UNEXPECTED);
  OLECHAR szName[32];
  GetItemName(szName);
  SCODE sc = pDoc->m_lpRootStg->OpenStorage(szName, NULL, STGM_READWRITE | STGM_SHARE_EXCLUSIVE,
                                            NULL, 0, &m_lpStorage);
  if (FAILED(sc))
    sc = pDoc->m_lpRootStg->OpenStorage(szName, NULL, STGM_READ | STGM_SHARE_EXCLUSIVE, NULL, 0,
                                        &m_lpStorage);
  if (SUCCEEDED(sc))
    sc = ::OleLoad(m_lpStorage, IID_IOleObject, GetClientSite(), (void**)&m_lpObject);
  if (FAILED(sc)) {
    m_lpObject = NULL;
    ReleaseStorage();
    AfxThrowOleException(sc);
  }
}

// ---------------------------------------------------------------------
// IOleClientSite

STDMETHODIMP_(ULONG) COleClientItem::XOleClientSite::AddRef() {
  METHOD_PROLOGUE_EX_(COleClientItem, OleClientSite)
  return pThis->ExternalAddRef();
}

STDMETHODIMP_(ULONG) COleClientItem::XOleClientSite::Release() {
  METHOD_PROLOGUE_EX_(COleClientItem, OleClientSite)
  return pThis->ExternalRelease();
}

STDMETHODIMP COleClientItem::XOleClientSite::QueryInterface(REFIID iid, LPVOID* ppvObj) {
  METHOD_PROLOGUE_EX_(COleClientItem, OleClientSite)
  return pThis->ExternalQueryInterface(&iid, ppvObj);
}

STDMETHODIMP COleClientItem::XOleClientSite::SaveObject() {
  METHOD_PROLOGUE_EX(COleClientItem, OleClientSite)
  if (pThis->m_lpObject == NULL || pThis->m_lpStorage == NULL) return E_UNEXPECTED;
  try {
    // The object's storage is the one it was loaded from.
    SaveItemObject(pThis->m_lpObject, pThis->m_lpStorage, TRUE, NULL);
    if (pThis->GetDocument() != NULL) pThis->GetDocument()->SetModifiedFlag();
  } catch (COleException* e) {
    SCODE sc = e->m_sc;
    e->Delete();
    return sc;
  } catch (CException* e) {
    e->Delete();
    return E_FAIL;
  }
  return S_OK;
}

// The object's moniker in a container that can be linked to (a
// COleLinkingDoc): the document's, then "!Embedding n".
STDMETHODIMP COleClientItem::XOleClientSite::GetMoniker(DWORD dwAssign, DWORD dwWhichMoniker,
                                                        LPMONIKER* ppmk) {
  METHOD_PROLOGUE_EX(COleClientItem, OleClientSite)
  if (ppmk == NULL) return E_POINTER;
  *ppmk = NULL;
  COleDocument* pDoc = pThis->GetDocument();
  if (pDoc == NULL) return E_FAIL;
  LPMONIKER lpItem = NULL;
  if (dwWhichMoniker != OLEWHICHMK_CONTAINER) {
    if (pThis->m_dwItemNumber == 0) return E_FAIL;
    WCHAR szName[64];
    pThis->GetItemName(szName);
    if (FAILED(::CreateItemMoniker(L"!", szName, &lpItem))) return E_FAIL;
    if (dwWhichMoniker == OLEWHICHMK_OBJREL) {
      *ppmk = lpItem;
      return S_OK;
    }
  }
  LPMONIKER lpDoc = NULL;
  try {
    lpDoc = pDoc->GetMoniker((OLEGETMONIKER)dwAssign);
  } catch (CException* e) {
    e->Delete();
  }
  HRESULT hr = E_FAIL;
  if (lpDoc != NULL) {
    if (lpItem == NULL) {
      *ppmk = lpDoc;
      return S_OK;
    }
    hr = ::CreateGenericComposite(lpDoc, lpItem, ppmk);
    lpDoc->Release();
  }
  if (lpItem != NULL) lpItem->Release();
  return hr;
}

STDMETHODIMP COleClientItem::XOleClientSite::GetContainer(LPOLECONTAINER* ppContainer) {
  METHOD_PROLOGUE_EX(COleClientItem, OleClientSite)
  if (ppContainer == NULL) return E_POINTER;
  *ppContainer = NULL;
  COleDocument* pDoc = pThis->GetDocument();
  if (pDoc == NULL) return E_NOINTERFACE;
  return (HRESULT)pDoc->ExternalQueryInterface(&IID_IOleContainer, (LPVOID*)ppContainer);
}

STDMETHODIMP COleClientItem::XOleClientSite::ShowObject() {
  METHOD_PROLOGUE_EX(COleClientItem, OleClientSite)
  pThis->OnShowItem();
  return S_OK;
}

STDMETHODIMP COleClientItem::XOleClientSite::OnShowWindow(BOOL fShow) {
  METHOD_PROLOGUE_EX(COleClientItem, OleClientSite)
  pThis->SetState(fShow ? openState : loadedState);
  return S_OK;
}

STDMETHODIMP COleClientItem::XOleClientSite::RequestNewObjectLayout() { return E_NOTIMPL; }

// ---------------------------------------------------------------------
// IAdviseSink

STDMETHODIMP_(ULONG) COleClientItem::XAdviseSink::AddRef() {
  METHOD_PROLOGUE_EX_(COleClientItem, AdviseSink)
  return pThis->ExternalAddRef();
}

STDMETHODIMP_(ULONG) COleClientItem::XAdviseSink::Release() {
  METHOD_PROLOGUE_EX_(COleClientItem, AdviseSink)
  return pThis->ExternalRelease();
}

STDMETHODIMP COleClientItem::XAdviseSink::QueryInterface(REFIID iid, LPVOID* ppvObj) {
  METHOD_PROLOGUE_EX_(COleClientItem, AdviseSink)
  return pThis->ExternalQueryInterface(&iid, ppvObj);
}

STDMETHODIMP_(void) COleClientItem::XAdviseSink::OnDataChange(LPFORMATETC, LPSTGMEDIUM) {}

STDMETHODIMP_(void) COleClientItem::XAdviseSink::OnViewChange(DWORD dwAspect, LONG) {
  METHOD_PROLOGUE_EX(COleClientItem, AdviseSink)
  pThis->OnChange(OLE_CHANGED, dwAspect);
}

STDMETHODIMP_(void) COleClientItem::XAdviseSink::OnRename(LPMONIKER) {
  METHOD_PROLOGUE_EX(COleClientItem, AdviseSink)
  pThis->OnChange(OLE_RENAMED, 0);
}

STDMETHODIMP_(void) COleClientItem::XAdviseSink::OnSave() {
  METHOD_PROLOGUE_EX(COleClientItem, AdviseSink)
  pThis->OnChange(OLE_SAVED, 0);
}

STDMETHODIMP_(void) COleClientItem::XAdviseSink::OnClose() {
  METHOD_PROLOGUE_EX(COleClientItem, AdviseSink)
  pThis->SetState(loadedState);
  pThis->OnChange(OLE_CLOSED, 0);
}

// ---------------------------------------------------------------------
// The object verb menu item

void AFXAPI AfxOleSetEditMenu(COleClientItem* pClient, CMenu* pMenu, UINT iMenuItem,
                              UINT nIDVerbMin, UINT nIDVerbMax, UINT nIDConvert) {
  HMENU hPopup = NULL;
  CString strShort;
  if (pClient != NULL) pClient->GetUserType(USERCLASSTYPE_SHORT, strShort);
  CStringW strShortW(strShort);
  // OleUIAddVerbMenu reads the verbs from the registry and the object.
  ::OleUIAddVerbMenuW(pClient != NULL ? pClient->m_lpObject : NULL,
                      pClient != NULL ? (LPCWSTR)strShortW : NULL, pMenu->GetSafeHmenu(),
                      iMenuItem, nIDVerbMin, nIDVerbMax, nIDConvert != 0, nIDConvert, &hPopup);
}
