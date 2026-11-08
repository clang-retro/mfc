// doctempl.cpp - document templates and the document manager.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include "afximpl.h"

// ---------------------------------------------------------------------
// CDocTemplate

IMPLEMENT_DYNAMIC(CDocTemplate, CCmdTarget)

CDocTemplate::CDocTemplate(UINT nIDResource, CRuntimeClass* pDocClass,
                           CRuntimeClass* pFrameClass, CRuntimeClass* pViewClass) {
  m_nIDResource = nIDResource;
  m_pDocClass = pDocClass;
  m_pFrameClass = pFrameClass;
  m_pViewClass = pViewClass;
  m_bAutoDelete = TRUE;
  m_pAttachedFactory = NULL;
  m_nIDServerResource = 0;
  m_nIDEmbeddingResource = 0;
  m_nIDContainerResource = 0;
  m_hMenuInPlace = NULL;
  m_hAccelInPlace = NULL;
  m_hMenuEmbedding = NULL;
  m_hAccelEmbedding = NULL;
  m_hMenuInPlaceServer = NULL;
  m_hAccelInPlaceServer = NULL;
  m_pOleFrameClass = NULL;
  m_pOleViewClass = NULL;
}

CDocTemplate::~CDocTemplate() {
  HMENU menus[] = {m_hMenuInPlace, m_hMenuEmbedding, m_hMenuInPlaceServer};
  for (HMENU hMenu : menus)
    if (hMenu != NULL) ::DestroyMenu(hMenu);
  HACCEL accels[] = {m_hAccelInPlace, m_hAccelEmbedding, m_hAccelInPlaceServer};
  for (HACCEL hAccel : accels)
    if (hAccel != NULL) ::DestroyAcceleratorTable(hAccel);
}

void CDocTemplate::SetContainerInfo(UINT nIDOleInPlaceContainer) {
  m_nIDContainerResource = nIDOleInPlaceContainer;
  HINSTANCE hInst = AfxFindResourceHandle(MAKEINTRESOURCE(nIDOleInPlaceContainer), RT_MENU);
  if (m_hMenuInPlace == NULL)
    m_hMenuInPlace = ::LoadMenu(hInst, MAKEINTRESOURCE(nIDOleInPlaceContainer));
  if (m_hAccelInPlace == NULL)
    m_hAccelInPlace = ::LoadAccelerators(hInst, MAKEINTRESOURCE(nIDOleInPlaceContainer));
}

void CDocTemplate::SetServerInfo(UINT nIDOleEmbedding, UINT nIDOleInPlaceServer,
                                 CRuntimeClass* pOleFrameClass, CRuntimeClass* pOleViewClass) {
  m_nIDEmbeddingResource = nIDOleEmbedding;
  m_nIDServerResource = nIDOleInPlaceServer;
  m_pOleFrameClass = pOleFrameClass;
  m_pOleViewClass = pOleViewClass;
  HINSTANCE hInst =
      AfxFindResourceHandle(MAKEINTRESOURCE(nIDOleEmbedding ? nIDOleEmbedding : nIDOleInPlaceServer), RT_MENU);
  if (m_hMenuEmbedding == NULL && nIDOleEmbedding != 0) {
    m_hMenuEmbedding = ::LoadMenu(hInst, MAKEINTRESOURCE(nIDOleEmbedding));
    m_hAccelEmbedding = ::LoadAccelerators(hInst, MAKEINTRESOURCE(nIDOleEmbedding));
  }
  if (m_hMenuInPlaceServer == NULL && nIDOleInPlaceServer != 0) {
    m_hMenuInPlaceServer = ::LoadMenu(hInst, MAKEINTRESOURCE(nIDOleInPlaceServer));
    m_hAccelInPlaceServer = ::LoadAccelerators(hInst, MAKEINTRESOURCE(nIDOleInPlaceServer));
  }
}

void CDocTemplate::LoadTemplate() {
  if (m_strDocStrings.IsEmpty()) m_strDocStrings.LoadString(m_nIDResource);
}

BOOL CDocTemplate::GetDocString(CString& rString, enum DocStringIndex index) const {
  return AfxExtractSubString(rString, m_strDocStrings, (int)index);
}

