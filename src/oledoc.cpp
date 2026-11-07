// oledoc.cpp - CDocItem and COleDocument: documents that hold linked and
// embedded objects, saved to flat files or to compound files.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include "afximpl.h"
#include <afxole.h>

IMPLEMENT_SERIAL(CDocItem, CCmdTarget, 0)
IMPLEMENT_DYNAMIC(COleDocument, CDocument)

void AFXAPI _AfxThrowOleFileException(SCODE sc);

// ---------------------------------------------------------------------
// CDocItem

CDocItem::CDocItem() : m_pDocument(NULL) {}

CDocItem::~CDocItem() {
  if (m_pDocument != NULL) m_pDocument->RemoveItem(this);
}

BOOL CDocItem::IsBlank() const { return FALSE; }

// ---------------------------------------------------------------------
// COleDocument

BEGIN_MESSAGE_MAP(COleDocument, CDocument)
END_MESSAGE_MAP()


COleDocument::COleDocument()
    : m_lpRootStg(NULL), m_bCompoundFile(FALSE), m_bSameAsLoad(TRUE), m_bRemember(TRUE),
      m_bRootIsTemp(FALSE), m_dwNextItemNumber(1), m_ptd(NULL) {}

COleDocument::~COleDocument() {
  while (!m_docItemList.IsEmpty()) {
    CDocItem* pItem = (CDocItem*)m_docItemList.GetHead();
    delete pItem;  // removes itself
  }
  ReleaseRoot();
  if (m_ptd != NULL) ::CoTaskMemFree(m_ptd);
}

void COleDocument::ReleaseRoot() {
  if (m_lpRootStg != NULL) {
    m_lpRootStg->Release();
    m_lpRootStg = NULL;
  }
  m_bRootIsTemp = FALSE;
}

void COleDocument::EnableCompoundFile(BOOL bEnable) { m_bCompoundFile = bEnable; }

void COleDocument::AddItem(CDocItem* pItem) {
  if (pItem->m_pDocument == this) return;
  if (pItem->m_pDocument != NULL) pItem->m_pDocument->RemoveItem(pItem);
  m_docItemList.AddTail(pItem);
  pItem->m_pDocument = this;
}

void COleDocument::RemoveItem(CDocItem* pItem) {
  POSITION pos = m_docItemList.Find(pItem);
  if (pos != NULL) m_docItemList.RemoveAt(pos);
  pItem->m_pDocument = NULL;
}

COleClientItem* COleDocument::GetNextClientItem(POSITION& pos) const {
  while (pos != NULL) {
    CDocItem* pItem = GetNextItem(pos);
    if (pItem->IsKindOf(RUNTIME_CLASS(COleClientItem))) return (COleClientItem*)pItem;
  }
  return NULL;
}

COleClientItem* COleDocument::GetInPlaceActiveItem(CWnd* pWnd) {
  POSITION pos = GetStartPosition();
  while (pos != NULL) {
    COleClientItem* pItem = GetNextClientItem(pos);
    if (pItem != NULL && pItem->IsInPlaceActive() &&
        (pWnd == NULL || pItem->m_pView == (CView*)pWnd))
      return pItem;
  }
  return NULL;
}

COleClientItem* COleDocument::GetPrimarySelectedItem(CView* pView) {
  if (pView == NULL) return NULL;
  COleClientItem* pSelected = NULL;
  POSITION pos = GetStartPosition();
  while (pos != NULL) {
    COleClientItem* pItem = GetNextClientItem(pos);
    if (pItem != NULL && pView->IsSelected(pItem)) {
      if (pSelected != NULL) return NULL;  // more than one
      pSelected = pItem;
    }
  }
  return pSelected;
}

BOOL COleDocument::HasBlankItems() const {
  POSITION pos = GetStartPosition();
  while (pos != NULL)
    if (GetNextItem(pos)->IsBlank()) return TRUE;
  return FALSE;
}

void COleDocument::UpdateModifiedFlag() {
  POSITION pos = GetStartPosition();
  while (pos != NULL) {
    COleClientItem* pItem = GetNextClientItem(pos);
    if (pItem != NULL && pItem->IsModified()) {
      SetModifiedFlag();
      return;
    }
  }
}

