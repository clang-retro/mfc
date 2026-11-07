// olesvr.cpp - OLE servers: COleLinkingDoc, COleServerDoc (the document a
// container embeds), COleServerItem (what it draws and hands out) and
// COleTemplateServer (the class factory that makes the documents).
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
#include "afximpl.h"
#include <afxole.h>

IMPLEMENT_DYNAMIC(COleLinkingDoc, COleDocument)
IMPLEMENT_DYNAMIC(COleServerDoc, COleLinkingDoc)
IMPLEMENT_DYNAMIC(COleServerItem, CDocItem)
IMPLEMENT_DYNAMIC(COleTemplateServer, COleObjectFactory)

static CLIPFORMAT RegisteredFormat(CLIPFORMAT& cf, LPCTSTR lpszName) {
  if (cf == 0) cf = (CLIPFORMAT)::RegisterClipboardFormat(lpszName);
  return cf;
}
static CLIPFORMAT s_cfEmbedSource, s_cfObjectDescriptor, s_cfLinkSource, s_cfLinkSrcDescriptor;
#define CF_EMBEDSOURCE_ RegisteredFormat(s_cfEmbedSource, _T("Embed Source"))
#define CF_OBJECTDESCRIPTOR_ RegisteredFormat(s_cfObjectDescriptor, _T("Object Descriptor"))
#define CF_LINKSOURCE_ RegisteredFormat(s_cfLinkSource, _T("Link Source"))
#define CF_LINKSRCDESCRIPTOR_ \
  RegisteredFormat(s_cfLinkSrcDescriptor, _T("Link Source Descriptor"))

// Runs an OLE call's work and turns an exception into its HRESULT.
#define OLE_TRY try {
#define OLE_CATCH(hr)                 \
  }                                   \
  catch (CException * e) {            \
    hr = COleException::Process(e);   \
    e->Delete();                      \
  }

static LPOLESTR TaskString(LPCTSTR lpsz) {
  CStringW str(lpsz);
  UINT cb = (str.GetLength() + 1) * sizeof(WCHAR);
  LPOLESTR p = (LPOLESTR)::CoTaskMemAlloc(cb);
  if (p != NULL) memcpy(p, (LPCWSTR)str, cb);
  return p;
}

// ---------------------------------------------------------------------
// The running object table

// What the running object table holds for a document or an item while it
// runs. It hands out the target's interfaces, but the table's reference
// is its own: a document a link opened closes when the links let go,
// though the table still lists it. (Its IUnknown is its own, so COM keys
// the table's stub on it and not on the target.)
class CRunningStandIn : public IUnknown {
public:
  CCmdTarget* m_pTarget;
  LONG m_cRef;
  explicit CRunningStandIn(CCmdTarget* pTarget) : m_pTarget(pTarget), m_cRef(1) {}
  STDMETHOD(QueryInterface)(REFIID iid, void** ppv) {
    if (ppv == NULL) return E_POINTER;
    *ppv = NULL;
    if (::IsEqualIID(iid, IID_IUnknown)) {
      *ppv = this;
      AddRef();
      return S_OK;
    }
    if (m_pTarget == NULL) return CO_E_OBJNOTCONNECTED;
    return (HRESULT)m_pTarget->ExternalQueryInterface(&iid, ppv);
  }
  STDMETHOD_(ULONG, AddRef)() { return (ULONG)::InterlockedIncrement(&m_cRef); }
  STDMETHOD_(ULONG, Release)() {
    LONG n = ::InterlockedDecrement(&m_cRef);
    if (n == 0) delete this;
    return (ULONG)n;
  }
};

// Puts pTarget in the table under lpMoniker; FALSE if it could not.
static BOOL RegisterRunning(CCmdTarget* pTarget, LPMONIKER lpMoniker, DWORD& dwRegister,
                            LPUNKNOWN& lpRunning) {
  dwRegister = 0;
  lpRunning = NULL;
  LPRUNNINGOBJECTTABLE lpROT = NULL;
  if (FAILED(::GetRunningObjectTable(0, &lpROT))) return FALSE;
  CRunningStandIn* pStandIn = new CRunningStandIn(pTarget);
  HRESULT hr = lpROT->Register(0, pStandIn, lpMoniker, &dwRegister);
  lpROT->Release();
  if (FAILED(hr)) {
    dwRegister = 0;
    pStandIn->Release();
    return FALSE;
  }
  lpRunning = pStandIn;
  return TRUE;
}

static void RevokeRunning(DWORD& dwRegister, LPUNKNOWN& lpRunning) {
  if (lpRunning != NULL) ((CRunningStandIn*)lpRunning)->m_pTarget = NULL;
  if (dwRegister != 0) {
    LPRUNNINGOBJECTTABLE lpROT = NULL;
    if (SUCCEEDED(::GetRunningObjectTable(0, &lpROT))) {
      lpROT->Revoke(dwRegister);
      lpROT->Release();
    }
  }
  if (lpRunning != NULL) lpRunning->Release();
  dwRegister = 0;
  lpRunning = NULL;
}

// ---------------------------------------------------------------------
// COleLinkingDoc

BEGIN_INTERFACE_MAP(COleLinkingDoc, COleDocument)
  INTERFACE_PART(COleLinkingDoc, IID_IPersist, PersistFile)
  INTERFACE_PART(COleLinkingDoc, IID_IPersistFile, PersistFile)
  INTERFACE_PART(COleLinkingDoc, IID_IParseDisplayName, OleItemContainer)
  INTERFACE_PART(COleLinkingDoc, IID_IOleContainer, OleItemContainer)
  INTERFACE_PART(COleLinkingDoc, IID_IOleItemContainer, OleItemContainer)
END_INTERFACE_MAP()

COleLinkingDoc::COleLinkingDoc() : m_pFactory(NULL), m_dwRegister(0), m_lpRunning(NULL) {}

COleLinkingDoc::~COleLinkingDoc() { Revoke(); }

BOOL COleLinkingDoc::Register(COleObjectFactory* pFactory, LPCTSTR lpszPathName) {
  Revoke();
  if (pFactory != NULL) m_pFactory = pFactory;
  if (lpszPathName == NULL || *lpszPathName == 0) return TRUE;
  LPMONIKER lpMoniker = NULL;
  CStringW strPath(lpszPathName);
  if (FAILED(::CreateFileMoniker(strPath, &lpMoniker))) return FALSE;
  BOOL bResult = RegisterRunning(this, lpMoniker, m_dwRegister, m_lpRunning);
  lpMoniker->Release();
  return bResult;
}

void COleLinkingDoc::Revoke() { RevokeRunning(m_dwRegister, m_lpRunning); }

// The factory this document's class comes from: the one it was registered
// or created with, or its template's.
static COleObjectFactory* FactoryOf(COleLinkingDoc* pDoc) {
  if (pDoc->m_pFactory != NULL) return pDoc->m_pFactory;
  CDocTemplate* pTemplate = pDoc->GetDocTemplate();
  if (pTemplate != NULL && pTemplate->m_pAttachedFactory != NULL &&
      pTemplate->m_pAttachedFactory->IsKindOf(RUNTIME_CLASS(COleObjectFactory)))
    return (COleObjectFactory*)pTemplate->m_pAttachedFactory;
  return NULL;
}

COleServerItem* COleLinkingDoc::OnGetLinkedItem(LPCTSTR lpszItemName) {
  POSITION pos = GetStartPosition();
  while (pos != NULL) {
    CDocItem* pItem = GetNextItem(pos);
    if (pItem->IsKindOf(RUNTIME_CLASS(COleServerItem)) &&
        ((COleServerItem*)pItem)->GetItemName() == lpszItemName)
      return (COleServerItem*)pItem;
  }
  return NULL;
}

COleClientItem* COleLinkingDoc::OnFindEmbeddedItem(LPCTSTR lpszItemName) {
  CStringW strName(lpszItemName);
  POSITION pos = GetStartPosition();
  while (pos != NULL) {
    COleClientItem* pItem = GetNextClientItem(pos);
    if (pItem == NULL || pItem->m_dwItemNumber == 0) continue;
    WCHAR szName[64];
    pItem->GetItemName(szName);
    if (strName == szName) return pItem;
  }
  return NULL;
}

LPMONIKER COleLinkingDoc::GetMoniker(OLEGETMONIKER) {
  if (m_strPathName.IsEmpty()) return NULL;
  LPMONIKER lpMoniker = NULL;
  CStringW strPath(m_strPathName);
  if (FAILED(::CreateFileMoniker(strPath, &lpMoniker))) return NULL;
  return lpMoniker;
}

BOOL COleLinkingDoc::OnNewDocument() {
  Revoke();
  return COleDocument::OnNewDocument();
}

BOOL COleLinkingDoc::OnOpenDocument(LPCTSTR lpszPathName) {
  Revoke();
  if (!COleDocument::OnOpenDocument(lpszPathName)) return FALSE;
  Register(FactoryOf(this), lpszPathName);
  return TRUE;
}

BOOL COleLinkingDoc::OnSaveDocument(LPCTSTR lpszPathName) {
  BOOL bRemember = m_bRemember;
  if (!COleDocument::OnSaveDocument(lpszPathName)) return FALSE;
  if (bRemember && (m_dwRegister == 0 || !AfxComparePath(m_strPathName, lpszPathName)))
    Register(FactoryOf(this), lpszPathName);
  return TRUE;
}

void COleLinkingDoc::OnCloseDocument() {
  Revoke();
  COleDocument::OnCloseDocument();
}

// IPersistFile

STDMETHODIMP_(ULONG) COleLinkingDoc::XPersistFile::AddRef() {
  METHOD_PROLOGUE_EX_(COleLinkingDoc, PersistFile)
  return pThis->ExternalAddRef();
}

STDMETHODIMP_(ULONG) COleLinkingDoc::XPersistFile::Release() {
  METHOD_PROLOGUE_EX_(COleLinkingDoc, PersistFile)
  return pThis->ExternalRelease();
}

STDMETHODIMP COleLinkingDoc::XPersistFile::QueryInterface(REFIID iid, LPVOID* ppvObj) {
  METHOD_PROLOGUE_EX_(COleLinkingDoc, PersistFile)
  return pThis->ExternalQueryInterface(&iid, ppvObj);
}

STDMETHODIMP COleLinkingDoc::XPersistFile::GetClassID(LPCLSID lpClassID) {
  METHOD_PROLOGUE_EX_(COleLinkingDoc, PersistFile)
  COleObjectFactory* pFactory = FactoryOf(pThis);
  *lpClassID = pFactory != NULL ? pFactory->GetClassID() : CLSID_NULL;
  return pFactory != NULL ? S_OK : E_FAIL;
}

STDMETHODIMP COleLinkingDoc::XPersistFile::IsDirty() {
  METHOD_PROLOGUE_EX_(COleLinkingDoc, PersistFile)
  pThis->UpdateModifiedFlag();
  return pThis->IsModified() ? S_OK : S_FALSE;
}

STDMETHODIMP COleLinkingDoc::XPersistFile::Load(LPCOLESTR lpszFileName, DWORD) {
  METHOD_PROLOGUE_EX_(COleLinkingDoc, PersistFile)
  HRESULT hr = S_OK;
  OLE_TRY
    CString strPath(lpszFileName);
    // A document made for a link (hidden, as for an embedding) is a file's.
    pThis->m_bEmbedded = FALSE;
    if (!pThis->OnOpenDocument(strPath)) return E_FAIL;
    pThis->SetPathName(strPath, FALSE);
  OLE_CATCH(hr)
  return hr;
}

STDMETHODIMP COleLinkingDoc::XPersistFile::Save(LPCOLESTR lpszFileName, BOOL fRemember) {
  METHOD_PROLOGUE_EX_(COleLinkingDoc, PersistFile)
  HRESULT hr = S_OK;
  OLE_TRY
    if (lpszFileName == NULL) return pThis->DoFileSave() ? S_OK : E_FAIL;
    CString strPath(lpszFileName);
    BOOL bRemember = pThis->m_bRemember;
    pThis->m_bRemember = fRemember;
    BOOL bOk = pThis->OnSaveDocument(strPath);
    pThis->m_bRemember = bRemember;
    if (!bOk) return E_FAIL;
    if (fRemember) pThis->SetPathName(strPath, FALSE);
  OLE_CATCH(hr)
  return hr;
}

