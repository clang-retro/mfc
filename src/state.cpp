// state.cpp - per-thread and per-process state, handle maps, resources.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include "afximpl.h"

// ---------------------------------------------------------------------
// Process state

static AFX_MODULE_STATE g_moduleState;

AFX_MODULE_STATE* AFXAPI AfxGetModuleState() { return &g_moduleState; }

HINSTANCE AFXAPI AfxGetInstanceHandle() {
  if (g_moduleState.m_hCurrentInstanceHandle == NULL)
    g_moduleState.m_hCurrentInstanceHandle = ::GetModuleHandle(NULL);
  return g_moduleState.m_hCurrentInstanceHandle;
}

HINSTANCE AFXAPI AfxGetResourceHandle() {
  if (g_moduleState.m_hCurrentResourceHandle == NULL)
    return AfxGetInstanceHandle();
  return g_moduleState.m_hCurrentResourceHandle;
}

void AFXAPI AfxSetResourceHandle(HINSTANCE hInstResource) {
  g_moduleState.m_hCurrentResourceHandle = hInstResource;
}

HINSTANCE AFXAPI AfxFindResourceHandle(LPCTSTR lpszName, LPCTSTR lpszType) {
  (void)lpszName;
  (void)lpszType;
  return AfxGetResourceHandle();
}

LPCTSTR AFXAPI AfxGetAppName() { return g_moduleState.m_lpszCurrentAppName; }

CWinApp* AFXAPI AfxGetApp() { return g_moduleState.m_pCurrentWinApp; }

// ---------------------------------------------------------------------
// Thread state

static DWORD g_tlsIndex = TLS_OUT_OF_INDEXES;
static CRITICAL_SECTION g_csGlobals[4];
static LONG g_nGlobalsInit = 0;  // 0 = not started, 1 = starting, 2 = ready

static void InitGlobals() {
  if (g_nGlobalsInit == 2) return;
  if (::InterlockedCompareExchange(&g_nGlobalsInit, 1, 0) == 0) {
    g_tlsIndex = ::TlsAlloc();
    for (int i = 0; i < 4; i++) ::InitializeCriticalSection(&g_csGlobals[i]);
    ::InterlockedExchange(&g_nGlobalsInit, 2);
  } else {
    while (g_nGlobalsInit != 2) ::Sleep(0);
  }
}

AFX_THREAD_STATE* AFXAPI AfxGetThreadState() {
  InitGlobals();
  AFX_THREAD_STATE* pState = (AFX_THREAD_STATE*)::TlsGetValue(g_tlsIndex);
  if (pState == NULL) {
    pState = (AFX_THREAD_STATE*)::calloc(1, sizeof(AFX_THREAD_STATE));
    if (pState == NULL) AfxThrowMemoryException();
    new (&pState->m_ptCursorLast) CPoint(-1, -1);
    ::TlsSetValue(g_tlsIndex, pState);
  }
  return pState;
}

void AFXAPI AfxTermThreadState() {
  if (g_nGlobalsInit != 2) return;
  AFX_THREAD_STATE* pState = (AFX_THREAD_STATE*)::TlsGetValue(g_tlsIndex);
  if (pState == NULL) return;
  if (pState->m_hHookOldCbtFilter != NULL) ::UnhookWindowsHookEx(pState->m_hHookOldCbtFilter);
  if (pState->m_hHookOldMsgFilter != NULL) ::UnhookWindowsHookEx(pState->m_hHookOldMsgFilter);
  delete pState->m_pmapHWND;
  delete pState->m_pmapHMENU;
  delete pState->m_pmapHDC;
  delete pState->m_pmapHGDIOBJ;
  delete pState->m_pmapHIMAGELIST;
  if (pState->m_pSockState != NULL) pState->m_pfnSockTerm(pState->m_pSockState);
  ::free(pState);
  ::TlsSetValue(g_tlsIndex, NULL);
}

void AFXAPI AfxLockGlobals(int nLockType) {
  InitGlobals();
  ::EnterCriticalSection(&g_csGlobals[nLockType & 3]);
}

void AFXAPI AfxUnlockGlobals(int nLockType) {
  ::LeaveCriticalSection(&g_csGlobals[nLockType & 3]);
}

CWinThread* AFXAPI AfxGetThread() {
  CWinThread* pThread = AfxGetThreadState()->m_pCurrentWinThread;
  return pThread;
}

