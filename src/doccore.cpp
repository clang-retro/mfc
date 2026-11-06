// doccore.cpp - CDocument.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
#include "afximpl.h"

IMPLEMENT_DYNAMIC(CDocument, CCmdTarget)

BEGIN_MESSAGE_MAP(CDocument, CCmdTarget)
  ON_COMMAND(ID_FILE_CLOSE, OnFileClose)
  ON_COMMAND(ID_FILE_SAVE, OnFileSave)
  ON_COMMAND(ID_FILE_SAVE_AS, OnFileSaveAs)
END_MESSAGE_MAP()

CDocument::CDocument() {
  m_pDocTemplate = NULL;
  m_bModified = FALSE;
  m_bAutoDelete = TRUE;
  m_bEmbedded = FALSE;
}

CDocument::~CDocument() {
  if (m_pDocTemplate != NULL) m_pDocTemplate->RemoveDocument(this);
}

void CDocument::OnFinalRelease() { OnCloseDocument(); }

// ---------------------------------------------------------------------
// Title and path

void CDocument::SetTitle(LPCTSTR lpszTitle) {
  m_strTitle = lpszTitle;
  UpdateFrameCounts();
}

void CDocument::SetPathName(LPCTSTR lpszPathName, BOOL bAddToMRU) {
  TCHAR szFullPath[MAX_PATH];
  if (!AfxFullPath(szFullPath, lpszPathName)) lstrcpyn(szFullPath, lpszPathName, MAX_PATH);
  m_strPathName = szFullPath;
  m_bEmbedded = FALSE;

  TCHAR szTitle[MAX_PATH];
  if (AfxGetFileTitle(szFullPath, szTitle, MAX_PATH) == 0) SetTitle(szTitle);

  if (bAddToMRU) {
    CWinApp* pApp = AfxGetApp();
    if (pApp != NULL) pApp->AddToRecentFileList(m_strPathName);
  }
}

// ---------------------------------------------------------------------
// Views

void CDocument::AddView(CView* pView) {
  m_viewList.AddTail(pView);
  pView->m_pDocument = this;
  OnChangedViewList();
}

void CDocument::RemoveView(CView* pView) {
  POSITION pos = m_viewList.Find(pView);
  if (pos != NULL) m_viewList.RemoveAt(pos);
  pView->m_pDocument = NULL;
  OnChangedViewList();
}

void CDocument::OnChangedViewList() {
  // A document with no views left closes itself.
  if (m_viewList.IsEmpty() && m_bAutoDelete) {
    OnCloseDocument();
    return;
  }
  UpdateFrameCounts();
}

void CDocument::UpdateAllViews(CView* pSender, LPARAM lHint, CObject* pHint) {
  POSITION pos = GetFirstViewPosition();
  while (pos != NULL) {
    CView* pView = GetNextView(pos);
    if (pView != pSender) pView->OnUpdate(pSender, lHint, pHint);
  }
}

void CDocument::SendInitialUpdate() {
  POSITION pos = GetFirstViewPosition();
  while (pos != NULL) GetNextView(pos)->OnInitialUpdate();
}

void CDocument::UpdateFrameCounts() {
  // Number the frames showing this document (":1", ":2") when there is
  // more than one, and refresh their titles.
  POSITION pos = GetFirstViewPosition();
  while (pos != NULL) {
    CFrameWnd* pFrame = GetNextView(pos)->GetParentFrame();
    if (pFrame != NULL) pFrame->m_nWindow = -1;
  }
  int nFrames = 0;
  pos = GetFirstViewPosition();
  while (pos != NULL) {
    CFrameWnd* pFrame = GetNextView(pos)->GetParentFrame();
    if (pFrame != NULL && pFrame->m_nWindow == -1) {
      pFrame->m_nWindow = 0;
      nFrames++;
    }
  }
  pos = GetFirstViewPosition();
  while (pos != NULL) {
    CFrameWnd* pFrame = GetNextView(pos)->GetParentFrame();
    if (pFrame != NULL) pFrame->m_nWindow = -1;
  }
  int iFrame = 1;
  pos = GetFirstViewPosition();
  while (pos != NULL) {
    CFrameWnd* pFrame = GetNextView(pos)->GetParentFrame();
    if (pFrame != NULL && pFrame->m_nWindow == -1) {
      pFrame->m_nWindow = nFrames == 1 ? 0 : iFrame++;
      pFrame->OnUpdateFrameTitle(TRUE);
    }
  }
}