STDMETHODIMP COleLinkingDoc::XPersistFile::SaveCompleted(LPCOLESTR) { return S_OK; }

STDMETHODIMP COleLinkingDoc::XPersistFile::GetCurFile(LPOLESTR* lplpszFileName) {
  METHOD_PROLOGUE_EX_(COleLinkingDoc, PersistFile)
  if (lplpszFileName == NULL) return E_POINTER;
  *lplpszFileName = TaskString(pThis->GetPathName().IsEmpty() ? pThis->GetTitle()
                                                               : pThis->GetPathName());
  if (*lplpszFileName == NULL) return E_OUTOFMEMORY;
  return pThis->GetPathName().IsEmpty() ? S_FALSE : S_OK;
}

// IOleItemContainer: items are named after the document's moniker with
// "!" before each name.

STDMETHODIMP_(ULONG) COleLinkingDoc::XOleItemContainer::AddRef() {
  METHOD_PROLOGUE_EX_(COleLinkingDoc, OleItemContainer)
  return pThis->ExternalAddRef();
}

STDMETHODIMP_(ULONG) COleLinkingDoc::XOleItemContainer::Release() {
  METHOD_PROLOGUE_EX_(COleLinkingDoc, OleItemContainer)
  return pThis->ExternalRelease();
}

STDMETHODIMP COleLinkingDoc::XOleItemContainer::QueryInterface(REFIID iid, LPVOID* ppvObj) {
  METHOD_PROLOGUE_EX_(COleLinkingDoc, OleItemContainer)
  return pThis->ExternalQueryInterface(&iid, ppvObj);
}

STDMETHODIMP COleLinkingDoc::XOleItemContainer::ParseDisplayName(LPBC, LPOLESTR pszDisplayName,
                                                                 ULONG* pchEaten,
                                                                 LPMONIKER* ppmkOut) {
  METHOD_PROLOGUE_EX_(COleLinkingDoc, OleItemContainer)
  if (pszDisplayName == NULL || pchEaten == NULL || ppmkOut == NULL) return E_POINTER;
  *ppmkOut = NULL;
  *pchEaten = 0;
  // The delimiter, then the name up to the next one.
  LPCOLESTR p = pszDisplayName;
  if (*p == L'!' || *p == L'/' || *p == L'\\' || *p == L':') p++;
  LPCOLESTR pEnd = p;
  while (*pEnd != 0 && *pEnd != L'!' && *pEnd != L'/' && *pEnd != L'\\' && *pEnd != L':')
    pEnd++;
  if (pEnd == p) return MK_E_SYNTAX;
  CStringW strItem(p, (int)(pEnd - p));
  HRESULT hr = S_OK;
  OLE_TRY
    CString strName(strItem);
    if (pThis->OnFindEmbeddedItem(strName) == NULL && pThis->OnGetLinkedItem(strName) == NULL)
      return MK_E_NOOBJECT;
    hr = ::CreateItemMoniker(L"!", strItem, ppmkOut);
  OLE_CATCH(hr)
  if (SUCCEEDED(hr)) *pchEaten = (ULONG)(pEnd - pszDisplayName);
  return hr;
}

STDMETHODIMP COleLinkingDoc::XOleItemContainer::EnumObjects(DWORD, LPENUMUNKNOWN* ppenum) {
  if (ppenum != NULL) *ppenum = NULL;
  return E_NOTIMPL;
}

// While a link is binding, the document stays.
STDMETHODIMP COleLinkingDoc::XOleItemContainer::LockContainer(BOOL fLock) {
  METHOD_PROLOGUE_EX_(COleLinkingDoc, OleItemContainer)
  if (fLock)
    pThis->ExternalAddRef();
  else
    pThis->ExternalRelease();
  return S_OK;
}

STDMETHODIMP COleLinkingDoc::XOleItemContainer::GetObject(LPOLESTR pszItem, DWORD dwSpeedNeeded,
                                                          LPBINDCTX, REFIID riid,
                                                          LPVOID* ppvObject) {
  METHOD_PROLOGUE_EX_(COleLinkingDoc, OleItemContainer)
  if (pszItem == NULL || ppvObject == NULL) return E_POINTER;
  *ppvObject = NULL;
  HRESULT hr = MK_E_NOOBJECT;
  OLE_TRY
    CString strName(pszItem);
    // An object embedded here, run for the link.
    COleClientItem* pClient = pThis->OnFindEmbeddedItem(strName);
    if (pClient != NULL) {
      if (pClient->m_lpObject == NULL) return MK_E_NOOBJECT;
      if (!::OleIsRunning(pClient->m_lpObject)) {
        if (dwSpeedNeeded == BINDSPEED_IMMEDIATE) return MK_E_EXCEEDEDDEADLINE;
        hr = ::OleRun(pClient->m_lpObject);
        if (FAILED(hr)) return hr;
      }
      return pClient->m_lpObject->QueryInterface(riid, ppvObject);
    }
    COleServerItem* pItem = pThis->OnGetLinkedItem(strName);
    if (pItem == NULL) return MK_E_NOOBJECT;
    hr = (HRESULT)pItem->ExternalQueryInterface(&riid, ppvObject);
    if (SUCCEEDED(hr) && !pItem->m_bLinkLocked &&
        pItem->GetDocument() == (COleServerDoc*)pThis) {
      // The links own the item from here and keep its document open; the
      // last to go hands the item back (OnFinalRelease).
      pItem->m_bLinkLocked = TRUE;
      pThis->InternalAddRef();
      pItem->InternalRelease();
      pItem->RegisterLinked();
    }
  OLE_CATCH(hr)
  return hr;
}

STDMETHODIMP COleLinkingDoc::XOleItemContainer::GetObjectStorage(LPOLESTR pszItem, LPBINDCTX,
                                                                 REFIID riid,
                                                                 LPVOID* ppvStorage) {
  METHOD_PROLOGUE_EX_(COleLinkingDoc, OleItemContainer)
  if (pszItem == NULL || ppvStorage == NULL) return E_POINTER;
  *ppvStorage = NULL;
  HRESULT hr = MK_E_NOOBJECT;
  OLE_TRY
    CString strName(pszItem);
    COleClientItem* pClient = pThis->OnFindEmbeddedItem(strName);
    if (pClient != NULL)
      hr = pClient->m_lpStorage != NULL ? pClient->m_lpStorage->QueryInterface(riid, ppvStorage)
                                        : MK_E_NOSTORAGE;
    else if (pThis->OnGetLinkedItem(strName) != NULL)
      hr = MK_E_NOSTORAGE;  // a server's items live in its document
  OLE_CATCH(hr)
  return hr;
}

STDMETHODIMP COleLinkingDoc::XOleItemContainer::IsRunning(LPOLESTR pszItem) {
  METHOD_PROLOGUE_EX_(COleLinkingDoc, OleItemContainer)
  if (pszItem == NULL) return E_POINTER;
  HRESULT hr = MK_E_NOOBJECT;
  OLE_TRY
    CString strName(pszItem);
    COleClientItem* pClient = pThis->OnFindEmbeddedItem(strName);
    if (pClient != NULL)
      hr = pClient->m_lpObject != NULL && ::OleIsRunning(pClient->m_lpObject) ? S_OK : S_FALSE;
    else if (pThis->OnGetLinkedItem(strName) != NULL)
      hr = S_OK;
  OLE_CATCH(hr)
  return hr;
}

// ---------------------------------------------------------------------
// COleServerDoc

BEGIN_INTERFACE_MAP(COleServerDoc, COleLinkingDoc)
  INTERFACE_PART(COleServerDoc, IID_IPersistStorage, PersistStorage)
  INTERFACE_PART(COleServerDoc, IID_IOleObject, OleObject)
  INTERFACE_PART(COleServerDoc, IID_IDataObject, DataObject)
  INTERFACE_PART(COleServerDoc, IID_IOleWindow, OleInPlaceObject)
  INTERFACE_PART(COleServerDoc, IID_IOleInPlaceObject, OleInPlaceObject)
  INTERFACE_PART(COleServerDoc, IID_IOleInPlaceActiveObject, OleInPlaceActiveObject)
END_INTERFACE_MAP()

BEGIN_MESSAGE_MAP(COleServerDoc, COleLinkingDoc)
  ON_COMMAND(ID_FILE_UPDATE, OnFileUpdate)
  ON_UPDATE_COMMAND_UI(ID_FILE_UPDATE, OnUpdateFileUpdate)
  ON_COMMAND(ID_FILE_SAVE_COPY_AS, OnFileSaveCopyAs)
END_MESSAGE_MAP()

COleServerDoc::COleServerDoc()
    : m_lpClientSite(NULL),
      m_lpOleAdviseHolder(NULL),
      m_lpDataAdviseHolder(NULL),
      m_pEmbeddedItem(NULL),
      m_bCntrVisible(FALSE),
      m_bClosing(FALSE),
      m_bOwnRef(TRUE),
      m_bShownBefore(FALSE),
      m_pInPlaceFrame(NULL),
      m_pOrigParent(NULL),
      m_dwOrigStyle(0),
      m_dwOrigStyleEx(0),
      m_lpInPlaceSite(NULL) {
  // A server program runs as long as it has documents.
  AfxOleLockApp();
}

COleServerDoc::~COleServerDoc() {
  if (m_lpInPlaceSite != NULL) m_lpInPlaceSite->Release();
  if (m_lpClientSite != NULL) m_lpClientSite->Release();
  if (m_lpOleAdviseHolder != NULL) m_lpOleAdviseHolder->Release();
  if (m_lpDataAdviseHolder != NULL) m_lpDataAdviseHolder->Release();
  // The items go before the lock, with the rest of the document.
  while (!m_docItemList.IsEmpty()) delete (CDocItem*)m_docItemList.GetHead();
  AfxOleUnlockApp();
}

COleServerItem* COleServerDoc::GetEmbeddedItem() {
  if (m_pEmbeddedItem == NULL) m_pEmbeddedItem = OnGetEmbeddedItem();
  return m_pEmbeddedItem;
}

void COleServerDoc::GetClassIDOf(LPCLSID lpClassID) {
  COleObjectFactory* pFactory = FactoryOf(this);
  *lpClassID = pFactory != NULL ? pFactory->GetClassID() : CLSID_NULL;
}

void COleServerDoc::UpdateAllItems(COleServerItem* pSender, LPARAM lHint, CObject* pHint,
                                   DVASPECT nDrawAspect) {
  POSITION pos = GetStartPosition();
  while (pos != NULL) {
    CDocItem* pItem = GetNextItem(pos);
    if (pItem != pSender && pItem->IsKindOf(RUNTIME_CLASS(COleServerItem)))
      ((COleServerItem*)pItem)->OnUpdate(pSender, lHint, pHint, nDrawAspect);
  }
}

// The document's notifications go to its container and to the links to it
// and to its items.

// The server items, held while a notification goes out: a link that lets
// go of its item there can delete it.
static void ServerItems(COleServerDoc* pDoc, CPtrArray& items) {
  POSITION pos = pDoc->GetStartPosition();
  while (pos != NULL) {
    CDocItem* pItem = pDoc->GetNextItem(pos);
    if (pItem->IsKindOf(RUNTIME_CLASS(COleServerItem))) {
      pItem->InternalAddRef();
      items.Add(pItem);
    }
  }
}

static void ReleaseItems(CPtrArray& items) {
  for (INT_PTR i = 0; i < items.GetSize(); i++) ((CDocItem*)items[i])->InternalRelease();
}

void COleServerDoc::NotifyChanged() {
  if (m_lpDataAdviseHolder != NULL)
    m_lpDataAdviseHolder->SendOnDataChange((LPDATAOBJECT)&m_xDataObject, 0, 0);
  CPtrArray items;
  ServerItems(this, items);
  for (INT_PTR i = 0; i < items.GetSize(); i++) {
    COleServerItem* pItem = (COleServerItem*)items[i];
    if (pItem->m_lpDataAdviseHolder != NULL)
      pItem->m_lpDataAdviseHolder->SendOnDataChange((LPDATAOBJECT)&pItem->m_xDataObject, 0, 0);
  }
  ReleaseItems(items);
}