CWnd* AFXAPI AfxGetMainWnd() {
  CWinThread* pThread = AfxGetThread();
  if (pThread == NULL) pThread = AfxGetApp();
  return pThread != NULL ? pThread->GetMainWnd() : NULL;
}

// ---------------------------------------------------------------------
// Handle maps

CHandleMap::CHandleMap(CRuntimeClass* pClass, size_t nOffset, int nHandles)
    : m_pClass(pClass), m_nOffset(nOffset), m_nHandles(nHandles) {
  m_permanentMap.InitHashTable(251);
  m_temporaryMap.InitHashTable(53);
}

CObject* CHandleMap::LookupPermanent(HANDLE h) {
  void* p;
  return m_permanentMap.Lookup(h, p) ? (CObject*)p : NULL;
}

CObject* CHandleMap::LookupTemporary(HANDLE h) {
  void* p;
  return m_temporaryMap.Lookup(h, p) ? (CObject*)p : NULL;
}

CObject* CHandleMap::FromHandle(HANDLE h) {
  if (h == NULL) return NULL;
  CObject* pObject = LookupPermanent(h);
  if (pObject != NULL) return pObject;
  pObject = LookupTemporary(h);
  if (pObject != NULL) return pObject;

  // Make a temporary wrapper. The object is constructed normally, which
  // for these classes only clears the handles.
  pObject = m_pClass->CreateObject();
  if (pObject == NULL) AfxThrowMemoryException();
  HANDLE* ph = (HANDLE*)((BYTE*)pObject + m_nOffset);
  ph[0] = h;
  if (m_nHandles == 2) ph[1] = h;
  m_temporaryMap.SetAt(h, pObject);
  return pObject;
}

void CHandleMap::SetPermanent(HANDLE h, CObject* pObject) { m_permanentMap[h] = pObject; }

void CHandleMap::RemoveHandle(HANDLE h) {
  m_permanentMap.RemoveKey(h);
}

void CHandleMap::DeleteTemp() {
  POSITION pos = m_temporaryMap.GetStartPosition();
  while (pos != NULL) {
    void* h;
    void* p;
    m_temporaryMap.GetNextAssoc(pos, h, p);
    CObject* pObject = (CObject*)p;
    HANDLE* ph = (HANDLE*)((BYTE*)pObject + m_nOffset);
    ph[0] = NULL;  // so the destructor leaves the handle alone
    if (m_nHandles == 2) ph[1] = NULL;
    delete pObject;
  }
  m_temporaryMap.RemoveAll();
}

CHandleMap* AFXAPI afxMapHWND(BOOL bCreate) {
  AFX_THREAD_STATE* pState = AfxGetThreadState();
  if (pState->m_pmapHWND == NULL && bCreate)
    pState->m_pmapHWND = new CHandleMap(RUNTIME_CLASS(CWnd), offsetof(CWnd, m_hWnd));
  return pState->m_pmapHWND;
}

CHandleMap* AFXAPI afxMapHMENU(BOOL bCreate) {
  AFX_THREAD_STATE* pState = AfxGetThreadState();
  if (pState->m_pmapHMENU == NULL && bCreate)
    pState->m_pmapHMENU = new CHandleMap(RUNTIME_CLASS(CMenu), offsetof(CMenu, m_hMenu));
  return pState->m_pmapHMENU;
}

CHandleMap* AFXAPI afxMapHDC(BOOL bCreate) {
  AFX_THREAD_STATE* pState = AfxGetThreadState();
  if (pState->m_pmapHDC == NULL && bCreate)
    pState->m_pmapHDC = new CHandleMap(RUNTIME_CLASS(CDC), offsetof(CDC, m_hDC), 2);
  return pState->m_pmapHDC;
}

CHandleMap* AFXAPI afxMapHGDIOBJ(BOOL bCreate) {
  AFX_THREAD_STATE* pState = AfxGetThreadState();
  if (pState->m_pmapHGDIOBJ == NULL && bCreate)
    pState->m_pmapHGDIOBJ =
        new CHandleMap(RUNTIME_CLASS(CGdiObject), offsetof(CGdiObject, m_hObject));
  return pState->m_pmapHGDIOBJ;
}

void AFXAPI AfxLockTempMaps() { ++AfxGetThreadState()->m_nTempMapLock; }