void COleDocument::OnShowViews(BOOL) {}

BOOL COleDocument::ApplyPrintDevice(const DVTARGETDEVICE* ptd) {
  if (m_ptd != NULL) {
    ::CoTaskMemFree(m_ptd);
    m_ptd = NULL;
  }
  if (ptd != NULL) {
    m_ptd = (DVTARGETDEVICE*)::CoTaskMemAlloc(ptd->tdSize);
    if (m_ptd == NULL) return FALSE;
    memcpy(m_ptd, ptd, ptd->tdSize);
  }
  POSITION pos = GetStartPosition();
  while (pos != NULL) {
    COleClientItem* pItem = GetNextClientItem(pos);
    if (pItem != NULL) pItem->SetPrintDevice(m_ptd);
  }
  return TRUE;
}

DVTARGETDEVICE* AFXAPI _AfxOleCreateTargetDevice(const PRINTDLG* ppd);

BOOL COleDocument::ApplyPrintDevice(const PRINTDLG* ppd) {
  DVTARGETDEVICE* ptd = _AfxOleCreateTargetDevice(ppd);
  BOOL bResult = ApplyPrintDevice(ptd);
  if (ptd != NULL) ::CoTaskMemFree(ptd);
  return bResult;
}

// The items: a count, then each item with its class.
void COleDocument::Serialize(CArchive& ar) {
  if (ar.IsStoring()) {
    DWORD n = 0;
    POSITION pos = GetStartPosition();
    while (pos != NULL)
      if (!GetNextItem(pos)->IsBlank()) n++;
    ar << n;
    pos = GetStartPosition();
    while (pos != NULL) {
      CDocItem* pItem = GetNextItem(pos);
      if (!pItem->IsBlank()) ar << pItem;
    }
  } else {
    DWORD n;
    ar >> n;
    for (DWORD i = 0; i < n; i++) {
      CObject* pObject = ar.ReadObject(RUNTIME_CLASS(CDocItem));
      // An item that does not add itself while loading belongs here too.
      if (pObject != NULL) AddItem((CDocItem*)pObject);
    }
  }
}

void COleDocument::DeleteContents() {
  POSITION pos = GetStartPosition();
  while (pos != NULL) {
    CDocItem* pItem = GetNextItem(pos);
    if (pItem->IsKindOf(RUNTIME_CLASS(COleClientItem))) {
      ((COleClientItem*)pItem)->Release();
      delete pItem;
    }
  }
  m_dwNextItemNumber = 1;
}

LPMONIKER COleDocument::GetMoniker(OLEGETMONIKER) { return NULL; }

BOOL COleDocument::OnNewDocument() {
  if (!CDocument::OnNewDocument()) return FALSE;
  ReleaseRoot();
  if (m_bCompoundFile) {
    // Items need a storage before the document has a file.
    SCODE sc = ::StgCreateDocfile(NULL,
                                  STGM_CREATE | STGM_READWRITE | STGM_SHARE_EXCLUSIVE |
                                      STGM_DELETEONRELEASE,
                                  0, &m_lpRootStg);
    if (FAILED(sc)) {
      m_lpRootStg = NULL;
      return FALSE;
    }
    m_bRootIsTemp = TRUE;
  }
  return TRUE;
}