CDocTemplate::Confidence CDocTemplate::MatchDocType(LPCTSTR lpszPathName,
                                                    CDocument*& rpDocMatch) {
  rpDocMatch = NULL;
  POSITION pos = GetFirstDocPosition();
  while (pos != NULL) {
    CDocument* pDoc = GetNextDoc(pos);
    if (!pDoc->GetPathName().IsEmpty() && AfxComparePath(pDoc->GetPathName(), lpszPathName)) {
      rpDocMatch = pDoc;
      return yesAlreadyOpen;
    }
  }

  // The filter extension may list several, as in ".htm;.html".
  CString strFilterExt;
  if (GetDocString(strFilterExt, filterExt) && !strFilterExt.IsEmpty()) {
    LPCTSTR lpszDot = _tcsrchr(lpszPathName, _T('.'));
    LPCTSTR lpszSlash = _tcsrchr(lpszPathName, _T('\\'));
    if (lpszDot != NULL && (lpszSlash == NULL || lpszDot > lpszSlash)) {
      int iStart = 0;
      for (;;) {
        CString strExt = strFilterExt.Tokenize(_T(";"), iStart);
        if (iStart < 0) break;
        if (!strExt.IsEmpty() && strExt[0] == _T('*')) strExt = strExt.Mid(1);
        if (lstrcmpi(lpszDot, strExt) == 0) return yesAttemptNative;
      }
    }
  }
  return yesAttemptForeign;
}

void CDocTemplate::AddDocument(CDocument* pDoc) { pDoc->m_pDocTemplate = this; }

void CDocTemplate::RemoveDocument(CDocument* pDoc) { pDoc->m_pDocTemplate = NULL; }

CDocument* CDocTemplate::CreateNewDocument() {
  if (m_pDocClass == NULL) return NULL;
  CDocument* pDocument = (CDocument*)m_pDocClass->CreateObject();
  if (pDocument == NULL) return NULL;
  AddDocument(pDocument);
  return pDocument;
}

CFrameWnd* CDocTemplate::CreateNewFrame(CDocument* pDoc, CFrameWnd* pOther) {
  CCreateContext context;
  context.m_pCurrentFrame = pOther;
  context.m_pCurrentDoc = pDoc;
  context.m_pNewViewClass = m_pViewClass;
  context.m_pNewDocTemplate = this;

  if (m_pFrameClass == NULL) return NULL;
  CFrameWnd* pFrame = (CFrameWnd*)m_pFrameClass->CreateObject();
  if (pFrame == NULL) return NULL;
  // A frame that fails after its window exists deletes itself.
  if (!pFrame->LoadFrame(m_nIDResource, WS_OVERLAPPEDWINDOW | FWS_ADDTOTITLE, NULL, &context))
    return NULL;
  return pFrame;
}

void CDocTemplate::InitialUpdateFrame(CFrameWnd* pFrame, CDocument* pDoc, BOOL bMakeVisible) {
  pFrame->InitialUpdateFrame(pDoc, bMakeVisible);
}

BOOL CDocTemplate::SaveAllModified() {
  POSITION pos = GetFirstDocPosition();
  while (pos != NULL) {
    if (!GetNextDoc(pos)->SaveModified()) return FALSE;
  }
  return TRUE;
}

void CDocTemplate::CloseAllDocuments(BOOL bEndSession) {
  (void)bEndSession;
  POSITION pos = GetFirstDocPosition();
  while (pos != NULL) GetNextDoc(pos)->OnCloseDocument();
}

CDocument* CDocTemplate::OpenDocumentFile(LPCTSTR lpszPathName, BOOL bAddToMRU,
                                          BOOL bMakeVisible) {
  (void)bAddToMRU;
  return OpenDocumentFile(lpszPathName, bMakeVisible);
}

void CDocTemplate::OnIdle() {
  POSITION pos = GetFirstDocPosition();
  while (pos != NULL) GetNextDoc(pos)->OnIdle();
}

BOOL CDocTemplate::OnCmdMsg(UINT nID, int nCode, void* pExtra,
                            AFX_CMDHANDLERINFO* pHandlerInfo) {
  return CCmdTarget::OnCmdMsg(nID, nCode, pExtra, pHandlerInfo);
}

static void _AfxDefaultDocTitle(CDocTemplate* pTemplate, CString& strDocName) {
  if (!pTemplate->GetDocString(strDocName, CDocTemplate::docName) || strDocName.IsEmpty())
    AfxLoadString(AFX_IDS_UNTITLED, strDocName);
}

// ---------------------------------------------------------------------
// CSingleDocTemplate

IMPLEMENT_DYNAMIC(CSingleDocTemplate, CDocTemplate)

CSingleDocTemplate::CSingleDocTemplate(UINT nIDResource, CRuntimeClass* pDocClass,
                                       CRuntimeClass* pFrameClass, CRuntimeClass* pViewClass)
    : CDocTemplate(nIDResource, pDocClass, pFrameClass, pViewClass), m_pOnlyDoc(NULL) {}

CSingleDocTemplate::~CSingleDocTemplate() {}

void CSingleDocTemplate::AddDocument(CDocument* pDoc) {
  CDocTemplate::AddDocument(pDoc);
  m_pOnlyDoc = pDoc;
}

void CSingleDocTemplate::RemoveDocument(CDocument* pDoc) {
  CDocTemplate::RemoveDocument(pDoc);
  if (m_pOnlyDoc == pDoc) m_pOnlyDoc = NULL;
}