BOOL AFXAPI AfxUnlockTempMaps(BOOL bDeleteTemps) {
  AFX_THREAD_STATE* pState = AfxGetThreadState();
  if (pState->m_nTempMapLock != 0 && --pState->m_nTempMapLock == 0) {
    if (bDeleteTemps) {
      // Windows first: their destructors may touch the other maps.
      if (pState->m_pmapHWND != NULL) pState->m_pmapHWND->DeleteTemp();
      if (pState->m_pmapHMENU != NULL) pState->m_pmapHMENU->DeleteTemp();
      if (pState->m_pmapHDC != NULL) pState->m_pmapHDC->DeleteTemp();
      if (pState->m_pmapHGDIOBJ != NULL) pState->m_pmapHGDIOBJ->DeleteTemp();
      if (pState->m_pmapHIMAGELIST != NULL) pState->m_pmapHIMAGELIST->DeleteTemp();
    }
  }
  return pState->m_nTempMapLock != 0;
}

// ---------------------------------------------------------------------
// Built-in text for the framework's own string IDs, used when the
// program's resources do not include afxres.rc.

struct AfxDefaultString {
  UINT nID;
  const char* psz;
};

static const AfxDefaultString g_defaultStrings[] = {
    {AFX_IDS_IDLEMESSAGE, "Ready"},
    {AFX_IDS_HELPMODEMESSAGE, "Select an object on which to get Help"},
    {AFX_IDS_OPENFILE, "Open"},
    {AFX_IDS_SAVEFILE, "Save As"},
    {AFX_IDS_ALLFILTER, "All Files (*.*)"},
    {AFX_IDS_UNTITLED, "Untitled"},
    {AFX_IDS_SAVEFILECOPY, "Save As"},
    {AFX_IDS_PREVIEW_CLOSE, "&Close"},
    {AFX_IDS_UNNAMED_FILE, "an unnamed file"},
    {AFX_IDS_HIDE, "&Hide"},
    {AFX_IDP_NO_ERROR_AVAILABLE, "No error message is available."},
    {AFX_IDS_NOT_SUPPORTED_EXCEPTION, "An unsupported operation was attempted."},
    {AFX_IDS_RESOURCE_EXCEPTION, "A required resource was unavailable."},
    {AFX_IDS_MEMORY_EXCEPTION, "Out of memory."},
    {AFX_IDS_USER_EXCEPTION, "An unknown error has occurred."},
    {AFX_IDS_INVALID_ARG_EXCEPTION, "Encountered an improper argument."},
    {AFX_IDP_INVALID_FILENAME, "Invalid filename."},
    {AFX_IDP_FAILED_TO_OPEN_DOC, "Failed to open document."},
    {AFX_IDP_FAILED_TO_SAVE_DOC, "Failed to save document."},
    {AFX_IDP_ASK_TO_SAVE, "Save changes to %1?"},
    {AFX_IDP_FAILED_TO_CREATE_DOC, "Failed to create empty document."},
    {AFX_IDP_FILE_TOO_LARGE, "The file is too large to open."},
    {AFX_IDP_FAILED_TO_START_PRINT, "Could not start print job."},
    {AFX_IDP_FAILED_TO_LAUNCH_HELP, "Failed to launch help."},
    {AFX_IDP_INTERNAL_FAILURE, "Internal application error."},
    {AFX_IDP_COMMAND_FAILURE, "Command failed."},
    {AFX_IDP_FAILED_MEMORY_ALLOC, "Insufficient memory to perform operation."},
    {AFX_IDP_PARSE_INT, "Please enter an integer."},
    {AFX_IDP_PARSE_REAL, "Please enter a number."},
    {AFX_IDP_PARSE_INT_RANGE, "Please enter an integer between %1 and %2."},
    {AFX_IDP_PARSE_REAL_RANGE, "Please enter a number between %1 and %2."},
    {AFX_IDP_PARSE_STRING_SIZE, "Please enter no more than %1 characters."},
    {AFX_IDP_PARSE_RADIO_BUTTON, "Please select a button."},
    {AFX_IDP_PARSE_BYTE, "Please enter an integer between 0 and 255."},
    {AFX_IDP_PARSE_UINT, "Please enter a positive integer."},
    {AFX_IDP_PARSE_DATETIME, "Please enter a date and/or time."},
    {AFX_IDP_PARSE_CURRENCY, "Please enter a currency."},
    {AFX_IDP_FAILED_INVALID_FORMAT, "Unexpected file format."},
    {AFX_IDP_FAILED_INVALID_PATH,
     "%1\nCannot find this file.\nPlease verify that the correct path and file name are given."},
    {AFX_IDP_FAILED_DISK_FULL, "Destination disk drive is full."},
    {AFX_IDP_FAILED_ACCESS_READ, "Unable to read from %1, it is opened by someone else."},
    {AFX_IDP_FAILED_ACCESS_WRITE,
     "Unable to write to %1, it is read-only or opened by someone else."},
    {AFX_IDP_FAILED_IO_ERROR_READ, "An unexpected error occurred while reading %1."},
    {AFX_IDP_FAILED_IO_ERROR_WRITE, "An unexpected error occurred while writing %1."},
    {AFX_IDP_STATIC_OBJECT, "A static object cannot be activated."},
    {AFX_IDP_FAILED_TO_CONNECT, "Failed to connect to the object.\nThe link may be broken."},
    {AFX_IDP_SERVER_BUSY, "The server application is busy and cannot process the command."},
    {AFX_IDP_BAD_VERB, "The server application could not carry out the command."},
    {AFX_IDP_FAILED_TO_NOTIFY, "Failed to notify the container."},
    {AFX_IDP_FAILED_TO_LAUNCH, "Failed to start the server application."},
    {AFX_IDP_ASK_TO_UPDATE, "Update %1 before going on?"},
    {AFX_IDP_FAILED_TO_UPDATE, "Could not update the object."},
    {AFX_IDP_FAILED_TO_CONVERT, "Failed to convert the object."},
    {AFX_IDP_FAILED_TO_CREATE,
     "Failed to create the object. Check that its application is registered."},
    {AFX_IDS_UNKNOWNTYPE, "Unknown Type"},
    {AFX_IDS_EMBED_FORMAT, "%s\nInserts the contents of the clipboard as %s."},
    {AFX_IDS_LINKSOURCE_FORMAT,
     "%s\nInserts a picture of the contents of the clipboard, linked to its source so that "
     "changes to the source show in your document."},
    {AFX_IDS_METAFILE_FORMAT, "Picture (Metafile)\nInserts the contents of the clipboard as a picture."},
    {AFX_IDS_DIB_FORMAT,
     "Device Independent Bitmap\nInserts the contents of the clipboard as a bitmap."},
    {AFX_IDS_BITMAP_FORMAT, "Bitmap\nInserts the contents of the clipboard as a bitmap."},
    {AFX_IDS_UPDATING_ITEMS, "Updating links"},
    {AFX_IDS_RTF_FORMAT_TEXT,
     "Unformatted Text\nInserts the contents of the clipboard as text without any formatting."},
    {AFX_IDS_UPDATE_MENU, "&Update %1"},
    {AFX_IDS_EXIT_MENU, "E&xit && Return to %1"},
    {AFX_IDS_SCSIZE, "Change the window size"},
    {AFX_IDS_SCMOVE, "Change the window position"},
    {AFX_IDS_SCMINIMIZE, "Reduce the window to an icon"},
    {AFX_IDS_SCMAXIMIZE, "Enlarge the window to full size"},
    {AFX_IDS_SCNEXTWINDOW, "Switch to the next document window"},
    {AFX_IDS_SCPREVWINDOW, "Switch to the previous document window"},
    {AFX_IDS_SCCLOSE, "Close the active window and prompts to save the documents"},
    {AFX_IDS_SCRESTORE, "Restore the window to normal size"},
    {AFX_IDS_SCTASKLIST, "Activate Task List"},
    {AFX_IDS_MDICHILD, "Activate this window"},
    {AFX_IDS_PRINTONPORT, "on %1"},
    {AFX_IDS_ONEPAGE, "&One Page"},
    {AFX_IDS_TWOPAGE, "&Two Page"},
    {AFX_IDS_PRINTPAGENUM, "Page %u"},
    {AFX_IDS_PREVIEWPAGEDESC, "Page %u\nPages %u-%u\n"},
    {AFX_IDS_PRINTDEFAULTEXT, "prn"},
    {AFX_IDS_PRINTDEFAULT, "Output.prn"},
    {AFX_IDS_PRINTFILTER, "Printer Files (*.prn)|*.prn|All Files (*.*)|*.*||"},
    {AFX_IDS_PRINTCAPTION, "Print to File"},
    {AFX_IDS_PRINTTOFILE, "to %1"},
    {AFX_ID_PREVIEW_CLOSE, "Close print preview mode\nCancel Preview"},
    {AFX_ID_PREVIEW_NUMPAGE, "Switch between one and two page display\nOne Page/Two Pages"},
    {AFX_ID_PREVIEW_NEXT, "Go to next page\nNext Page"},
    {AFX_ID_PREVIEW_PREV, "Go to previous page\nPrevious Page"},
    {AFX_ID_PREVIEW_PRINT, "Print the previewed item\nPrint"},
    {AFX_ID_PREVIEW_ZOOMIN, "Increase magnification of preview page\nZoom In"},
    {AFX_ID_PREVIEW_ZOOMOUT, "Decrease magnification of preview page\nZoom Out"},
    {ID_INDICATOR_EXT, "EXT"},
    {ID_INDICATOR_CAPS, "CAP"},
    {ID_INDICATOR_NUM, "NUM"},
    {ID_INDICATOR_SCRL, "SCRL"},
    {ID_INDICATOR_OVR, "OVR"},
    {ID_INDICATOR_REC, "REC"},
};