BOOL COleDocument::OnOpenDocument(LPCTSTR lpszPathName) {
  if (!m_bCompoundFile) {
    ReleaseRoot();
    return CDocument::OnOpenDocument(lpszPathName);
  }

  // The file the document has open already would refuse to open again.
  if (m_lpRootStg != NULL && !m_bRootIsTemp && !m_strPathName.IsEmpty() &&
      AfxComparePath(m_strPathName, lpszPathName)) {
    DeleteContents();
    ReleaseRoot();
  }

  CStringW strPath(lpszPathName);
  LPSTORAGE lpStorage = NULL;
  SCODE sc = ::StgOpenStorage(strPath, NULL,
                              STGM_READWRITE | STGM_TRANSACTED | STGM_SHARE_DENY_WRITE, NULL,
                              0, &lpStorage);
  if (FAILED(sc))
    sc = ::StgOpenStorage(strPath, NULL, STGM_READ | STGM_TRANSACTED | STGM_SHARE_DENY_WRITE,
                          NULL, 0, &lpStorage);
  if (FAILED(sc)) {
    CFileException fe;
    fe.m_cause = sc == STG_E_FILENOTFOUND   ? CFileException::fileNotFound
                 : sc == STG_E_ACCESSDENIED ? CFileException::accessDenied
                 : sc == STG_E_SHAREVIOLATION ? CFileException::sharingViolation
                                             : CFileException::invalidFile;
    fe.m_lOsError = sc;
    fe.m_strFileName = lpszPathName;
    ReportSaveLoadException(lpszPathName, &fe, FALSE, AFX_IDP_FAILED_TO_OPEN_DOC);
    return FALSE;
  }

  DeleteContents();
  ReleaseRoot();
  m_lpRootStg = lpStorage;
  SetModifiedFlag();
  try {
    CWaitCursor wait;
    LoadFromStorage();
  } catch (CException* e) {
    DeleteContents();
    ReleaseRoot();
    ReportSaveLoadException(lpszPathName, e, FALSE, AFX_IDP_FAILED_TO_OPEN_DOC);
    e->Delete();
    return FALSE;
  }
  SetModifiedFlag(FALSE);
  return TRUE;
}

void COleDocument::LoadFromStorage() {
  COleStreamFile file;
  CFileException fe;
  if (!file.OpenStream(m_lpRootStg, _T("Contents"), CFile::modeRead | CFile::shareExclusive,
                       &fe))
    AfxThrowFileException(fe.m_cause, fe.m_lOsError);
  CArchive ar(&file, CArchive::load | CArchive::bNoFlushOnDelete);
  ar.m_pDocument = this;
  ar.m_bForceFlat = FALSE;
  try {
    if (file.GetLength() != 0) Serialize(ar);
    ar.Close();
    file.Close();
  } catch (CException*) {
    ar.Abort();
    file.Abort();
    throw;
  }
}

void COleDocument::SaveToStorage(CObject* pObject) {
  COleStreamFile file;
  CFileException fe;
  if (!file.CreateStream(m_lpRootStg, _T("Contents"),
                         CFile::modeReadWrite | CFile::shareExclusive | CFile::modeCreate, &fe))
    AfxThrowFileException(fe.m_cause, fe.m_lOsError);
  CArchive ar(&file, CArchive::store | CArchive::bNoFlushOnDelete);
  ar.m_pDocument = this;
  ar.m_bForceFlat = FALSE;
  try {
    if (pObject != NULL)
      pObject->Serialize(ar);
    else
      Serialize(ar);
    ar.Close();
    file.Close();
  } catch (CException*) {
    ar.Abort();
    file.Abort();
    throw;
  }
}

BOOL COleDocument::OnSaveDocument(LPCTSTR lpszPathName) {
  if (!m_bCompoundFile) return CDocument::OnSaveDocument(lpszPathName);

  // Into the file the document came from, or into a new one; the items
  // then live in the new file.
  BOOL bSame = m_lpRootStg != NULL && !m_bRootIsTemp && !m_strPathName.IsEmpty() &&
               AfxComparePath(m_strPathName, lpszPathName);
  LPSTORAGE lpOldRoot = m_lpRootStg;
  LPSTORAGE lpStorage = m_lpRootStg;
  if (!bSame) {
    CStringW strPath(lpszPathName);
    SCODE sc = ::StgCreateDocfile(strPath,
                                  STGM_CREATE | STGM_READWRITE | STGM_TRANSACTED |
                                      STGM_SHARE_DENY_WRITE,
                                  0, &lpStorage);
    if (FAILED(sc)) {
      CFileException fe;
      fe.m_cause = sc == STG_E_ACCESSDENIED     ? CFileException::accessDenied
                   : sc == STG_E_SHAREVIOLATION ? CFileException::sharingViolation
                                                : CFileException::badPath;
      fe.m_lOsError = sc;
      fe.m_strFileName = lpszPathName;
      ReportSaveLoadException(lpszPathName, &fe, TRUE, AFX_IDP_INVALID_FILENAME);
      return FALSE;
    }
  }

  m_bSameAsLoad = bSame;
  m_lpRootStg = lpStorage;
  try {
    CWaitCursor wait;
    SaveToStorage();
    SCODE sc = m_lpRootStg->Commit(STGC_DEFAULT);
    if (FAILED(sc)) _AfxThrowOleFileException(sc);
  } catch (CException* e) {
    if (!bSame) {
      lpStorage->Release();
      m_lpRootStg = lpOldRoot;
    } else {
      m_lpRootStg->Revert();
    }
    m_bSameAsLoad = TRUE;
    ReportSaveLoadException(lpszPathName, e, TRUE, AFX_IDP_FAILED_TO_SAVE_DOC);
    e->Delete();
    return FALSE;
  }
  m_bSameAsLoad = TRUE;

  if (!bSame) {
    if (m_bRemember) {
      if (lpOldRoot != NULL) lpOldRoot->Release();
      m_bRootIsTemp = FALSE;
    } else {
      lpStorage->Release();
      m_lpRootStg = lpOldRoot;
    }
  }
  if (m_bRemember) SetModifiedFlag(FALSE);
  return TRUE;
}