void COleServerDoc::NotifyClosed() {
  if (m_lpOleAdviseHolder != NULL) m_lpOleAdviseHolder->SendOnClose();
  CPtrArray items;
  ServerItems(this, items);
  for (INT_PTR i = 0; i < items.GetSize(); i++) {
    COleServerItem* pItem = (COleServerItem*)items[i];
    if (pItem->m_lpOleAdviseHolder != NULL) pItem->m_lpOleAdviseHolder->SendOnClose();
  }
  ReleaseItems(items);
}

void COleServerDoc::NotifySaved() {
  if (m_lpOleAdviseHolder != NULL) m_lpOleAdviseHolder->SendOnSave();
  CPtrArray items;
  ServerItems(this, items);
  for (INT_PTR i = 0; i < items.GetSize(); i++) {
    COleServerItem* pItem = (COleServerItem*)items[i];
    if (pItem->m_lpOleAdviseHolder != NULL) pItem->m_lpOleAdviseHolder->SendOnSave();
  }
  ReleaseItems(items);
}

// The moniker of an item of the document at lpDoc: the document's own for
// the embedded item, and "!name" after it for the others.
static LPMONIKER ItemMoniker(COleServerItem* pItem, LPMONIKER lpDoc) {
  if (lpDoc == NULL) return NULL;
  if (!pItem->IsLinkedItem()) {
    lpDoc->AddRef();
    return lpDoc;
  }
  if (pItem->GetItemName().IsEmpty()) return NULL;
  CStringW strName(pItem->GetItemName());
  LPMONIKER lpItem = NULL;
  if (FAILED(::CreateItemMoniker(L"!", strName, &lpItem))) return NULL;
  LPMONIKER lpFull = NULL;
  HRESULT hr = ::CreateGenericComposite(lpDoc, lpItem, &lpFull);
  lpItem->Release();
  return SUCCEEDED(hr) ? lpFull : NULL;
}

void COleServerDoc::NotifyRename(LPCTSTR lpszNewName) {
  LPMONIKER lpMoniker = NULL;
  CStringW strName(lpszNewName);
  if (FAILED(::CreateFileMoniker(strName, &lpMoniker))) return;
  if (m_lpOleAdviseHolder != NULL) m_lpOleAdviseHolder->SendOnRename(lpMoniker);
  CPtrArray items;
  ServerItems(this, items);
  for (INT_PTR i = 0; i < items.GetSize(); i++) {
    COleServerItem* pItem = (COleServerItem*)items[i];
    if (pItem->m_lpOleAdviseHolder == NULL) continue;
    LPMONIKER lpItem = ItemMoniker(pItem, lpMoniker);
    if (lpItem != NULL) {
      pItem->m_lpOleAdviseHolder->SendOnRename(lpItem);
      lpItem->Release();
    }
  }
  ReleaseItems(items);
  lpMoniker->Release();
}

LPMONIKER COleServerDoc::GetMoniker(OLEGETMONIKER nAssign) {
  if (!IsEmbedded()) return COleLinkingDoc::GetMoniker(nAssign);
  if (m_lpClientSite == NULL) return NULL;
  LPMONIKER lpMoniker = NULL;
  if (FAILED(m_lpClientSite->GetMoniker(nAssign, OLEWHICHMK_OBJFULL, &lpMoniker))) return NULL;
  return lpMoniker;
}

// Saving a file the links name tells them.
BOOL COleServerDoc::OnSaveDocument(LPCTSTR lpszPathName) {
  BOOL bRemember = m_bRemember;
  if (!COleLinkingDoc::OnSaveDocument(lpszPathName)) return FALSE;
  if (bRemember && !IsEmbedded()) NotifySaved();
  return TRUE;
}

void COleServerDoc::SaveEmbedding() {
  if (m_lpClientSite == NULL) return;
  SCODE sc = m_lpClientSite->SaveObject();
  if (FAILED(sc)) AfxThrowOleException(sc);
}

BOOL COleServerDoc::OnUpdateDocument() {
  if (!IsEmbedded()) return DoFileSave();
  try {
    NotifyChanged();
    SaveEmbedding();
  } catch (CException* e) {
    e->Delete();
    AfxMessageBox(AFX_IDP_FAILED_TO_UPDATE);
    return FALSE;
  }
  return TRUE;
}

// The first frame of the document, and the frame that holds it when that
// is an MDI child.
static CFrameWnd* FirstFrame(CDocument* pDoc) {
  POSITION pos = pDoc->GetFirstViewPosition();
  if (pos == NULL) return NULL;
  return pDoc->GetNextView(pos)->GetParentFrame();
}

void COleServerDoc::UpdateMenuText(CMenu* pMenu) {
  if (pMenu == NULL || m_strHostObj.IsEmpty()) return;
  CString str;
  AfxFormatString1(str, AFX_IDS_UPDATE_MENU, m_strHostObj);
  if (::GetMenuState(pMenu->m_hMenu, ID_FILE_UPDATE, MF_BYCOMMAND) != (UINT)-1)
    ::ModifyMenu(pMenu->m_hMenu, ID_FILE_UPDATE, MF_BYCOMMAND | MF_STRING, ID_FILE_UPDATE, str);
  AfxFormatString1(str, AFX_IDS_EXIT_MENU, m_strHostObj);
  if (::GetMenuState(pMenu->m_hMenu, ID_APP_EXIT, MF_BYCOMMAND) != (UINT)-1)
    ::ModifyMenu(pMenu->m_hMenu, ID_APP_EXIT, MF_BYCOMMAND | MF_STRING, ID_APP_EXIT, str);
}

void COleServerDoc::OnShowDocument(BOOL bShow) {
  // Opening an object that is active in place takes it out first.
  if (IsInPlaceActive()) OnDeactivate();
  CFrameWnd* pFrame = FirstFrame(this);
  CWnd* pMainWnd = AfxGetMainWnd();
  if (bShow) {
    if (m_lpClientSite != NULL) m_lpClientSite->ShowObject();
    if (pFrame != NULL) {
      CFrameWnd* pTopFrame = pFrame->GetTopLevelFrame();
      if (pTopFrame != NULL && pTopFrame != pFrame && !pTopFrame->IsWindowVisible())
        pTopFrame->ShowWindow(SW_SHOW);
      if (pTopFrame == NULL) pTopFrame = pFrame;
      // The frame's menu comes from its active document: the embedding menu.
      if (pFrame->GetActiveView() == NULL) {
        POSITION pos = GetFirstViewPosition();
        pFrame->SetActiveView(GetNextView(pos), FALSE);
      }
      if (!m_bShownBefore) {
        // The views have their initial update when they first show.
        m_bShownBefore = TRUE;
        pFrame->InitialUpdateFrame(this, TRUE);
      } else {
        pFrame->ActivateFrame(SW_SHOW);
      }
      pTopFrame->OnUpdateFrameMenu(NULL);
      UpdateMenuText(pTopFrame->GetMenu());
      pTopFrame->DrawMenuBar();
      pTopFrame->SetForegroundWindow();
    }
    if (!m_bCntrVisible && m_lpClientSite != NULL) {
      m_bCntrVisible = TRUE;
      m_lpClientSite->OnShowWindow(TRUE);
    }
  } else {
    if (m_bCntrVisible && m_lpClientSite != NULL) {
      m_bCntrVisible = FALSE;
      m_lpClientSite->OnShowWindow(FALSE);
    }
    if (pFrame != NULL) {
      pFrame->ShowWindow(SW_HIDE);
      // The program's main window goes too unless the user started it.
      if (pMainWnd != NULL && pMainWnd != pFrame && !AfxOleGetUserCtrl())
        pMainWnd->ShowWindow(SW_HIDE);
    }
  }
}

void COleServerDoc::OnSetHostNames(LPCTSTR lpszHost, LPCTSTR lpszHostObj) {
  m_strHostApp = lpszHost;
  m_strHostObj = lpszHostObj;
  if (!m_strHostObj.IsEmpty()) SetTitle(m_strHostObj);
  CFrameWnd* pFrame = FirstFrame(this);
  if (pFrame != NULL) {
    CFrameWnd* pTop = pFrame->GetTopLevelFrame();
    if (pTop == NULL) pTop = pFrame;
    if (pTop->IsWindowVisible()) UpdateMenuText(pTop->GetMenu());
  }
}

void COleServerDoc::OnClose(OLECLOSE dwCloseOption) {
  if (m_bClosing) return;
  if (IsInPlaceActive()) OnDeactivate();
  UpdateModifiedFlag();
  switch (dwCloseOption) {
  case OLECLOSE_PROMPTSAVE: {
    CFrameWnd* pFrame = FirstFrame(this);
    if (pFrame != NULL && pFrame->IsWindowVisible()) {
      if (!SaveModified()) AfxThrowOleException(OLE_E_PROMPTSAVECANCELLED);
      break;
    }
    // Nobody to ask: save, as for OLECLOSE_SAVEIFDIRTY.
  }
  // fallthrough
  case OLECLOSE_SAVEIFDIRTY:
    if (IsModified()) {
      NotifyChanged();
      SaveEmbedding();
    }
    break;
  default:
    break;
  }
  OnCloseDocument();
}

// An embedded document asks to update its container, not to save itself.
BOOL COleServerDoc::SaveModified() {
  if (!IsEmbedded()) return COleLinkingDoc::SaveModified();
  UpdateModifiedFlag();
  if (!IsModified() || m_lpClientSite == NULL) return TRUE;
  CString strPrompt;
  AfxFormatString1(strPrompt, AFX_IDP_ASK_TO_UPDATE,
                   m_strHostObj.IsEmpty() ? (LPCTSTR)GetTitle() : (LPCTSTR)m_strHostObj);
  switch (AfxMessageBox(strPrompt, MB_YESNOCANCEL | MB_ICONQUESTION, AFX_IDP_ASK_TO_UPDATE)) {
  case IDCANCEL:
    return FALSE;
  case IDYES:
    return OnUpdateDocument();
  default:
    return TRUE;
  }
}

void COleServerDoc::OnCloseDocument() {
  if (m_bClosing) return;
  // Out of the container's window first.
  if (IsInPlaceActive()) OnDeactivate();
  m_bClosing = TRUE;
  // Keep the document while its containers let go of it.
  InternalAddRef();
  if (m_bCntrVisible && m_lpClientSite != NULL) {
    m_bCntrVisible = FALSE;
    m_lpClientSite->OnShowWindow(FALSE);
  }
  NotifyClosed();
  // The links to the items let go of them.
  {
    CPtrArray items;
    ServerItems(this, items);
    for (INT_PTR i = 0; i < items.GetSize(); i++) ((COleServerItem*)items[i])->Disconnect();
    ReleaseItems(items);
  }
  ::CoDisconnectObject((LPUNKNOWN)&m_xOleObject, 0);
  if (m_lpClientSite != NULL) {
    m_lpClientSite->Release();
    m_lpClientSite = NULL;
  }
  if (m_lpOleAdviseHolder != NULL) {
    m_lpOleAdviseHolder->Release();
    m_lpOleAdviseHolder = NULL;
  }
  if (m_lpDataAdviseHolder != NULL) {
    m_lpDataAdviseHolder->Release();
    m_lpDataAdviseHolder = NULL;
  }

  BOOL bAutoDelete = m_bAutoDelete;
  m_bAutoDelete = FALSE;
  COleLinkingDoc::OnCloseDocument();
  m_bAutoDelete = bAutoDelete;

  // The document's own reference, then the one above: the last reference
  // gone deletes it (OnFinalRelease).
  if (m_bOwnRef) {
    m_bOwnRef = FALSE;
    InternalRelease();
  }
  InternalRelease();
}

void COleServerDoc::OnFinalRelease() {
  if (m_bClosing) {
    if (m_bAutoDelete) delete this;
    return;
  }
  // The containers let go first: close, which ends with the last release.
  m_dwRef = 0;
  m_bOwnRef = FALSE;
  OnCloseDocument();
}