POSITION CSingleDocTemplate::GetFirstDocPosition() const {
  return m_pOnlyDoc != NULL ? BEFORE_START_POSITION : NULL;
}

CDocument* CSingleDocTemplate::GetNextDoc(POSITION& rPos) const {
  CDocument* pDoc = rPos == BEFORE_START_POSITION ? m_pOnlyDoc : NULL;
  rPos = NULL;
  return pDoc;
}

void CSingleDocTemplate::SetDefaultTitle(CDocument* pDocument) {
  CString strDocName;
  _AfxDefaultDocTitle(this, strDocName);
  pDocument->SetTitle(strDocName);
}

CDocument* CSingleDocTemplate::OpenDocumentFile(LPCTSTR lpszPathName, BOOL bMakeVisible) {
  return OpenDocumentFile(lpszPathName, TRUE, bMakeVisible);
}

CDocument* CSingleDocTemplate::OpenDocumentFile(LPCTSTR lpszPathName, BOOL bAddToMRU,
                                                BOOL bMakeVisible) {
  CDocument* pDocument = NULL;
  CFrameWnd* pFrame = NULL;
  BOOL bCreated = FALSE;

  if (m_pOnlyDoc != NULL) {
    // An SDI application reuses its one document and frame.
    pDocument = m_pOnlyDoc;
    if (!pDocument->SaveModified()) return NULL;
    POSITION pos = pDocument->GetFirstViewPosition();
    if (pos != NULL) pFrame = pDocument->GetNextView(pos)->GetParentFrame();
    if (pFrame == NULL) pFrame = (CFrameWnd*)AfxGetMainWnd();
  } else {
    pDocument = CreateNewDocument();
    bCreated = TRUE;
  }
  if (pDocument == NULL) {
    AfxMessageBox(AFX_IDP_FAILED_TO_CREATE_DOC);
    return NULL;
  }

  if (pFrame == NULL) {
    // Keep the document alive if the frame fails halfway.
    BOOL bAutoDelete = pDocument->m_bAutoDelete;
    pDocument->m_bAutoDelete = FALSE;
    pFrame = CreateNewFrame(pDocument, NULL);
    pDocument->m_bAutoDelete = bAutoDelete;
    if (pFrame == NULL) {
      AfxMessageBox(AFX_IDP_FAILED_TO_CREATE_DOC);
      delete pDocument;
      return NULL;
    }
  }

  if (lpszPathName == NULL) {
    SetDefaultTitle(pDocument);
    if (!bMakeVisible) pDocument->m_bEmbedded = TRUE;
    if (!pDocument->OnNewDocument()) {
      if (bCreated) pFrame->DestroyWindow();  // takes the document with it
      return NULL;
    }
  } else {
    CWaitCursor wait;
    BOOL bWasModified = pDocument->IsModified();
    pDocument->SetModifiedFlag(FALSE);
    if (!pDocument->OnOpenDocument(lpszPathName)) {
      if (bCreated) {
        pFrame->DestroyWindow();
      } else if (!pDocument->IsModified()) {
        // The old contents are intact.
        pDocument->SetModifiedFlag(bWasModified);
      } else {
        // Loading wiped the old contents; start a new document instead.
        SetDefaultTitle(pDocument);
        if (!pDocument->OnNewDocument()) {
          pFrame->DestroyWindow();
          return NULL;
        }
        pFrame->InitialUpdateFrame(pDocument, bMakeVisible);
      }
      return NULL;
    }
    pDocument->SetPathName(lpszPathName, bAddToMRU);
  }

  CWinThread* pThread = AfxGetThread();
  if (bCreated && pThread != NULL && pThread->m_pMainWnd == NULL) pThread->m_pMainWnd = pFrame;
  InitialUpdateFrame(pFrame, pDocument, bMakeVisible);
  return pDocument;
}

// ---------------------------------------------------------------------
// CMultiDocTemplate

IMPLEMENT_DYNAMIC(CMultiDocTemplate, CDocTemplate)

CMultiDocTemplate::CMultiDocTemplate(UINT nIDResource, CRuntimeClass* pDocClass,
                                     CRuntimeClass* pFrameClass, CRuntimeClass* pViewClass)
    : CDocTemplate(nIDResource, pDocClass, pFrameClass, pViewClass) {
  m_hMenuShared = NULL;
  m_hAccelTable = NULL;
  m_nUntitledCount = 0;
}

CMultiDocTemplate::~CMultiDocTemplate() {
  if (m_hMenuShared != NULL) ::DestroyMenu(m_hMenuShared);
}