LPCTSTR AFXAPI _AfxLoadDefaultString(UINT nID) {
  // Converted lazily; the table is small and the strings live forever.
  static CString* s_cache[sizeof(g_defaultStrings) / sizeof(g_defaultStrings[0])];
  for (size_t i = 0; i < sizeof(g_defaultStrings) / sizeof(g_defaultStrings[0]); i++) {
    if (g_defaultStrings[i].nID == nID) {
      if (s_cache[i] == NULL) s_cache[i] = new CString(g_defaultStrings[i].psz);
      return *s_cache[i];
    }
  }
  return NULL;
}

// Loads one of the framework's strings, falling back to built-in text.
BOOL AFXAPI AfxLoadString(UINT nIDS, CString& rString) {
  if (rString.LoadString(nIDS)) return TRUE;
  LPCTSTR psz = _AfxLoadDefaultString(nIDS);
  if (psz == NULL) return FALSE;
  rString = psz;
  return TRUE;
}

// ---------------------------------------------------------------------
// String formatting with %1..%9 placeholders

void AFXAPI AfxFormatStrings(CString& rString, LPCTSTR lpszFormat, LPCTSTR const* rglpsz,
                             int nString) {
  CString result;
  for (LPCTSTR p = lpszFormat; *p != 0; p++) {
    if (*p == '%' && p[1] >= '1' && p[1] <= '9') {
      int i = p[1] - '1';
      if (i < nString && rglpsz[i] != NULL) result += rglpsz[i];
      p++;
    } else {
      result += *p;
    }
  }
  rString = result;
}