void COleDocument::OnCloseDocument() {
  // Servers of open items close without saving; SaveModified has already
  // asked about the document.
  POSITION pos = GetStartPosition();
  while (pos != NULL) {
    COleClientItem* pItem = GetNextClientItem(pos);
    if (pItem != NULL && pItem->m_lpObject != NULL) pItem->Close(OLECLOSE_NOSAVE);
  }
  BOOL bAutoDelete = m_bAutoDelete;
  m_bAutoDelete = FALSE;
  CDocument::OnCloseDocument();
  ReleaseRoot();
  if (bAutoDelete) delete this;
}

BOOL COleDocument::SaveModified() {
  UpdateModifiedFlag();
  return CDocument::SaveModified();
}

// The verb commands go to the item selected in the view the command came
// through.
BOOL COleDocument::OnCmdMsg(UINT nID, int nCode, void* pExtra,
                            AFX_CMDHANDLERINFO* pHandlerInfo) {
  if (nCode == CN_COMMAND && nID >= ID_OLE_VERB_FIRST && nID <= ID_OLE_VERB_LAST &&
      pHandlerInfo == NULL) {
    CView* pView = AfxGetThreadState()->m_pRoutingView;
    if (pView != NULL && pView->GetDocument() == this) {
      COleClientItem* pItem = GetPrimarySelectedItem(pView);
      if (pItem != NULL) {
        pItem->DoVerb((LONG)(nID - ID_OLE_VERB_FIRST), pView);
        return TRUE;
      }
    }
  }
  return CDocument::OnCmdMsg(nID, nCode, pExtra, pHandlerInfo);
}

void COleDocument::OnUpdatePasteMenu(CCmdUI* pCmdUI) {
  pCmdUI->Enable(COleClientItem::CanPaste());
}

void COleDocument::OnUpdatePasteLinkMenu(CCmdUI* pCmdUI) {
  pCmdUI->Enable(COleClientItem::CanPasteLink());
}

void COleDocument::OnUpdateEditLinksMenu(CCmdUI* pCmdUI) {
  BOOL bLinks = FALSE;
  POSITION pos = GetStartPosition();
  while (pos != NULL && !bLinks) {
    COleClientItem* pItem = GetNextClientItem(pos);
    if (pItem != NULL && pItem->GetType() == OT_LINK) bLinks = TRUE;
  }
  pCmdUI->Enable(bLinks);
}

void COleDocument::OnUpdateObjectVerbMenu(CCmdUI* pCmdUI) {
  CView* pView = AfxGetThreadState()->m_pRoutingView;
  COleClientItem* pItem = NULL;
  if (pView != NULL && pView->GetDocument() == this) pItem = GetPrimarySelectedItem(pView);
  if (pCmdUI->m_pMenu == NULL) {
    pCmdUI->Enable(pItem != NULL);
    return;
  }
  AfxOleSetEditMenu(pItem, pCmdUI->m_pMenu, pCmdUI->m_nIndex, ID_OLE_VERB_FIRST,
                    ID_OLE_VERB_LAST, ID_OLE_EDIT_CONVERT);
}