void CMultiDocTemplate::LoadTemplate() {
  CDocTemplate::LoadTemplate();
  if (m_nIDResource != 0 && m_hMenuShared == NULL) {
    HINSTANCE hInst = AfxFindResourceHandle(MAKEINTRESOURCE(m_nIDResource), RT_MENU);
    m_hMenuShared = ::LoadMenu(hInst, MAKEINTRESOURCE(m_nIDResource));
    m_hAccelTable = ::LoadAccelerators(hInst, MAKEINTRESOURCE(m_nIDResource));
  }
}

void CMultiDocTemplate::AddDocument(CDocument* pDoc) {
  CDocTemplate::AddDocument(pDoc);
  m_docList.AddTail(pDoc);
}

void CMultiDocTemplate::RemoveDocument(CDocument* pDoc) {
  CDocTemplate::RemoveDocument(pDoc);
  POSITION pos = m_docList.Find(pDoc);
  if (pos != NULL) m_docList.RemoveAt(pos);
}

POSITION CMultiDocTemplate::GetFirstDocPosition() const { return m_docList.GetHeadPosition(); }

CDocument* CMultiDocTemplate::GetNextDoc(POSITION& rPos) const {
  return (CDocument*)m_docList.GetNext(rPos);
}

void CMultiDocTemplate::SetDefaultTitle(CDocument* pDocument) {
  CString strDocName;
  _AfxDefaultDocTitle(this, strDocName);
  TCHAR szNum[16];
  wsprintf(szNum, _T("%u"), m_nUntitledCount + 1);
  strDocName += szNum;
  pDocument->SetTitle(strDocName);
}

CDocument* CMultiDocTemplate::OpenDocumentFile(LPCTSTR lpszPathName, BOOL bMakeVisible) {
  return OpenDocumentFile(lpszPathName, TRUE, bMakeVisible);
}

CDocument* CMultiDocTemplate::OpenDocumentFile(LPCTSTR lpszPathName, BOOL bAddToMRU,
                                               BOOL bMakeVisible) {
  CDocument* pDocument = CreateNewDocument();
  if (pDocument == NULL) {
    AfxMessageBox(AFX_IDP_FAILED_TO_CREATE_DOC);
    return NULL;
  }

  BOOL bAutoDelete = pDocument->m_bAutoDelete;
  pDocument->m_bAutoDelete = FALSE;
  CFrameWnd* pFrame = CreateNewFrame(pDocument, NULL);
  pDocument->m_bAutoDelete = bAutoDelete;
  if (pFrame == NULL) {
    AfxMessageBox(AFX_IDP_FAILED_TO_CREATE_DOC);
    delete pDocument;
    return NULL;
  }

  if (lpszPathName == NULL) {
    SetDefaultTitle(pDocument);
    if (!bMakeVisible) pDocument->m_bEmbedded = TRUE;
    if (!pDocument->OnNewDocument()) {
      pFrame->DestroyWindow();
      return NULL;
    }
    m_nUntitledCount++;
  } else {
    CWaitCursor wait;
    if (!pDocument->OnOpenDocument(lpszPathName)) {
      pFrame->DestroyWindow();
      return NULL;
    }
    pDocument->SetPathName(lpszPathName, bAddToMRU);
  }

  InitialUpdateFrame(pFrame, pDocument, bMakeVisible);
  return pDocument;
}

// ---------------------------------------------------------------------
// CDocManager

IMPLEMENT_DYNAMIC(CDocManager, CObject)

CPtrList* CDocManager::pStaticList = NULL;
BOOL CDocManager::bStaticInit = TRUE;

CDocManager::CDocManager() {}

CDocManager::~CDocManager() {
  while (!m_templateList.IsEmpty()) {
    CDocTemplate* pTemplate = (CDocTemplate*)m_templateList.RemoveHead();
    if (pTemplate->m_bAutoDelete) delete pTemplate;
  }
}

static BOOL SetShellKey(LPCTSTR lpszKey, LPCTSTR lpszValue) {
  HKEY hKey;
  if (::RegCreateKeyEx(HKEY_CLASSES_ROOT, lpszKey, 0, NULL, 0, KEY_WRITE, NULL, &hKey, NULL) !=
      ERROR_SUCCESS)
    return FALSE;
  LONG l = ::RegSetValueEx(hKey, NULL, 0, REG_SZ, (const BYTE*)lpszValue,
                           (DWORD)((lstrlen(lpszValue) + 1) * sizeof(TCHAR)));
  ::RegCloseKey(hKey);
  return l == ERROR_SUCCESS;
}

static CString ShellKeyValue(LPCTSTR lpszKey) {
  TCHAR sz[MAX_PATH];
  LONG cb = sizeof(sz);
  if (::RegQueryValue(HKEY_CLASSES_ROOT, lpszKey, sz, &cb) != ERROR_SUCCESS) return CString();
  return sz;
}