HMENU COleServerDoc::GetDefaultMenu() {
  CDocTemplate* pTemplate = GetDocTemplate();
  if (IsInPlaceActive() && pTemplate != NULL && pTemplate->m_hMenuInPlaceServer != NULL)
    return pTemplate->m_hMenuInPlaceServer;
  if (IsEmbedded() && pTemplate != NULL && pTemplate->m_hMenuEmbedding != NULL)
    return pTemplate->m_hMenuEmbedding;
  return COleLinkingDoc::GetDefaultMenu();
}

HACCEL COleServerDoc::GetDefaultAccelerator() {
  CDocTemplate* pTemplate = GetDocTemplate();
  if (IsInPlaceActive() && pTemplate != NULL && pTemplate->m_hAccelInPlaceServer != NULL)
    return pTemplate->m_hAccelInPlaceServer;
  if (IsEmbedded() && pTemplate != NULL && pTemplate->m_hAccelEmbedding != NULL)
    return pTemplate->m_hAccelEmbedding;
  return COleLinkingDoc::GetDefaultAccelerator();
}

void COleServerDoc::OnNewEmbedding(LPSTORAGE lpStorage) {
  ReleaseRoot();
  lpStorage->AddRef();
  m_lpRootStg = lpStorage;
  m_bEmbedded = TRUE;
}

void COleServerDoc::OnOpenEmbedding(LPSTORAGE lpStorage) {
  DeleteContents();
  ReleaseRoot();
  lpStorage->AddRef();
  m_lpRootStg = lpStorage;
  m_bEmbedded = TRUE;
  try {
    LoadFromStorage();
  } catch (CException*) {
    DeleteContents();
    throw;
  }
  SetModifiedFlag(FALSE);
}

void COleServerDoc::OnSaveEmbedding(LPSTORAGE lpStorage) {
  LPSTORAGE lpOldRoot = m_lpRootStg;
  m_bSameAsLoad = lpStorage == lpOldRoot;
  m_lpRootStg = lpStorage;
  try {
    SaveToStorage();
  } catch (CException*) {
    m_lpRootStg = lpOldRoot;
    m_bSameAsLoad = TRUE;
    throw;
  }
  m_lpRootStg = lpOldRoot;
  m_bSameAsLoad = TRUE;
}

void COleServerDoc::OnFileUpdate() { OnUpdateDocument(); }

void COleServerDoc::OnUpdateFileUpdate(CCmdUI* pCmdUI) { pCmdUI->Enable(IsEmbedded()); }

// A copy in a file of its own; the document stays as it was.
void COleServerDoc::OnFileSaveCopyAs() {
  BOOL bModified = IsModified();
  BOOL bRemember = m_bRemember;
  m_bRemember = FALSE;
  DoSave(NULL, FALSE);
  m_bRemember = bRemember;
  SetModifiedFlag(bModified);
}

// IPersistStorage

STDMETHODIMP_(ULONG) COleServerDoc::XPersistStorage::AddRef() {
  METHOD_PROLOGUE_EX_(COleServerDoc, PersistStorage)
  return pThis->ExternalAddRef();
}

STDMETHODIMP_(ULONG) COleServerDoc::XPersistStorage::Release() {
  METHOD_PROLOGUE_EX_(COleServerDoc, PersistStorage)
  return pThis->ExternalRelease();
}

STDMETHODIMP COleServerDoc::XPersistStorage::QueryInterface(REFIID iid, LPVOID* ppvObj) {
  METHOD_PROLOGUE_EX_(COleServerDoc, PersistStorage)
  return pThis->ExternalQueryInterface(&iid, ppvObj);
}

STDMETHODIMP COleServerDoc::XPersistStorage::GetClassID(LPCLSID lpClassID) {
  METHOD_PROLOGUE_EX_(COleServerDoc, PersistStorage)
  if (lpClassID == NULL) return E_POINTER;
  pThis->GetClassIDOf(lpClassID);
  return S_OK;
}

STDMETHODIMP COleServerDoc::XPersistStorage::IsDirty() {
  METHOD_PROLOGUE_EX_(COleServerDoc, PersistStorage)
  pThis->UpdateModifiedFlag();
  return pThis->IsModified() ? S_OK : S_FALSE;
}

STDMETHODIMP COleServerDoc::XPersistStorage::InitNew(LPSTORAGE pStg) {
  METHOD_PROLOGUE_EX_(COleServerDoc, PersistStorage)
  if (pStg == NULL) return E_INVALIDARG;
  HRESULT hr = S_OK;
  OLE_TRY
    pThis->OnNewEmbedding(pStg);
  OLE_CATCH(hr)
  return hr;
}

STDMETHODIMP COleServerDoc::XPersistStorage::Load(LPSTORAGE pStg) {
  METHOD_PROLOGUE_EX_(COleServerDoc, PersistStorage)
  if (pStg == NULL) return E_INVALIDARG;
  HRESULT hr = S_OK;
  OLE_TRY
    pThis->OnOpenEmbedding(pStg);
  OLE_CATCH(hr)
  return hr;
}

STDMETHODIMP COleServerDoc::XPersistStorage::Save(LPSTORAGE pStgSave, BOOL) {
  METHOD_PROLOGUE_EX_(COleServerDoc, PersistStorage)
  if (pStgSave == NULL) return E_INVALIDARG;
  HRESULT hr = S_OK;
  OLE_TRY
    pThis->OnSaveEmbedding(pStgSave);
  OLE_CATCH(hr)
  return hr;
}

STDMETHODIMP COleServerDoc::XPersistStorage::SaveCompleted(LPSTORAGE pStgNew) {
  METHOD_PROLOGUE_EX_(COleServerDoc, PersistStorage)
  if (pStgNew != NULL) {
    pStgNew->AddRef();
    pThis->ReleaseRoot();
    pThis->m_lpRootStg = pStgNew;
  }
  pThis->SetModifiedFlag(FALSE);
  pThis->NotifySaved();
  return S_OK;
}

STDMETHODIMP COleServerDoc::XPersistStorage::HandsOffStorage() {
  METHOD_PROLOGUE_EX_(COleServerDoc, PersistStorage)
  pThis->ReleaseRoot();
  return S_OK;
}

// IOleObject

STDMETHODIMP_(ULONG) COleServerDoc::XOleObject::AddRef() {
  METHOD_PROLOGUE_EX_(COleServerDoc, OleObject)
  return pThis->ExternalAddRef();
}

STDMETHODIMP_(ULONG) COleServerDoc::XOleObject::Release() {
  METHOD_PROLOGUE_EX_(COleServerDoc, OleObject)
  return pThis->ExternalRelease();
}

STDMETHODIMP COleServerDoc::XOleObject::QueryInterface(REFIID iid, LPVOID* ppvObj) {
  METHOD_PROLOGUE_EX_(COleServerDoc, OleObject)
  return pThis->ExternalQueryInterface(&iid, ppvObj);
}

STDMETHODIMP COleServerDoc::XOleObject::SetClientSite(LPOLECLIENTSITE pClientSite) {
  METHOD_PROLOGUE_EX_(COleServerDoc, OleObject)
  if (pClientSite != NULL) pClientSite->AddRef();
  if (pThis->m_lpClientSite != NULL) pThis->m_lpClientSite->Release();
  pThis->m_lpClientSite = pClientSite;
  return S_OK;
}

STDMETHODIMP COleServerDoc::XOleObject::GetClientSite(LPOLECLIENTSITE* ppClientSite) {
  METHOD_PROLOGUE_EX_(COleServerDoc, OleObject)
  if (ppClientSite == NULL) return E_POINTER;
  *ppClientSite = pThis->m_lpClientSite;
  if (*ppClientSite != NULL) (*ppClientSite)->AddRef();
  return S_OK;
}

STDMETHODIMP COleServerDoc::XOleObject::SetHostNames(LPCOLESTR szContainerApp,
                                                     LPCOLESTR szContainerObj) {
  METHOD_PROLOGUE_EX_(COleServerDoc, OleObject)
  HRESULT hr = S_OK;
  OLE_TRY
    CString strApp(szContainerApp != NULL ? szContainerApp : L"");
    CString strObj(szContainerObj != NULL ? szContainerObj : L"");
    pThis->OnSetHostNames(strApp, strObj);
  OLE_CATCH(hr)
  return hr;
}

STDMETHODIMP COleServerDoc::XOleObject::Close(DWORD dwSaveOption) {
  METHOD_PROLOGUE_EX_(COleServerDoc, OleObject)
  HRESULT hr = S_OK;
  pThis->InternalAddRef();
  OLE_TRY
    pThis->OnClose((OLECLOSE)dwSaveOption);
  OLE_CATCH(hr)
  pThis->InternalRelease();
  return hr;
}

STDMETHODIMP COleServerDoc::XOleObject::SetMoniker(DWORD, LPMONIKER) { return S_OK; }

STDMETHODIMP COleServerDoc::XOleObject::GetMoniker(DWORD dwAssign, DWORD dwWhichMoniker,
                                                   LPMONIKER* ppmk) {
  METHOD_PROLOGUE_EX_(COleServerDoc, OleObject)
  if (ppmk == NULL) return E_POINTER;
  *ppmk = NULL;
  if (pThis->m_lpClientSite != NULL)
    return pThis->m_lpClientSite->GetMoniker(dwAssign, dwWhichMoniker, ppmk);
  if (pThis->IsEmbedded()) return E_UNEXPECTED;
  // A link to the whole file: its moniker.
  *ppmk = pThis->GetMoniker((OLEGETMONIKER)dwAssign);
  return *ppmk != NULL ? S_OK : MK_E_UNAVAILABLE;
}

STDMETHODIMP COleServerDoc::XOleObject::InitFromData(LPDATAOBJECT, BOOL, DWORD) {
  return E_NOTIMPL;
}

STDMETHODIMP COleServerDoc::XOleObject::GetClipboardData(DWORD, LPDATAOBJECT* ppDataObject) {
  if (ppDataObject != NULL) *ppDataObject = NULL;
  return E_NOTIMPL;
}

STDMETHODIMP COleServerDoc::XOleObject::DoVerb(LONG iVerb, LPMSG, LPOLECLIENTSITE, LONG, HWND,
                                               LPCRECT) {
  METHOD_PROLOGUE_EX_(COleServerDoc, OleObject)
  HRESULT hr = S_OK;
  pThis->InternalAddRef();
  OLE_TRY
    COleServerItem* pItem = pThis->GetEmbeddedItem();
    if (pItem == NULL)
      hr = E_UNEXPECTED;
    else
      pItem->OnDoVerb(iVerb);
  OLE_CATCH(hr)
  pThis->InternalRelease();
  return hr;
}

STDMETHODIMP COleServerDoc::XOleObject::EnumVerbs(LPENUMOLEVERB* ppEnumOleVerb) {
  METHOD_PROLOGUE_EX_(COleServerDoc, OleObject)
  CLSID clsid;
  pThis->GetClassIDOf(&clsid);
  return ::OleRegEnumVerbs(clsid, ppEnumOleVerb);
}

STDMETHODIMP COleServerDoc::XOleObject::Update() {
  METHOD_PROLOGUE_EX_(COleServerDoc, OleObject)
  pThis->NotifyChanged();
  return S_OK;
}

STDMETHODIMP COleServerDoc::XOleObject::IsUpToDate() { return S_OK; }

STDMETHODIMP COleServerDoc::XOleObject::GetUserClassID(LPCLSID pClsid) {
  METHOD_PROLOGUE_EX_(COleServerDoc, OleObject)
  if (pClsid == NULL) return E_POINTER;
  pThis->GetClassIDOf(pClsid);
  return S_OK;
}

STDMETHODIMP COleServerDoc::XOleObject::GetUserType(DWORD dwFormOfType, LPOLESTR* pszUserType) {
  METHOD_PROLOGUE_EX_(COleServerDoc, OleObject)
  CLSID clsid;
  pThis->GetClassIDOf(&clsid);
  return ::OleRegGetUserType(clsid, dwFormOfType, pszUserType);
}