// ---------------------------------------------------------------------
// Loading and saving

void CDocument::DeleteContents() {}

BOOL CDocument::OnNewDocument() {
  DeleteContents();
  m_strPathName.Empty();
  SetModifiedFlag(FALSE);
  return TRUE;
}

CFile* CDocument::GetFile(LPCTSTR lpszFileName, UINT nOpenFlags, CFileException* pError) {
  CFile* pFile = new CFile;
  if (!pFile->Open(lpszFileName, nOpenFlags, pError)) {
    delete pFile;
    return NULL;
  }
  return pFile;
}

void CDocument::ReleaseFile(CFile* pFile, BOOL bAbort) {
  if (bAbort)
    pFile->Abort();
  else
    pFile->Close();
  delete pFile;
}

BOOL CDocument::OnOpenDocument(LPCTSTR lpszPathName) {
  CFileException fe;
  CFile* pFile = GetFile(lpszPathName, CFile::modeRead | CFile::shareDenyWrite, &fe);
  if (pFile == NULL) {
    ReportSaveLoadException(lpszPathName, &fe, FALSE, AFX_IDP_FAILED_TO_OPEN_DOC);
    return FALSE;
  }

  DeleteContents();
  SetModifiedFlag();  // dirty while loading

  CArchive loadArchive(pFile, CArchive::load | CArchive::bNoFlushOnDelete);
  loadArchive.m_pDocument = this;
  loadArchive.m_bForceFlat = FALSE;
  try {
    CWaitCursor wait;
    if (pFile->GetLength() != 0) Serialize(loadArchive);
    loadArchive.Close();
    ReleaseFile(pFile, FALSE);
  } catch (CException* e) {
    loadArchive.Abort();
    ReleaseFile(pFile, TRUE);
    DeleteContents();
    ReportSaveLoadException(lpszPathName, e, FALSE, AFX_IDP_FAILED_TO_OPEN_DOC);
    e->Delete();
    return FALSE;
  }

  SetModifiedFlag(FALSE);
  return TRUE;
}

BOOL CDocument::OnSaveDocument(LPCTSTR lpszPathName) {
  CFileException fe;
  CFile* pFile = GetFile(lpszPathName,
                         CFile::modeCreate | CFile::modeReadWrite | CFile::shareExclusive, &fe);
  if (pFile == NULL) {
    ReportSaveLoadException(lpszPathName, &fe, TRUE, AFX_IDP_INVALID_FILENAME);
    return FALSE;
  }

  CArchive saveArchive(pFile, CArchive::store | CArchive::bNoFlushOnDelete);
  saveArchive.m_pDocument = this;
  saveArchive.m_bForceFlat = FALSE;
  try {
    CWaitCursor wait;
    Serialize(saveArchive);
    saveArchive.Close();
    ReleaseFile(pFile, FALSE);
  } catch (CException* e) {
    saveArchive.Abort();
    ReleaseFile(pFile, TRUE);
    ReportSaveLoadException(lpszPathName, e, TRUE, AFX_IDP_FAILED_TO_SAVE_DOC);
    e->Delete();
    return FALSE;
  }

  SetModifiedFlag(FALSE);
  return TRUE;
}