static void DeleteShellTree(HKEY hParent, LPCTSTR lpszKey) {
  HKEY hKey;
  if (::RegOpenKeyEx(hParent, lpszKey, 0, KEY_READ | KEY_WRITE, &hKey) != ERROR_SUCCESS) return;
  TCHAR szName[256];
  while (::RegEnumKey(hKey, 0, szName, _countof(szName)) == ERROR_SUCCESS)
    DeleteShellTree(hKey, szName);
  ::RegCloseKey(hKey);
  ::RegDeleteKey(hParent, lpszKey);
}

// The extensions of a filter string such as ".txt;.log" (or "*.txt").
static void ShellExtensions(const CString& strFilter, CStringArray& exts) {
  int nStart = 0;
  while (nStart < strFilter.GetLength()) {
    int nEnd = strFilter.Find(_T(';'), nStart);
    if (nEnd < 0) nEnd = strFilter.GetLength();
    CString str = strFilter.Mid(nStart, nEnd - nStart);
    str.TrimLeft();
    str.TrimRight();
    if (!str.IsEmpty() && str[0] == _T('*')) str = str.Mid(1);
    if (str.GetLength() > 1 && str[0] == _T('.')) exts.Add(str);
    nStart = nEnd + 1;
  }
}

void CDocManager::RegisterShellFileTypes(BOOL bCompat) {
  TCHAR szPath[MAX_PATH];
  ::GetModuleFileName(AfxGetInstanceHandle(), szPath, MAX_PATH);
  CString strPath = CString(_T("\"")) + szPath + _T("\"");
  int nTemplate = 0;
  POSITION pos = GetFirstDocTemplatePosition();
  while (pos != NULL) {
    CDocTemplate* pTemplate = GetNextDocTemplate(pos);
    nTemplate++;
    CString strTypeId, strTypeName, strFilter;
    if (!pTemplate->GetDocString(strTypeId, CDocTemplate::regFileTypeId) || strTypeId.IsEmpty())
      continue;
    pTemplate->GetDocString(strTypeName, CDocTemplate::regFileTypeName);
    pTemplate->GetDocString(strFilter, CDocTemplate::filterExt);
    // The type's name, icon and commands; the name the user sees stays as
    // the program first registered it.
    if (strTypeName.IsEmpty()) strTypeName = strTypeId;
    if (ShellKeyValue(strTypeId).IsEmpty()) SetShellKey(strTypeId, strTypeName);
    CString strIcon;
    strIcon.Format(_T("%s,%d"), szPath, nTemplate);
    SetShellKey(strTypeId + _T("\\DefaultIcon"), strIcon);
    SetShellKey(strTypeId + _T("\\shell\\open\\command"), strPath + _T(" \"%1\""));
    if (bCompat) {
      SetShellKey(strTypeId + _T("\\shell\\print\\command"), strPath + _T(" /p \"%1\""));
      SetShellKey(strTypeId + _T("\\shell\\printto\\command"),
                  strPath + _T(" /pt \"%1\" \"%2\" \"%3\" \"%4\""));
    }
    // Each extension is a file of the type, unless another program's
    // type already has it.
    CStringArray exts;
    ShellExtensions(strFilter, exts);
    for (INT_PTR i = 0; i < exts.GetSize(); i++) {
      CString strOld = ShellKeyValue(exts[i]);
      if (strOld.IsEmpty() || strOld == strTypeId) {
        SetShellKey(exts[i], strTypeId);
        if (bCompat) SetShellKey(exts[i] + _T("\\ShellNew\\NullFile"), _T(""));
      }
    }
  }
}

void CDocManager::UnregisterShellFileTypes() {
  POSITION pos = GetFirstDocTemplatePosition();
  while (pos != NULL) {
    CDocTemplate* pTemplate = GetNextDocTemplate(pos);
    CString strTypeId, strFilter;
    if (!pTemplate->GetDocString(strTypeId, CDocTemplate::regFileTypeId) || strTypeId.IsEmpty())
      continue;
    pTemplate->GetDocString(strFilter, CDocTemplate::filterExt);
    CStringArray exts;
    ShellExtensions(strFilter, exts);
    for (INT_PTR i = 0; i < exts.GetSize(); i++)
      if (ShellKeyValue(exts[i]) == strTypeId) DeleteShellTree(HKEY_CLASSES_ROOT, exts[i]);
    DeleteShellTree(HKEY_CLASSES_ROOT, strTypeId);
  }
}

void CDocManager::AddDocTemplate(CDocTemplate* pTemplate) {
  if (pTemplate == NULL) return;
  pTemplate->LoadTemplate();
  m_templateList.AddTail(pTemplate);
}

POSITION CDocManager::GetFirstDocTemplatePosition() const {
  return m_templateList.GetHeadPosition();
}