STDMETHODIMP COleServerDoc::XOleObject::SetExtent(DWORD dwDrawAspect, LPSIZEL psizel) {
  METHOD_PROLOGUE_EX_(COleServerDoc, OleObject)
  if (psizel == NULL) return E_POINTER;
  HRESULT hr = E_FAIL;
  OLE_TRY
    COleServerItem* pItem = pThis->GetEmbeddedItem();
    if (pItem != NULL &&
        pItem->OnSetExtent((DVASPECT)dwDrawAspect, CSize(psizel->cx, psizel->cy)))
      hr = S_OK;
  OLE_CATCH(hr)
  return hr;
}

STDMETHODIMP COleServerDoc::XOleObject::GetExtent(DWORD dwDrawAspect, LPSIZEL psizel) {
  METHOD_PROLOGUE_EX_(COleServerDoc, OleObject)
  if (psizel == NULL) return E_POINTER;
  HRESULT hr = E_FAIL;
  OLE_TRY
    COleServerItem* pItem = pThis->GetEmbeddedItem();
    CSize size(0, 0);
    if (pItem != NULL && pItem->OnGetExtent((DVASPECT)dwDrawAspect, size)) {
      psizel->cx = size.cx;
      psizel->cy = size.cy;
      hr = S_OK;
    }
  OLE_CATCH(hr)
  return hr;
}

STDMETHODIMP COleServerDoc::XOleObject::Advise(LPADVISESINK pAdvSink, DWORD* pdwConnection) {
  METHOD_PROLOGUE_EX_(COleServerDoc, OleObject)
  if (pThis->m_lpOleAdviseHolder == NULL &&
      FAILED(::CreateOleAdviseHolder(&pThis->m_lpOleAdviseHolder)))
    return E_OUTOFMEMORY;
  return pThis->m_lpOleAdviseHolder->Advise(pAdvSink, pdwConnection);
}

STDMETHODIMP COleServerDoc::XOleObject::Unadvise(DWORD dwConnection) {
  METHOD_PROLOGUE_EX_(COleServerDoc, OleObject)
  if (pThis->m_lpOleAdviseHolder == NULL) return OLE_E_NOCONNECTION;
  return pThis->m_lpOleAdviseHolder->Unadvise(dwConnection);
}

STDMETHODIMP COleServerDoc::XOleObject::EnumAdvise(LPENUMSTATDATA* ppenumAdvise) {
  METHOD_PROLOGUE_EX_(COleServerDoc, OleObject)
  if (ppenumAdvise == NULL) return E_POINTER;
  *ppenumAdvise = NULL;
  if (pThis->m_lpOleAdviseHolder == NULL) return E_FAIL;
  return pThis->m_lpOleAdviseHolder->EnumAdvise(ppenumAdvise);
}

STDMETHODIMP COleServerDoc::XOleObject::GetMiscStatus(DWORD dwAspect, DWORD* pdwStatus) {
  METHOD_PROLOGUE_EX_(COleServerDoc, OleObject)
  CLSID clsid;
  pThis->GetClassIDOf(&clsid);
  return ::OleRegGetMiscStatus(clsid, dwAspect, pdwStatus);
}

STDMETHODIMP COleServerDoc::XOleObject::SetColorScheme(LPLOGPALETTE) { return E_NOTIMPL; }

// IDataObject: the embedded item's data, and the advise connections that
// keep the container's copy of it current.

static LPDATAOBJECT ItemDataObject(COleServerDoc* pDoc) {
  COleServerItem* pItem = NULL;
  try {
    pItem = pDoc->GetEmbeddedItem();
  } catch (CException* e) {
    e->Delete();
  }
  if (pItem == NULL) return NULL;
  return (LPDATAOBJECT)pItem->GetDataSource()->GetInterface(&IID_IDataObject);
}

STDMETHODIMP_(ULONG) COleServerDoc::XDataObject::AddRef() {
  METHOD_PROLOGUE_EX_(COleServerDoc, DataObject)
  return pThis->ExternalAddRef();
}

STDMETHODIMP_(ULONG) COleServerDoc::XDataObject::Release() {
  METHOD_PROLOGUE_EX_(COleServerDoc, DataObject)
  return pThis->ExternalRelease();
}

STDMETHODIMP COleServerDoc::XDataObject::QueryInterface(REFIID iid, LPVOID* ppvObj) {
  METHOD_PROLOGUE_EX_(COleServerDoc, DataObject)
  return pThis->ExternalQueryInterface(&iid, ppvObj);
}

// A request with no format is the object handler's cache asking for a
// picture to draw: it gets the metafile.
static LPFORMATETC PictureFormat(LPFORMATETC lpFormatEtc, FORMATETC& formatEtc) {
  if (lpFormatEtc == NULL || lpFormatEtc->cfFormat != 0) return lpFormatEtc;
  formatEtc = *lpFormatEtc;
  formatEtc.cfFormat = CF_METAFILEPICT;
  formatEtc.tymed = TYMED_MFPICT;
  return &formatEtc;
}

STDMETHODIMP COleServerDoc::XDataObject::GetData(LPFORMATETC pformatetcIn, LPSTGMEDIUM pmedium) {
  METHOD_PROLOGUE_EX_(COleServerDoc, DataObject)
  LPDATAOBJECT lpData = ItemDataObject(pThis);
  FORMATETC formatEtc;
  return lpData != NULL ? lpData->GetData(PictureFormat(pformatetcIn, formatEtc), pmedium)
                        : E_UNEXPECTED;
}

STDMETHODIMP COleServerDoc::XDataObject::GetDataHere(LPFORMATETC pformatetc,
                                                     LPSTGMEDIUM pmedium) {
  METHOD_PROLOGUE_EX_(COleServerDoc, DataObject)
  LPDATAOBJECT lpData = ItemDataObject(pThis);
  return lpData != NULL ? lpData->GetDataHere(pformatetc, pmedium) : E_UNEXPECTED;
}

STDMETHODIMP COleServerDoc::XDataObject::QueryGetData(LPFORMATETC pformatetc) {
  METHOD_PROLOGUE_EX_(COleServerDoc, DataObject)
  LPDATAOBJECT lpData = ItemDataObject(pThis);
  FORMATETC formatEtc;
  return lpData != NULL ? lpData->QueryGetData(PictureFormat(pformatetc, formatEtc))
                        : E_UNEXPECTED;
}

STDMETHODIMP COleServerDoc::XDataObject::GetCanonicalFormatEtc(LPFORMATETC pformatetcIn,
                                                               LPFORMATETC pformatetcOut) {
  METHOD_PROLOGUE_EX_(COleServerDoc, DataObject)
  LPDATAOBJECT lpData = ItemDataObject(pThis);
  return lpData != NULL ? lpData->GetCanonicalFormatEtc(pformatetcIn, pformatetcOut)
                        : E_UNEXPECTED;
}

STDMETHODIMP COleServerDoc::XDataObject::SetData(LPFORMATETC pformatetc, LPSTGMEDIUM pmedium,
                                                 BOOL fRelease) {
  METHOD_PROLOGUE_EX_(COleServerDoc, DataObject)
  LPDATAOBJECT lpData = ItemDataObject(pThis);
  return lpData != NULL ? lpData->SetData(pformatetc, pmedium, fRelease) : E_UNEXPECTED;
}

STDMETHODIMP COleServerDoc::XDataObject::EnumFormatEtc(DWORD dwDirection,
                                                       LPENUMFORMATETC* ppenumFormatEtc) {
  METHOD_PROLOGUE_EX_(COleServerDoc, DataObject)
  LPDATAOBJECT lpData = ItemDataObject(pThis);
  return lpData != NULL ? lpData->EnumFormatEtc(dwDirection, ppenumFormatEtc) : E_UNEXPECTED;
}

STDMETHODIMP COleServerDoc::XDataObject::DAdvise(LPFORMATETC pformatetc, DWORD advf,
                                                 LPADVISESINK pAdvSink, DWORD* pdwConnection) {
  METHOD_PROLOGUE_EX_(COleServerDoc, DataObject)
  if (pThis->m_lpDataAdviseHolder == NULL &&
      FAILED(::CreateDataAdviseHolder(&pThis->m_lpDataAdviseHolder)))
    return E_OUTOFMEMORY;
  return pThis->m_lpDataAdviseHolder->Advise((LPDATAOBJECT)this, pformatetc, advf, pAdvSink,
                                             pdwConnection);
}

STDMETHODIMP COleServerDoc::XDataObject::DUnadvise(DWORD dwConnection) {
  METHOD_PROLOGUE_EX_(COleServerDoc, DataObject)
  if (pThis->m_lpDataAdviseHolder == NULL) return OLE_E_NOCONNECTION;
  return pThis->m_lpDataAdviseHolder->Unadvise(dwConnection);
}

STDMETHODIMP COleServerDoc::XDataObject::EnumDAdvise(LPENUMSTATDATA* ppenumAdvise) {
  METHOD_PROLOGUE_EX_(COleServerDoc, DataObject)
  if (ppenumAdvise == NULL) return E_POINTER;
  *ppenumAdvise = NULL;
  if (pThis->m_lpDataAdviseHolder == NULL) return E_FAIL;
  return pThis->m_lpDataAdviseHolder->EnumAdvise(ppenumAdvise);
}

// ---------------------------------------------------------------------
// COleServerItem

BEGIN_INTERFACE_MAP(COleServerItem, CDocItem)
  INTERFACE_PART(COleServerItem, IID_IOleObject, OleObject)
  INTERFACE_PART(COleServerItem, IID_IDataObject, DataObject)
END_INTERFACE_MAP()

COleServerItem::COleServerItem(COleServerDoc* pServerDoc, BOOL bAutoDelete)
    : m_bAutoDelete(bAutoDelete),
      m_sizeExtent(0, 0),
      m_lpOleAdviseHolder(NULL),
      m_lpDataAdviseHolder(NULL),
      m_bLinkLocked(FALSE),
      m_dwRegister(0),
      m_lpRunning(NULL) {
  m_dataSource.m_pItem = this;
  if (pServerDoc != NULL) pServerDoc->AddItem(this);
  // The picture, drawn by OnDraw when a container asks for it.
  FORMATETC formatEtc = {CF_METAFILEPICT, NULL, DVASPECT_CONTENT, -1, TYMED_MFPICT};
  m_dataSource.DelayRenderData(CF_METAFILEPICT, &formatEtc);
}

COleServerItem::~COleServerItem() {
  RevokeLinked();
  if (m_lpOleAdviseHolder != NULL) m_lpOleAdviseHolder->Release();
  if (m_lpDataAdviseHolder != NULL) m_lpDataAdviseHolder->Release();
  COleServerDoc* pDoc = GetDocument();
  if (pDoc != NULL && pDoc->m_pEmbeddedItem == this) pDoc->m_pEmbeddedItem = NULL;
}

// The last link let go: the item goes back to its document, or goes, and
// lets the document close.
void COleServerItem::OnFinalRelease() {
  COleServerDoc* pDoc = m_bLinkLocked ? GetDocument() : NULL;
  m_bLinkLocked = FALSE;
  RevokeLinked();
  if (m_bAutoDelete) delete this;
  if (pDoc != NULL) pDoc->InternalRelease();
}

// The links' connections end; their references go with the stubs.
// A linked item is running while links hold it: the running object table
// has it under its full moniker, which links ask to see that it runs.
void COleServerItem::RegisterLinked() {
  if (m_dwRegister != 0) return;
  LPMONIKER lpMoniker = GetMoniker(OLEGETMONIKER_ONLYIFTHERE);
  if (lpMoniker == NULL) return;
  RegisterRunning(this, lpMoniker, m_dwRegister, m_lpRunning);
  lpMoniker->Release();
}

void COleServerItem::RevokeLinked() { RevokeRunning(m_dwRegister, m_lpRunning); }

void COleServerItem::Disconnect() {
  RevokeLinked();
  if (m_lpOleAdviseHolder != NULL) {
    m_lpOleAdviseHolder->Release();
    m_lpOleAdviseHolder = NULL;
  }
  if (m_lpDataAdviseHolder != NULL) {
    m_lpDataAdviseHolder->Release();
    m_lpDataAdviseHolder = NULL;
  }
  if (IsLinkedItem()) ::CoDisconnectObject((LPUNKNOWN)&m_xOleObject, 0);
}

BOOL COleServerItem::IsLinkedItem() const {
  COleServerDoc* pDoc = GetDocument();
  return pDoc == NULL || pDoc->m_pEmbeddedItem != this;
}