void CDocument::ReportSaveLoadException(LPCTSTR lpszPathName, CException* e, BOOL bSaving,
                                        UINT nIDPDefault) {
  UINT nIDP = nIDPDefault;
  UINT nHelpContext = nIDPDefault;
  CString prompt;

  if (e != NULL) {
    if (e->IsKindOf(RUNTIME_CLASS(CUserException))) return;  // already reported

    if (e->IsKindOf(RUNTIME_CLASS(CArchiveException))) {
      switch (((CArchiveException*)e)->m_cause) {
        case CArchiveException::badSchema:
        case CArchiveException::badClass:
        case CArchiveException::badIndex:
        case CArchiveException::endOfFile:
          nIDP = AFX_IDP_FAILED_INVALID_FORMAT;
          break;
        default:
          break;
      }
    } else if (e->IsKindOf(RUNTIME_CLASS(CFileException))) {
      CFileException* pFE = (CFileException*)e;
      if (pFE->m_strFileName.IsEmpty()) pFE->m_strFileName = lpszPathName;
      switch (pFE->m_cause) {
        case CFileException::fileNotFound:
        case CFileException::badPath:
          nIDP = AFX_IDP_FAILED_INVALID_PATH;
          break;
        case CFileException::diskFull:
          nIDP = AFX_IDP_FAILED_DISK_FULL;
          break;
        case CFileException::accessDenied:
        case CFileException::sharingViolation:
          nIDP = bSaving ? AFX_IDP_FAILED_ACCESS_WRITE : AFX_IDP_FAILED_ACCESS_READ;
          break;
        case CFileException::endOfFile:
        case CFileException::hardIO:
          nIDP = bSaving ? AFX_IDP_FAILED_IO_ERROR_WRITE : AFX_IDP_FAILED_IO_ERROR_READ;
          break;
        default:
          break;
      }
    }
  }

  TCHAR szTitle[MAX_PATH];
  if (lpszPathName == NULL || AfxGetFileTitle(lpszPathName, szTitle, MAX_PATH) != 0)
    lstrcpyn(szTitle, lpszPathName != NULL ? lpszPathName : _T(""), MAX_PATH);
  AfxFormatString1(prompt, nIDP, szTitle);
  AfxMessageBox(prompt, MB_ICONEXCLAMATION, nHelpContext);
}

// ---------------------------------------------------------------------
// Closing

void CDocument::OnCloseDocument() {
  // Destroying the frames destroys the views, which would otherwise
  // delete the document halfway through.
  BOOL bAutoDelete = m_bAutoDelete;
  m_bAutoDelete = FALSE;
  while (!m_viewList.IsEmpty()) {
    CView* pView = (CView*)m_viewList.GetHead();
    CFrameWnd* pFrame = pView->GetParentFrame();
    if (pFrame == NULL) {
      // A view without a frame: destroy it directly.
      if (pView->m_hWnd != NULL)
        pView->DestroyWindow();
      else
        RemoveView(pView);
      continue;
    }
    PreCloseFrame(pFrame);
    pFrame->DestroyWindow();
    // Guard against a frame that did not take the view with it.
    if (!m_viewList.IsEmpty() && m_viewList.GetHead() == pView) RemoveView(pView);
  }
  m_bAutoDelete = bAutoDelete;

  DeleteContents();
  if (m_bAutoDelete) delete this;
}

BOOL CDocument::CanCloseFrame(CFrameWnd* pFrameArg) {
  // Only the last frame showing the document asks to save it.
  POSITION pos = GetFirstViewPosition();
  while (pos != NULL) {
    CFrameWnd* pFrame = GetNextView(pos)->GetParentFrame();
    if (pFrame != NULL && pFrame != pFrameArg) return TRUE;
  }
  return SaveModified();
}

void CDocument::PreCloseFrame(CFrameWnd* pFrame) { (void)pFrame; }