void AFXAPI AfxFormatStrings(CString& rString, UINT nIDS, LPCTSTR const* rglpsz, int nString) {
  CString strFormat;
  if (!AfxLoadString(nIDS, strFormat)) {
    rString.Empty();
    return;
  }
  AfxFormatStrings(rString, strFormat, rglpsz, nString);
}

void AFXAPI AfxFormatString1(CString& rString, UINT nIDS, LPCTSTR lpsz1) {
  AfxFormatStrings(rString, nIDS, &lpsz1, 1);
}

void AFXAPI AfxFormatString2(CString& rString, UINT nIDS, LPCTSTR lpsz1, LPCTSTR lpsz2) {
  LPCTSTR rglpsz[2] = {lpsz1, lpsz2};
  AfxFormatStrings(rString, nIDS, rglpsz, 2);
}


void AFXAPI AfxGlobalFree(HGLOBAL hGlobal) {
  if (hGlobal == NULL) return;
  UINT nCount = ::GlobalFlags(hGlobal) & GMEM_LOCKCOUNT;
  while (nCount--) ::GlobalUnlock(hGlobal);
  ::GlobalFree(hGlobal);
}

void AFXAPI AfxDeleteObject(HGDIOBJ* pObject) {
  if (*pObject != NULL) {
    ::DeleteObject(*pObject);
    *pObject = NULL;
  }
}

BOOL AFXAPI AfxInitRichEdit() { return ::LoadLibraryA("riched32.dll") != NULL; }
BOOL AFXAPI AfxInitRichEdit2() { return ::LoadLibraryA("riched20.dll") != NULL; }