BOOL COleServerItem::IsConnected() const {
  COleServerDoc* pDoc = GetDocument();
  if (pDoc == NULL) return FALSE;
  if (!IsLinkedItem()) return pDoc->m_lpClientSite != NULL;
  return m_bLinkLocked || m_dwRef > 1;
}

void COleServerItem::NotifyChanged(DVASPECT) {
  if (m_lpDataAdviseHolder != NULL)
    m_lpDataAdviseHolder->SendOnDataChange((LPDATAOBJECT)&m_xDataObject, 0, 0);
  COleServerDoc* pDoc = GetDocument();
  if (pDoc != NULL && !IsLinkedItem() && pDoc->m_lpDataAdviseHolder != NULL)
    pDoc->m_lpDataAdviseHolder->SendOnDataChange((LPDATAOBJECT)&pDoc->m_xDataObject, 0, 0);
}

LPMONIKER COleServerItem::GetMoniker(OLEGETMONIKER nAssign) {
  COleServerDoc* pDoc = GetDocument();
  if (pDoc == NULL) return NULL;
  LPMONIKER lpDoc = pDoc->GetMoniker(nAssign);
  LPMONIKER lpMoniker = ItemMoniker(this, lpDoc);
  if (lpDoc != NULL) lpDoc->Release();
  return lpMoniker;
}

void COleServerItem::OnUpdate(COleServerItem*, LPARAM, CObject*, DVASPECT nDrawAspect) {
  NotifyChanged(nDrawAspect);
}

void COleServerItem::OnDoVerb(LONG iVerb) {
  switch (iVerb) {
  case OLEIVERB_HIDE:
    OnHide();
    break;
  case OLEIVERB_OPEN:
    OnOpen();
    break;
  case OLEIVERB_PRIMARY:
  case OLEIVERB_SHOW:
  case OLEIVERB_UIACTIVATE:
  case OLEIVERB_INPLACEACTIVATE:
    OnShow();
    break;
  default:
    // The program's own verbs show the item unless overridden; the other
    // standard verbs mean nothing here.
    if (iVerb < 0) AfxThrowOleException(E_NOTIMPL);
    OnShow();
    break;
  }
}

// Showing the item activates it in place where the container and the
// template allow, and opens it in a window otherwise. A link's item opens
// in the document's window.
void COleServerItem::OnShow() {
  COleServerDoc* pDoc = GetDocument();
  if (pDoc != NULL && !IsLinkedItem() && pDoc->ActivateInPlace()) return;
  OnOpen();
}

void COleServerItem::OnOpen() {
  COleServerDoc* pDoc = GetDocument();
  if (pDoc != NULL) pDoc->OnShowDocument(TRUE);
}

void COleServerItem::OnHide() {
  COleServerDoc* pDoc = GetDocument();
  if (pDoc == NULL) return;
  if (pDoc->IsInPlaceActive())
    pDoc->OnDeactivate();
  else
    pDoc->OnShowDocument(FALSE);
}

BOOL COleServerItem::OnDrawEx(CDC* pDC, DVASPECT nDrawAspect, CSize& rSize) {
  if (nDrawAspect != DVASPECT_CONTENT) return FALSE;
  return OnDraw(pDC, rSize);
}

BOOL COleServerItem::OnGetExtent(DVASPECT nDrawAspect, CSize& rSize) {
  if (nDrawAspect != DVASPECT_CONTENT || m_sizeExtent.cx == 0 || m_sizeExtent.cy == 0)
    return FALSE;
  rSize = m_sizeExtent;
  return TRUE;
}

BOOL COleServerItem::OnSetExtent(DVASPECT nDrawAspect, const CSize& size) {
  if (nDrawAspect != DVASPECT_CONTENT) return FALSE;
  m_sizeExtent = size;
  return TRUE;
}

BOOL COleServerItem::GetMetafileData(LPFORMATETC lpFormatEtc, LPSTGMEDIUM lpStgMedium) {
  if (!(lpFormatEtc->tymed & TYMED_MFPICT)) return FALSE;
  DVASPECT nAspect = (DVASPECT)lpFormatEtc->dwAspect;
  CSize size(0, 0);
  if (!OnGetExtent(nAspect, size)) size = CSize(0, 0);

  HDC hMetaDC = ::CreateMetaFile(NULL);
  if (hMetaDC == NULL) return FALSE;
  CDC dc;
  dc.Attach(hMetaDC);
  dc.SetMapMode(MM_ANISOTROPIC);
  dc.SetWindowOrg(0, 0);
  if (size.cx > 0 && size.cy > 0) dc.SetWindowExt(size);
  BOOL bDrawn = FALSE;
  try {
    bDrawn = OnDrawEx(&dc, nAspect, size);
  } catch (CException*) {
    dc.Detach();
    ::DeleteMetaFile(::CloseMetaFile(hMetaDC));
    throw;
  }
  dc.Detach();
  HMETAFILE hMF = ::CloseMetaFile(hMetaDC);
  if (!bDrawn || hMF == NULL) {
    if (hMF != NULL) ::DeleteMetaFile(hMF);
    return FALSE;
  }
  HGLOBAL hPict = ::GlobalAlloc(GMEM_MOVEABLE | GMEM_SHARE, sizeof(METAFILEPICT));
  if (hPict == NULL) {
    ::DeleteMetaFile(hMF);
    return FALSE;
  }
  METAFILEPICT* pPict = (METAFILEPICT*)::GlobalLock(hPict);
  pPict->mm = MM_ANISOTROPIC;
  pPict->xExt = size.cx;
  pPict->yExt = size.cy;
  pPict->hMF = hMF;
  ::GlobalUnlock(hPict);
  lpStgMedium->tymed = TYMED_MFPICT;
  lpStgMedium->hMetaFilePict = hPict;
  lpStgMedium->pUnkForRelease = NULL;
  return TRUE;
}

BOOL COleServerItem::OnRenderData(LPFORMATETC lpFormatEtc, LPSTGMEDIUM lpStgMedium) {
  if (lpFormatEtc->cfFormat == CF_METAFILEPICT && (lpFormatEtc->tymed & TYMED_MFPICT))
    return GetMetafileData(lpFormatEtc, lpStgMedium);
  // The global and file forms, through OnRenderGlobalData and OnRenderFileData.
  return m_dataSource.COleDataSource::OnRenderData(lpFormatEtc, lpStgMedium);
}

BOOL COleServerItem::OnRenderGlobalData(LPFORMATETC, HGLOBAL*) { return FALSE; }

BOOL COleServerItem::OnRenderFileData(LPFORMATETC, CFile*) { return FALSE; }

BOOL COleServerItem::OnSetData(LPFORMATETC, LPSTGMEDIUM, BOOL) { return FALSE; }

BOOL COleServerItem::CItemDataSource::OnRenderData(LPFORMATETC lpFormatEtc,
                                                   LPSTGMEDIUM lpStgMedium) {
  return m_pItem->OnRenderData(lpFormatEtc, lpStgMedium);
}

BOOL COleServerItem::CItemDataSource::OnRenderFileData(LPFORMATETC lpFormatEtc, CFile* pFile) {
  return m_pItem->OnRenderFileData(lpFormatEtc, pFile);
}

BOOL COleServerItem::CItemDataSource::OnRenderGlobalData(LPFORMATETC lpFormatEtc,
                                                         HGLOBAL* phGlobal) {
  return m_pItem->OnRenderGlobalData(lpFormatEtc, phGlobal);
}

BOOL COleServerItem::CItemDataSource::OnSetData(LPFORMATETC lpFormatEtc,
                                                LPSTGMEDIUM lpStgMedium, BOOL bRelease) {
  return m_pItem->OnSetData(lpFormatEtc, lpStgMedium, bRelease);
}

void COleServerItem::GetEmbedSourceData(LPSTGMEDIUM lpStgMedium) {
  COleServerDoc* pDoc = GetDocument();
  LPLOCKBYTES lpLockBytes = NULL;
  SCODE sc = ::CreateILockBytesOnHGlobal(NULL, TRUE, &lpLockBytes);
  if (FAILED(sc)) AfxThrowOleException(sc);
  LPSTORAGE lpStorage = NULL;
  sc = ::StgCreateDocfileOnILockBytes(
      lpLockBytes, STGM_SHARE_EXCLUSIVE | STGM_CREATE | STGM_READWRITE, 0, &lpStorage);
  lpLockBytes->Release();
  if (FAILED(sc)) AfxThrowOleException(sc);
  try {
    CLSID clsid;
    pDoc->GetClassIDOf(&clsid);
    sc = ::WriteClassStg(lpStorage, clsid);
    if (FAILED(sc)) AfxThrowOleException(sc);
    pDoc->OnSaveEmbedding(lpStorage);
    sc = lpStorage->Commit(STGC_OVERWRITE);
    if (FAILED(sc)) AfxThrowOleException(sc);
  } catch (CException*) {
    lpStorage->Release();
    throw;
  }
  lpStgMedium->tymed = TYMED_ISTORAGE;
  lpStgMedium->pstg = lpStorage;
  lpStgMedium->pUnkForRelease = NULL;
}

void COleServerItem::GetObjectDescriptorData(LPPOINT lpOffset, LPSIZE lpSize,
                                             LPSTGMEDIUM lpStgMedium) {
  COleServerDoc* pDoc = GetDocument();
  CLSID clsid;
  pDoc->GetClassIDOf(&clsid);
  CStringW strType;
  LPOLESTR lpszType = NULL;
  if (SUCCEEDED(::OleRegGetUserType(clsid, USERCLASSTYPE_FULL, &lpszType)) && lpszType != NULL) {
    strType = lpszType;
    ::CoTaskMemFree(lpszType);
  }
  CStringW strSource(pDoc->GetPathName().IsEmpty() ? pDoc->GetTitle() : pDoc->GetPathName());
  CSize size(0, 0);
  if (lpSize != NULL && (lpSize->cx != 0 || lpSize->cy != 0))
    size = *lpSize;
  else
    OnGetExtent(DVASPECT_CONTENT, size);

  UINT cbType = (strType.GetLength() + 1) * sizeof(WCHAR);
  UINT cbSource = (strSource.GetLength() + 1) * sizeof(WCHAR);
  HGLOBAL hDesc = ::GlobalAlloc(GMEM_MOVEABLE | GMEM_SHARE | GMEM_ZEROINIT,
                                sizeof(OBJECTDESCRIPTOR) + cbType + cbSource);
  if (hDesc == NULL) AfxThrowMemoryException();
  OBJECTDESCRIPTOR* pod = (OBJECTDESCRIPTOR*)::GlobalLock(hDesc);
  pod->cbSize = sizeof(OBJECTDESCRIPTOR) + cbType + cbSource;
  pod->clsid = clsid;
  pod->dwDrawAspect = DVASPECT_CONTENT;
  pod->sizel.cx = size.cx;
  pod->sizel.cy = size.cy;
  if (lpOffset != NULL) {
    pod->pointl.x = lpOffset->x;
    pod->pointl.y = lpOffset->y;
  }
  ::OleRegGetMiscStatus(clsid, DVASPECT_CONTENT, &pod->dwStatus);
  pod->dwFullUserTypeName = sizeof(OBJECTDESCRIPTOR);
  pod->dwSrcOfCopy = sizeof(OBJECTDESCRIPTOR) + cbType;
  memcpy((BYTE*)pod + pod->dwFullUserTypeName, (LPCWSTR)strType, cbType);
  memcpy((BYTE*)pod + pod->dwSrcOfCopy, (LPCWSTR)strSource, cbSource);
  ::GlobalUnlock(hDesc);
  lpStgMedium->tymed = TYMED_HGLOBAL;
  lpStgMedium->hGlobal = hDesc;
  lpStgMedium->pUnkForRelease = NULL;
}

void COleServerItem::AddOtherClipboardData(COleDataSource* pDataSource) {
  FORMATETC formatEtc = {CF_METAFILEPICT, NULL, DVASPECT_CONTENT, -1, TYMED_MFPICT};
  STGMEDIUM stgMedium;
  if (GetMetafileData(&formatEtc, &stgMedium))
    pDataSource->CacheData(CF_METAFILEPICT, &stgMedium, &formatEtc);
}