CDocTemplate* CDocManager::GetNextDocTemplate(POSITION& pos) const {
  return (CDocTemplate*)m_templateList.GetNext(pos);
}

CDocument* CDocManager::OpenDocumentFile(LPCTSTR lpszFileName) {
  return OpenDocumentFile(lpszFileName, TRUE);
}

CDocument* CDocManager::OpenDocumentFile(LPCTSTR lpszFileName, BOOL bAddToMRU) {
  if (lpszFileName == NULL) return NULL;

  // Accept a name in quotes, as the shell passes them.
  TCHAR szUnquoted[MAX_PATH];
  lstrcpyn(szUnquoted, lpszFileName, MAX_PATH);
  if (szUnquoted[0] == _T('"')) {
    lstrcpyn(szUnquoted, lpszFileName + 1, MAX_PATH);
    int n = lstrlen(szUnquoted);
    if (n > 0 && szUnquoted[n - 1] == _T('"')) szUnquoted[n - 1] = 0;
  }
  TCHAR szPath[MAX_PATH];
  if (!AfxFullPath(szPath, szUnquoted)) lstrcpyn(szPath, szUnquoted, MAX_PATH);

  // Pick the template that claims the file most strongly.
  CDocTemplate::Confidence bestMatch = CDocTemplate::noAttempt;
  CDocTemplate* pBestTemplate = NULL;
  CDocument* pOpenDocument = NULL;
  POSITION pos = m_templateList.GetHeadPosition();
  while (pos != NULL) {
    CDocTemplate* pTemplate = (CDocTemplate*)m_templateList.GetNext(pos);
    CDocument* pOpenDoc = NULL;
    CDocTemplate::Confidence match = pTemplate->MatchDocType(szPath, pOpenDoc);
    if (match > bestMatch) {
      bestMatch = match;
      pBestTemplate = pTemplate;
    }
    if (match == CDocTemplate::yesAlreadyOpen) {
      pOpenDocument = pOpenDoc;
      break;
    }
  }

  if (pOpenDocument != NULL) {
    // Already open: bring its frame forward.
    POSITION posView = pOpenDocument->GetFirstViewPosition();
    if (posView != NULL) {
      CFrameWnd* pFrame = pOpenDocument->GetNextView(posView)->GetParentFrame();
      if (pFrame != NULL) {
        pFrame->ActivateFrame();
        CFrameWnd* pAppFrame = (CFrameWnd*)AfxGetMainWnd();
        if (pAppFrame != NULL && pFrame != pAppFrame) pAppFrame->ActivateFrame();
      }
    }
    return pOpenDocument;
  }

  if (pBestTemplate == NULL) {
    AfxMessageBox(AFX_IDP_FAILED_TO_OPEN_DOC);
    return NULL;
  }
  return pBestTemplate->OpenDocumentFile(szPath, bAddToMRU, TRUE);
}

BOOL CDocManager::SaveAllModified() {
  POSITION pos = m_templateList.GetHeadPosition();
  while (pos != NULL) {
    if (!((CDocTemplate*)m_templateList.GetNext(pos))->SaveAllModified()) return FALSE;
  }
  return TRUE;
}

void CDocManager::CloseAllDocuments(BOOL bEndSession) {
  POSITION pos = m_templateList.GetHeadPosition();
  while (pos != NULL) ((CDocTemplate*)m_templateList.GetNext(pos))->CloseAllDocuments(bEndSession);
}

int CDocManager::GetOpenDocumentCount() {
  int nOpen = 0;
  POSITION pos = m_templateList.GetHeadPosition();
  while (pos != NULL) {
    CDocTemplate* pTemplate = (CDocTemplate*)m_templateList.GetNext(pos);
    POSITION posDoc = pTemplate->GetFirstDocPosition();
    while (posDoc != NULL) {
      pTemplate->GetNextDoc(posDoc);
      nOpen++;
    }
  }
  return nOpen;
}

// Adds "name|*.ext1;*.ext2|" for a template to a '|' separated filter.
void AFXAPI _AfxAppendFilterSuffix(CString& filter, OPENFILENAME& ofn, CDocTemplate* pTemplate,
                                   CString* pstrDefaultExt) {
  CString strFilterExt, strFilterName;
  if (!pTemplate->GetDocString(strFilterExt, CDocTemplate::filterExt) || strFilterExt.IsEmpty() ||
      !pTemplate->GetDocString(strFilterName, CDocTemplate::filterName) ||
      strFilterName.IsEmpty())
    return;

  CString strPatterns;
  int iStart = 0;
  BOOL bFirst = TRUE;
  for (;;) {
    CString strExt = strFilterExt.Tokenize(_T(";"), iStart);
    if (iStart < 0) break;
    if (!strExt.IsEmpty() && strExt[0] == _T('*')) strExt = strExt.Mid(1);
    if (strExt.IsEmpty()) continue;
    if (bFirst && pstrDefaultExt != NULL) {
      // The default extension has no dot.
      *pstrDefaultExt = strExt[0] == _T('.') ? strExt.Mid(1) : strExt;
      ofn.lpstrDefExt = *pstrDefaultExt;
      ofn.nFilterIndex = ofn.nMaxCustFilter + 1;
    }
    if (!bFirst) strPatterns += _T(';');
    strPatterns += _T('*');
    strPatterns += strExt;
    bFirst = FALSE;
  }
  if (strPatterns.IsEmpty()) return;
  filter += strFilterName;
  filter += _T('|');
  filter += strPatterns;
  filter += _T('|');
  ofn.nMaxCustFilter++;
}