BOOL CDocument::SaveModified() {
  if (!IsModified()) return TRUE;

  CString name;
  if (m_strPathName.IsEmpty()) {
    name = m_strTitle;
    if (name.IsEmpty()) AfxLoadString(AFX_IDS_UNTITLED, name);
  } else {
    TCHAR szTitle[MAX_PATH];
    if (AfxGetFileTitle(m_strPathName, szTitle, MAX_PATH) == 0)
      name = szTitle;
    else
      name = m_strPathName;
  }

  CString prompt;
  AfxFormatString1(prompt, AFX_IDP_ASK_TO_SAVE, name);
  switch (AfxMessageBox(prompt, MB_YESNOCANCEL, AFX_IDP_ASK_TO_SAVE)) {
    case IDCANCEL:
      return FALSE;
    case IDYES:
      return DoFileSave();
    default:
      break;
  }
  return TRUE;
}

BOOL CDocument::DoFileSave() {
  DWORD dwAttrib = m_strPathName.IsEmpty() ? INVALID_FILE_ATTRIBUTES
                                           : ::GetFileAttributes(m_strPathName);
  if (m_strPathName.IsEmpty() ||
      (dwAttrib != INVALID_FILE_ATTRIBUTES && (dwAttrib & FILE_ATTRIBUTE_READONLY))) {
    // No name yet, or read-only: ask for a new one.
    return DoSave(NULL);
  }
  return DoSave(m_strPathName);
}

BOOL CDocument::DoSave(LPCTSTR lpszPathName, BOOL bReplace) {
  CString newName = lpszPathName;
  if (newName.IsEmpty()) {
    CDocTemplate* pTemplate = GetDocTemplate();
    newName = m_strPathName;
    if (bReplace && newName.IsEmpty()) {
      // Suggest the title, without characters a file name cannot have,
      // with the template's extension.
      newName = m_strTitle;
      int iBad = newName.FindOneOf(_T(":/\\*?\"<>|"));
      if (iBad != -1) newName = newName.Left(iBad);
      CString strExt;
      if (pTemplate != NULL && pTemplate->GetDocString(strExt, CDocTemplate::filterExt) &&
          !strExt.IsEmpty()) {
        int iStart = 0;
        CString strFirst = strExt.Tokenize(_T(";"), iStart);
        if (!strFirst.IsEmpty() && strFirst[0] == _T('*')) strFirst = strFirst.Mid(1);
        newName += strFirst;
      }
    }
    CWinApp* pApp = AfxGetApp();
    if (pApp == NULL ||
        !pApp->DoPromptFileName(newName, bReplace ? AFX_IDS_SAVEFILE : AFX_IDS_SAVEFILECOPY,
                                OFN_HIDEREADONLY | OFN_PATHMUSTEXIST, FALSE, pTemplate))
      return FALSE;
  }

  CWaitCursor wait;
  if (!OnSaveDocument(newName)) return FALSE;
  if (bReplace) SetPathName(newName);
  return TRUE;
}

HMENU CDocument::GetDefaultMenu() { return NULL; }

HACCEL CDocument::GetDefaultAccelerator() { return NULL; }

void CDocument::OnIdle() {}

BOOL CDocument::OnCmdMsg(UINT nID, int nCode, void* pExtra, AFX_CMDHANDLERINFO* pHandlerInfo) {
  if (CCmdTarget::OnCmdMsg(nID, nCode, pExtra, pHandlerInfo)) return TRUE;
  if (m_pDocTemplate != NULL && m_pDocTemplate->OnCmdMsg(nID, nCode, pExtra, pHandlerInfo))
    return TRUE;
  return FALSE;
}

// ---------------------------------------------------------------------
// Commands

void CDocument::OnFileClose() {
  if (!SaveModified()) return;
  OnCloseDocument();
}

void CDocument::OnFileSave() { DoFileSave(); }

void CDocument::OnFileSaveAs() { DoSave(NULL); }

// Mail needs MAPI, which may not be installed; report it as unavailable.
void CDocument::OnFileSendMail() { AfxMessageBox(AFX_IDP_COMMAND_FAILURE); }

void CDocument::OnUpdateFileSendMail(CCmdUI* pCmdUI) { pCmdUI->Enable(FALSE); }