BOOL COleServerItem::GetLinkSourceData(LPSTGMEDIUM lpStgMedium) {
  LPMONIKER lpMoniker = GetMoniker(OLEGETMONIKER_FORCEASSIGN);
  if (lpMoniker == NULL) return FALSE;
  LPSTREAM lpStream = NULL;
  SCODE sc = ::CreateStreamOnHGlobal(NULL, TRUE, &lpStream);
  if (SUCCEEDED(sc)) {
    sc = ::OleSaveToStream((LPPERSISTSTREAM)lpMoniker, lpStream);
    if (SUCCEEDED(sc)) {
      CLSID clsid;
      GetDocument()->GetClassIDOf(&clsid);
      sc = ::WriteClassStm(lpStream, clsid);
    }
  }
  lpMoniker->Release();
  if (FAILED(sc)) {
    if (lpStream != NULL) lpStream->Release();
    return FALSE;
  }
  LARGE_INTEGER zero = {};
  lpStream->Seek(zero, STREAM_SEEK_SET, NULL);
  lpStgMedium->tymed = TYMED_ISTREAM;
  lpStgMedium->pstm = lpStream;
  lpStgMedium->pUnkForRelease = NULL;
  return TRUE;
}

void COleServerItem::GetClipboardData(COleDataSource* pDataSource, BOOL bIncludeLink,
                                      LPPOINT lpOffset, LPSIZE lpSize) {
  FORMATETC formatEtc = {CF_EMBEDSOURCE_, NULL, DVASPECT_CONTENT, -1, TYMED_ISTORAGE};
  STGMEDIUM stgMedium;
  GetEmbedSourceData(&stgMedium);
  pDataSource->CacheData(CF_EMBEDSOURCE_, &stgMedium, &formatEtc);
  GetObjectDescriptorData(lpOffset, lpSize, &stgMedium);
  pDataSource->CacheData(CF_OBJECTDESCRIPTOR_, &stgMedium);
  // What Paste Link makes a link from, when the document has a file.
  if (bIncludeLink && GetLinkSourceData(&stgMedium)) {
    FORMATETC feLink = {CF_LINKSOURCE_, NULL, DVASPECT_CONTENT, -1, TYMED_ISTREAM};
    pDataSource->CacheData(CF_LINKSOURCE_, &stgMedium, &feLink);
    GetObjectDescriptorData(lpOffset, lpSize, &stgMedium);
    pDataSource->CacheData(CF_LINKSRCDESCRIPTOR_, &stgMedium);
  }
  AddOtherClipboardData(pDataSource);
}

void COleServerItem::CopyToClipboard(BOOL bIncludeLink) {
  COleDataSource* pDataSource = new COleDataSource;
  try {
    GetClipboardData(pDataSource, bIncludeLink, NULL, NULL);
  } catch (CException*) {
    pDataSource->InternalRelease();
    throw;
  }
  pDataSource->SetClipboard();
}

DROPEFFECT COleServerItem::DoDragDrop(LPCRECT lpItemRect, CPoint ptOffset, BOOL bIncludeLink,
                                      DWORD dwEffects, LPCRECT lpRectStartDrag) {
  CSize size(0, 0);
  CSize offset(ptOffset.x, ptOffset.y);
  {
    // The descriptor wants HIMETRIC.
    CClientDC dc(NULL);
    if (lpItemRect != NULL) {
      size = CSize(lpItemRect->right - lpItemRect->left, lpItemRect->bottom - lpItemRect->top);
      dc.DPtoHIMETRIC(&size);
    }
    dc.DPtoHIMETRIC(&offset);
  }
  CPoint ptHimetric(offset.cx, offset.cy);
  COleDataSource* pDataSource = new COleDataSource;
  DROPEFFECT dropEffect = DROPEFFECT_NONE;
  try {
    GetClipboardData(pDataSource, bIncludeLink, &ptHimetric, &size);
    dropEffect = pDataSource->DoDragDrop(dwEffects, lpRectStartDrag);
  } catch (CException*) {
    pDataSource->InternalRelease();
    throw;
  }
  pDataSource->InternalRelease();
  return dropEffect;
}

// COleServerItem's IOleObject and IDataObject: what a link to one of the
// document's items binds to (the embedded item is the document's).

STDMETHODIMP_(ULONG) COleServerItem::XOleObject::AddRef() {
  METHOD_PROLOGUE_EX_(COleServerItem, OleObject)
  return pThis->ExternalAddRef();
}

STDMETHODIMP_(ULONG) COleServerItem::XOleObject::Release() {
  METHOD_PROLOGUE_EX_(COleServerItem, OleObject)
  return pThis->ExternalRelease();
}

STDMETHODIMP COleServerItem::XOleObject::QueryInterface(REFIID iid, LPVOID* ppvObj) {
  METHOD_PROLOGUE_EX_(COleServerItem, OleObject)
  return pThis->ExternalQueryInterface(&iid, ppvObj);
}

// A link has no client site of the object's own.
STDMETHODIMP COleServerItem::XOleObject::SetClientSite(LPOLECLIENTSITE) { return S_OK; }

STDMETHODIMP COleServerItem::XOleObject::GetClientSite(LPOLECLIENTSITE* ppClientSite) {
  if (ppClientSite == NULL) return E_POINTER;
  *ppClientSite = NULL;
  return E_NOTIMPL;
}

STDMETHODIMP COleServerItem::XOleObject::SetHostNames(LPCOLESTR, LPCOLESTR) { return S_OK; }

// Closing a link's item leaves the document open.
STDMETHODIMP COleServerItem::XOleObject::Close(DWORD) { return S_OK; }

STDMETHODIMP COleServerItem::XOleObject::SetMoniker(DWORD, LPMONIKER) { return S_OK; }

STDMETHODIMP COleServerItem::XOleObject::GetMoniker(DWORD dwAssign, DWORD dwWhichMoniker,
                                                    LPMONIKER* ppmk) {
  METHOD_PROLOGUE_EX_(COleServerItem, OleObject)
  if (ppmk == NULL) return E_POINTER;
  *ppmk = NULL;
  COleServerDoc* pDoc = pThis->GetDocument();
  if (pDoc == NULL) return E_UNEXPECTED;
  HRESULT hr = MK_E_UNAVAILABLE;
  OLE_TRY
    switch (dwWhichMoniker) {
      case OLEWHICHMK_CONTAINER:
        *ppmk = pDoc->GetMoniker((OLEGETMONIKER)dwAssign);
        break;
      case OLEWHICHMK_OBJREL:
        if (!pThis->GetItemName().IsEmpty()) {
          CStringW strName(pThis->GetItemName());
          if (FAILED(::CreateItemMoniker(L"!", strName, ppmk))) *ppmk = NULL;
        }
        break;
      default:
        *ppmk = pThis->GetMoniker((OLEGETMONIKER)dwAssign);
        break;
    }
    if (*ppmk != NULL) hr = S_OK;
  OLE_CATCH(hr)
  return hr;
}

STDMETHODIMP COleServerItem::XOleObject::InitFromData(LPDATAOBJECT, BOOL, DWORD) {
  return E_NOTIMPL;
}

STDMETHODIMP COleServerItem::XOleObject::GetClipboardData(DWORD, LPDATAOBJECT* ppDataObject) {
  METHOD_PROLOGUE_EX_(COleServerItem, OleObject)
  if (ppDataObject == NULL) return E_POINTER;
  *ppDataObject = NULL;
  HRESULT hr = S_OK;
  COleDataSource* pDataSource = NULL;
  OLE_TRY
    pDataSource = new COleDataSource;
    pThis->GetClipboardData(pDataSource, FALSE, NULL, NULL);
    *ppDataObject = (LPDATAOBJECT)pDataSource->GetInterface(&IID_IDataObject);
    pDataSource = NULL;  // its first reference is the caller's
  OLE_CATCH(hr)
  if (pDataSource != NULL) pDataSource->InternalRelease();
  return hr;
}

STDMETHODIMP COleServerItem::XOleObject::DoVerb(LONG iVerb, LPMSG, LPOLECLIENTSITE, LONG, HWND,
                                                LPCRECT) {
  METHOD_PROLOGUE_EX_(COleServerItem, OleObject)
  HRESULT hr = S_OK;
  pThis->InternalAddRef();
  OLE_TRY
    pThis->OnDoVerb(iVerb);
  OLE_CATCH(hr)
  pThis->InternalRelease();
  return hr;
}

STDMETHODIMP COleServerItem::XOleObject::EnumVerbs(LPENUMOLEVERB* ppEnumOleVerb) {
  METHOD_PROLOGUE_EX_(COleServerItem, OleObject)
  if (pThis->GetDocument() == NULL) return E_UNEXPECTED;
  CLSID clsid;
  pThis->GetDocument()->GetClassIDOf(&clsid);
  return ::OleRegEnumVerbs(clsid, ppEnumOleVerb);
}

STDMETHODIMP COleServerItem::XOleObject::Update() {
  METHOD_PROLOGUE_EX_(COleServerItem, OleObject)
  pThis->NotifyChanged();
  return S_OK;
}

STDMETHODIMP COleServerItem::XOleObject::IsUpToDate() { return S_OK; }

STDMETHODIMP COleServerItem::XOleObject::GetUserClassID(LPCLSID pClsid) {
  METHOD_PROLOGUE_EX_(COleServerItem, OleObject)
  if (pClsid == NULL) return E_POINTER;
  if (pThis->GetDocument() == NULL) return E_UNEXPECTED;
  pThis->GetDocument()->GetClassIDOf(pClsid);
  return S_OK;
}

STDMETHODIMP COleServerItem::XOleObject::GetUserType(DWORD dwFormOfType, LPOLESTR* pszUserType) {
  METHOD_PROLOGUE_EX_(COleServerItem, OleObject)
  if (pThis->GetDocument() == NULL) return E_UNEXPECTED;
  CLSID clsid;
  pThis->GetDocument()->GetClassIDOf(&clsid);
  return ::OleRegGetUserType(clsid, dwFormOfType, pszUserType);
}

STDMETHODIMP COleServerItem::XOleObject::SetExtent(DWORD dwDrawAspect, LPSIZEL psizel) {
  METHOD_PROLOGUE_EX_(COleServerItem, OleObject)
  if (psizel == NULL) return E_POINTER;
  HRESULT hr = E_FAIL;
  OLE_TRY
    if (pThis->OnSetExtent((DVASPECT)dwDrawAspect, CSize(psizel->cx, psizel->cy))) hr = S_OK;
  OLE_CATCH(hr)
  return hr;
}

STDMETHODIMP COleServerItem::XOleObject::GetExtent(DWORD dwDrawAspect, LPSIZEL psizel) {
  METHOD_PROLOGUE_EX_(COleServerItem, OleObject)
  if (psizel == NULL) return E_POINTER;
  HRESULT hr = E_FAIL;
  OLE_TRY
    CSize size(0, 0);
    if (pThis->OnGetExtent((DVASPECT)dwDrawAspect, size)) {
      psizel->cx = size.cx;
      psizel->cy = size.cy;
      hr = S_OK;
    }
  OLE_CATCH(hr)
  return hr;
}

STDMETHODIMP COleServerItem::XOleObject::Advise(LPADVISESINK pAdvSink, DWORD* pdwConnection) {
  METHOD_PROLOGUE_EX_(COleServerItem, OleObject)
  if (pThis->m_lpOleAdviseHolder == NULL &&
      FAILED(::CreateOleAdviseHolder(&pThis->m_lpOleAdviseHolder)))
    return E_OUTOFMEMORY;
  return pThis->m_lpOleAdviseHolder->Advise(pAdvSink, pdwConnection);
}

STDMETHODIMP COleServerItem::XOleObject::Unadvise(DWORD dwConnection) {
  METHOD_PROLOGUE_EX_(COleServerItem, OleObject)
  if (pThis->m_lpOleAdviseHolder == NULL) return OLE_E_NOCONNECTION;
  return pThis->m_lpOleAdviseHolder->Unadvise(dwConnection);
}