#ifndef OPENFILENAME_SIZE_VERSION_400
#define OPENFILENAME_SIZE_VERSION_400 sizeof(OPENFILENAME)
#endif

BOOL CDocManager::DoPromptFileName(CString& fileName, UINT nIDSTitle, DWORD lFlags,
                                   BOOL bOpenFileDialog, CDocTemplate* pTemplate) {
  CString title;
  AfxLoadString(nIDSTitle, title);

  OPENFILENAME ofn;
  memset(&ofn, 0, sizeof(ofn));
  // The Windows 95 / NT 4 size works everywhere.
  ofn.lStructSize = OPENFILENAME_SIZE_VERSION_400;
  ofn.hwndOwner = _AfxGetSafeOwner(NULL, NULL);
  ofn.Flags = lFlags | OFN_EXPLORER | OFN_HIDEREADONLY | OFN_NOCHANGEDIR;
  if (!bOpenFileDialog) ofn.Flags |= OFN_OVERWRITEPROMPT;

  CString strFilter;
  CString strDefault;
  if (pTemplate != NULL) {
    _AfxAppendFilterSuffix(strFilter, ofn, pTemplate, &strDefault);
  } else {
    BOOL bFirst = TRUE;
    POSITION pos = m_templateList.GetHeadPosition();
    while (pos != NULL) {
      _AfxAppendFilterSuffix(strFilter, ofn, (CDocTemplate*)m_templateList.GetNext(pos),
                             bFirst ? &strDefault : NULL);
      bFirst = FALSE;
    }
  }
  CString allFilter;
  AfxLoadString(AFX_IDS_ALLFILTER, allFilter);
  strFilter += allFilter;
  strFilter += _T("|*.*|");
  ofn.nMaxCustFilter++;
  if (ofn.nFilterIndex == 0) ofn.nFilterIndex = 1;

  // The dialog wants NUL separators and a double NUL at the end.
  int nLen = strFilter.GetLength();
  TCHAR* pszFilter = new TCHAR[nLen + 2];
  memcpy(pszFilter, (LPCTSTR)strFilter, nLen * sizeof(TCHAR));
  for (int i = 0; i < nLen; i++)
    if (pszFilter[i] == _T('|')) pszFilter[i] = 0;
  pszFilter[nLen] = 0;
  pszFilter[nLen + 1] = 0;
  ofn.lpstrFilter = pszFilter;
  ofn.nMaxCustFilter = 0;  // only used above to count entries

  TCHAR szFile[MAX_PATH];
  lstrcpyn(szFile, fileName, MAX_PATH);
  ofn.lpstrFile = szFile;
  ofn.nMaxFile = MAX_PATH;
  ofn.lpstrTitle = title.IsEmpty() ? NULL : (LPCTSTR)title;

  BOOL bResult = bOpenFileDialog ? ::GetOpenFileName(&ofn) : ::GetSaveFileName(&ofn);
  delete[] pszFilter;
  if (bResult) fileName = szFile;
  return bResult;
}

// ---------------------------------------------------------------------
// File New with several document types: a small list dialog, built in
// memory so that applications need no resource for it.

static INT_PTR CALLBACK _AfxNewTypeDlgProc(HWND hDlg, UINT message, WPARAM wParam,
                                           LPARAM lParam) {
  switch (message) {
    case WM_INITDIALOG: {
      CPtrList* pList = (CPtrList*)lParam;
      HWND hList = ::GetDlgItem(hDlg, 100);
      POSITION pos = pList->GetHeadPosition();
      while (pos != NULL) {
        CDocTemplate* pTemplate = (CDocTemplate*)pList->GetNext(pos);
        CString strTypeName;
        if (pTemplate->GetDocString(strTypeName, CDocTemplate::fileNewName) &&
            !strTypeName.IsEmpty()) {
          int i = (int)::SendMessage(hList, LB_ADDSTRING, 0, (LPARAM)(LPCTSTR)strTypeName);
          if (i >= 0) ::SendMessage(hList, LB_SETITEMDATA, i, (LPARAM)pTemplate);
        }
      }
      ::SendMessage(hList, LB_SETCURSEL, 0, 0);
      ::SetFocus(hList);
      return FALSE;
    }
    case WM_COMMAND:
      if (LOWORD(wParam) == IDOK || (LOWORD(wParam) == 100 && HIWORD(wParam) == LBN_DBLCLK)) {
        HWND hList = ::GetDlgItem(hDlg, 100);
        int i = (int)::SendMessage(hList, LB_GETCURSEL, 0, 0);
        LPARAM lData = i >= 0 ? ::SendMessage(hList, LB_GETITEMDATA, i, 0) : 0;
        ::EndDialog(hDlg, (INT_PTR)lData);
        return TRUE;
      }
      if (LOWORD(wParam) == IDCANCEL) {
        ::EndDialog(hDlg, 0);
        return TRUE;
      }
      break;
  }
  return FALSE;
}