STDMETHODIMP COleServerItem::XOleObject::EnumAdvise(LPENUMSTATDATA* ppenumAdvise) {
  METHOD_PROLOGUE_EX_(COleServerItem, OleObject)
  if (ppenumAdvise == NULL) return E_POINTER;
  *ppenumAdvise = NULL;
  if (pThis->m_lpOleAdviseHolder == NULL) return E_FAIL;
  return pThis->m_lpOleAdviseHolder->EnumAdvise(ppenumAdvise);
}

STDMETHODIMP COleServerItem::XOleObject::GetMiscStatus(DWORD dwAspect, DWORD* pdwStatus) {
  METHOD_PROLOGUE_EX_(COleServerItem, OleObject)
  if (pThis->GetDocument() == NULL) return E_UNEXPECTED;
  CLSID clsid;
  pThis->GetDocument()->GetClassIDOf(&clsid);
  return ::OleRegGetMiscStatus(clsid, dwAspect, pdwStatus);
}

STDMETHODIMP COleServerItem::XOleObject::SetColorScheme(LPLOGPALETTE) { return E_NOTIMPL; }

// IDataObject: the item's data source, and the link's advise connections.

STDMETHODIMP_(ULONG) COleServerItem::XDataObject::AddRef() {
  METHOD_PROLOGUE_EX_(COleServerItem, DataObject)
  return pThis->ExternalAddRef();
}

STDMETHODIMP_(ULONG) COleServerItem::XDataObject::Release() {
  METHOD_PROLOGUE_EX_(COleServerItem, DataObject)
  return pThis->ExternalRelease();
}

STDMETHODIMP COleServerItem::XDataObject::QueryInterface(REFIID iid, LPVOID* ppvObj) {
  METHOD_PROLOGUE_EX_(COleServerItem, DataObject)
  return pThis->ExternalQueryInterface(&iid, ppvObj);
}

static LPDATAOBJECT SourceOf(COleServerItem* pItem) {
  return (LPDATAOBJECT)pItem->GetDataSource()->GetInterface(&IID_IDataObject);
}

STDMETHODIMP COleServerItem::XDataObject::GetData(LPFORMATETC pformatetcIn, LPSTGMEDIUM pmedium) {
  METHOD_PROLOGUE_EX_(COleServerItem, DataObject)
  FORMATETC formatEtc;
  return SourceOf(pThis)->GetData(PictureFormat(pformatetcIn, formatEtc), pmedium);
}

STDMETHODIMP COleServerItem::XDataObject::GetDataHere(LPFORMATETC pformatetc,
                                                      LPSTGMEDIUM pmedium) {
  METHOD_PROLOGUE_EX_(COleServerItem, DataObject)
  return SourceOf(pThis)->GetDataHere(pformatetc, pmedium);
}

STDMETHODIMP COleServerItem::XDataObject::QueryGetData(LPFORMATETC pformatetc) {
  METHOD_PROLOGUE_EX_(COleServerItem, DataObject)
  FORMATETC formatEtc;
  return SourceOf(pThis)->QueryGetData(PictureFormat(pformatetc, formatEtc));
}

STDMETHODIMP COleServerItem::XDataObject::GetCanonicalFormatEtc(LPFORMATETC pformatetcIn,
                                                                LPFORMATETC pformatetcOut) {
  METHOD_PROLOGUE_EX_(COleServerItem, DataObject)
  return SourceOf(pThis)->GetCanonicalFormatEtc(pformatetcIn, pformatetcOut);
}

STDMETHODIMP COleServerItem::XDataObject::SetData(LPFORMATETC pformatetc, LPSTGMEDIUM pmedium,
                                                  BOOL fRelease) {
  METHOD_PROLOGUE_EX_(COleServerItem, DataObject)
  return SourceOf(pThis)->SetData(pformatetc, pmedium, fRelease);
}

STDMETHODIMP COleServerItem::XDataObject::EnumFormatEtc(DWORD dwDirection,
                                                        LPENUMFORMATETC* ppenumFormatEtc) {
  METHOD_PROLOGUE_EX_(COleServerItem, DataObject)
  return SourceOf(pThis)->EnumFormatEtc(dwDirection, ppenumFormatEtc);
}

STDMETHODIMP COleServerItem::XDataObject::DAdvise(LPFORMATETC pformatetc, DWORD advf,
                                                  LPADVISESINK pAdvSink, DWORD* pdwConnection) {
  METHOD_PROLOGUE_EX_(COleServerItem, DataObject)
  if (pThis->m_lpDataAdviseHolder == NULL &&
      FAILED(::CreateDataAdviseHolder(&pThis->m_lpDataAdviseHolder)))
    return E_OUTOFMEMORY;
  return pThis->m_lpDataAdviseHolder->Advise((LPDATAOBJECT)this, pformatetc, advf, pAdvSink,
                                             pdwConnection);
}

STDMETHODIMP COleServerItem::XDataObject::DUnadvise(DWORD dwConnection) {
  METHOD_PROLOGUE_EX_(COleServerItem, DataObject)
  if (pThis->m_lpDataAdviseHolder == NULL) return OLE_E_NOCONNECTION;
  return pThis->m_lpDataAdviseHolder->Unadvise(dwConnection);
}

STDMETHODIMP COleServerItem::XDataObject::EnumDAdvise(LPENUMSTATDATA* ppenumAdvise) {
  METHOD_PROLOGUE_EX_(COleServerItem, DataObject)
  if (ppenumAdvise == NULL) return E_POINTER;
  *ppenumAdvise = NULL;
  if (pThis->m_lpDataAdviseHolder == NULL) return E_FAIL;
  return pThis->m_lpDataAdviseHolder->EnumAdvise(ppenumAdvise);
}

// ---------------------------------------------------------------------
// COleTemplateServer

COleTemplateServer::COleTemplateServer()
    : COleObjectFactory(CLSID_NULL, NULL, FALSE, NULL),
      m_pDocTemplate(NULL),
      m_nAppType(OAT_UNKNOWN) {}

void COleTemplateServer::ConnectTemplate(REFCLSID clsid, CDocTemplate* pDocTemplate,
                                         BOOL bMultiInstance) {
  m_clsid = clsid;
  m_pDocTemplate = pDocTemplate;
  m_bMultiInstance = bMultiInstance;
  if (pDocTemplate != NULL) pDocTemplate->m_pAttachedFactory = this;
}

CCmdTarget* COleTemplateServer::OnCreateObject() {
  if (m_pDocTemplate == NULL) return NULL;
  // A new document, its frame hidden until a verb shows it.
  CDocument* pDoc = m_pDocTemplate->OpenDocumentFile(NULL, FALSE);
  if (pDoc == NULL) return NULL;
  if (pDoc->IsKindOf(RUNTIME_CLASS(COleServerDoc))) {
    COleServerDoc* pServerDoc = (COleServerDoc*)pDoc;
    pServerDoc->m_pFactory = this;
    // The reference it starts with goes to the container.
    pServerDoc->m_bOwnRef = FALSE;
  }
  return pDoc;
}

static BOOL SetClassKey(LPCTSTR lpszKey, LPCTSTR lpszValue, LPCTSTR lpszValueName = NULL) {
  HKEY hKey;
  if (::RegCreateKeyEx(HKEY_CLASSES_ROOT, lpszKey, 0, NULL, 0, KEY_WRITE, NULL, &hKey, NULL) !=
      ERROR_SUCCESS)
    return FALSE;
  LONG l = ::RegSetValueEx(hKey, lpszValueName, 0, REG_SZ, (const BYTE*)lpszValue,
                           (DWORD)((lstrlen(lpszValue) + 1) * sizeof(TCHAR)));
  ::RegCloseKey(hKey);
  return l == ERROR_SUCCESS;
}

static void DeleteClassTree(HKEY hParent, LPCTSTR lpszKey) {
  HKEY hKey;
  if (::RegOpenKeyEx(hParent, lpszKey, 0, KEY_READ | KEY_WRITE, &hKey) != ERROR_SUCCESS) return;
  TCHAR szName[256];
  while (::RegEnumKey(hKey, 0, szName, _countof(szName)) == ERROR_SUCCESS)
    DeleteClassTree(hKey, szName);
  ::RegCloseKey(hKey);
  ::RegDeleteKey(hParent, lpszKey);
}

static CString ClassIDString(REFCLSID clsid) {
  LPOLESTR psz = NULL;
  if (FAILED(::StringFromCLSID(clsid, &psz))) return CString();
  CString str(psz);
  ::CoTaskMemFree(psz);
  return str;
}

void COleTemplateServer::UpdateRegistry(OLE_APPTYPE nAppType, LPCTSTR*, LPCTSTR*,
                                        BOOL bRegister) {
  m_nAppType = nAppType;
  if (!bRegister) {
    Unregister();
    return;
  }
  if (m_pDocTemplate == NULL) return;
  CString strClsid = ClassIDString(m_clsid);
  if (strClsid.IsEmpty()) return;

  CString strProgID, strName, strShort;
  m_pDocTemplate->GetDocString(strProgID, CDocTemplate::regFileTypeId);
  m_pDocTemplate->GetDocString(strName, CDocTemplate::regFileTypeName);
  m_pDocTemplate->GetDocString(strShort, CDocTemplate::fileNewName);
  if (strName.IsEmpty()) strName = strProgID;
  if (strShort.IsEmpty()) strShort = strName;

  TCHAR szPath[MAX_PATH];
  ::GetModuleFileName(AfxGetInstanceHandle(), szPath, MAX_PATH);
  CString strPath = szPath;
  if (strPath.Find(_T(' ')) >= 0) strPath = _T("\"") + strPath + _T("\"");
  BOOL bServer = nAppType == OAT_INPLACE_SERVER || nAppType == OAT_SERVER ||
                 nAppType == OAT_DOC_OBJECT_SERVER;

  CString strKey = _T("CLSID\\") + strClsid;
  SetClassKey(strKey, strName);
  if (!strProgID.IsEmpty()) {
    SetClassKey(strProgID, strName);
    SetClassKey(strProgID + _T("\\CLSID"), strClsid);
    SetClassKey(strKey + _T("\\ProgID"), strProgID);
    if (bServer) SetClassKey(strProgID + _T("\\Insertable"), _T(""));
  }
  SetClassKey(strKey + _T("\\LocalServer32"),
              nAppType == OAT_DISPATCH_OBJECT ? strPath + _T(" /Automation") : strPath);
  if (nAppType == OAT_DISPATCH_OBJECT) return;
  SetClassKey(strKey + _T("\\InprocHandler32"), _T("ole32.dll"));
  SetClassKey(strKey + _T("\\DefaultIcon"), strPath + _T(",0"));
  SetClassKey(strKey + _T("\\AuxUserType\\2"), strShort);
  SetClassKey(strKey + _T("\\AuxUserType\\3"), AfxGetAppName());
  if (!bServer) return;
  SetClassKey(strKey + _T("\\Insertable"), _T(""));
  SetClassKey(strKey + _T("\\MiscStatus"), _T("32"));  // OLEMISC_RECOMPOSEONRESIZE
  SetClassKey(strKey + _T("\\verb\\0"), _T("&Edit,0,2"));
  if (nAppType != OAT_SERVER) SetClassKey(strKey + _T("\\verb\\1"), _T("&Open,0,2"));
}

BOOL COleTemplateServer::UpdateRegistry(BOOL bRegister) {
  if (m_pDocTemplate == NULL) return COleObjectFactory::UpdateRegistry(bRegister);
  UpdateRegistry(m_nAppType == OAT_UNKNOWN ? OAT_INPLACE_SERVER : m_nAppType, NULL, NULL,
                 bRegister);
  return TRUE;
}

BOOL COleTemplateServer::Unregister() {
  CString strClsid = ClassIDString(m_clsid);
  if (strClsid.IsEmpty()) return FALSE;
  DeleteClassTree(HKEY_CLASSES_ROOT, _T("CLSID\\") + strClsid);
  if (m_pDocTemplate != NULL) {
    CString strProgID;
    m_pDocTemplate->GetDocString(strProgID, CDocTemplate::regFileTypeId);
    if (!strProgID.IsEmpty()) DeleteClassTree(HKEY_CLASSES_ROOT, strProgID);
  }
  return TRUE;
}