namespace {
// Writes a dialog template; strings in templates are always UTF-16.
struct DlgWriter {
  alignas(4) WORD buf[512];
  int n = 0;
  void Align(int nBytes) {
    while ((n * 2) % nBytes) buf[n++] = 0;
  }
  void Word(WORD w) { buf[n++] = w; }
  void Dword(DWORD d) {
    Word(LOWORD(d));
    Word(HIWORD(d));
  }
  void Str(const char* s) {
    while (*s) buf[n++] = (WORD)(unsigned char)*s++;
    buf[n++] = 0;
  }
  void Item(DWORD style, short x, short y, short cx, short cy, WORD id, WORD atom,
            const char* text) {
    Align(4);
    Dword(style | WS_CHILD | WS_VISIBLE);
    Dword(0);
    Word(x); Word(y); Word(cx); Word(cy);
    Word(id);
    Word(0xFFFF);
    Word(atom);
    Str(text);
    Word(0);  // no creation data
  }
};
}  // namespace

static CDocTemplate* _AfxChooseNewType(CPtrList& templateList) {
  // Only templates with a File New name are offered.
  int nCandidates = 0;
  CDocTemplate* pOnly = NULL;
  POSITION pos = templateList.GetHeadPosition();
  while (pos != NULL) {
    CDocTemplate* pTemplate = (CDocTemplate*)templateList.GetNext(pos);
    CString strTypeName;
    if (pTemplate->GetDocString(strTypeName, CDocTemplate::fileNewName) &&
        !strTypeName.IsEmpty()) {
      nCandidates++;
      pOnly = pTemplate;
    }
  }
  if (nCandidates == 0) return (CDocTemplate*)templateList.GetHead();
  if (nCandidates == 1) return pOnly;

  DlgWriter w;
  w.Dword(DS_MODALFRAME | DS_SETFONT | WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_CENTER);
  w.Dword(0);
  w.Word(3);  // items
  w.Word(0); w.Word(0); w.Word(186); w.Word(74);
  w.Word(0);  // no menu
  w.Word(0);  // default class
  w.Str("New");
  w.Word(8);
  w.Str("MS Shell Dlg");
  w.Item(LBS_NOTIFY | LBS_HASSTRINGS | WS_VSCROLL | WS_BORDER | WS_TABSTOP, 7, 7, 118, 60, 100,
         0x0083, "");
  w.Item(BS_DEFPUSHBUTTON | WS_TABSTOP, 130, 7, 50, 14, IDOK, 0x0080, "OK");
  w.Item(BS_PUSHBUTTON | WS_TABSTOP, 130, 24, 50, 14, IDCANCEL, 0x0080, "Cancel");

  INT_PTR nResult = ::DialogBoxIndirectParam(AfxGetInstanceHandle(), (LPCDLGTEMPLATE)w.buf,
                                             _AfxGetSafeOwner(NULL, NULL), _AfxNewTypeDlgProc,
                                             (LPARAM)&templateList);
  return nResult > 0 ? (CDocTemplate*)nResult : NULL;
}

void CDocManager::OnFileNew() {
  if (m_templateList.IsEmpty()) {
    AfxMessageBox(AFX_IDP_FAILED_TO_CREATE_DOC);
    return;
  }
  CDocTemplate* pTemplate = (CDocTemplate*)m_templateList.GetHead();
  if (m_templateList.GetCount() > 1) {
    pTemplate = _AfxChooseNewType(m_templateList);
    if (pTemplate == NULL) return;  // cancelled
  }
  pTemplate->OpenDocumentFile(NULL);
}

void CDocManager::OnFileOpen() {
  CString newName;
  if (!DoPromptFileName(newName, AFX_IDS_OPENFILE, OFN_HIDEREADONLY | OFN_FILEMUSTEXIST, TRUE,
                        NULL))
    return;
  CWinApp* pApp = AfxGetApp();
  if (pApp != NULL) pApp->OpenDocumentFile(newName);
}
