// state.cpp - per-thread and per-process state, handle maps, resources.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include "afximpl.h"

// ---------------------------------------------------------------------
// Process state

static AFX_MODULE_STATE g_moduleState;  // the program's (and its extension DLLs')

AFX_MODULE_STATE* AFXAPI AfxGetAppModuleState() { return &g_moduleState; }

AFX_MODULE_STATE* AFXAPI AfxGetModuleState() {
#ifdef _AFX_BUILDING_DLL
  // A regular DLL's state while its code runs (AFX_MANAGE_STATE).
  AFX_MODULE_STATE* pState = AfxGetThreadState()->m_pModuleState;
  if (pState != NULL) return pState;
#endif
  return &g_moduleState;
}

AFX_MODULE_STATE* AFXAPI AfxSetModuleState(AFX_MODULE_STATE* pNewState) {
  AFX_THREAD_STATE* pThreadState = AfxGetThreadState();
  AFX_MODULE_STATE* pOldState = pThreadState->m_pModuleState;
  pThreadState->m_pModuleState = pNewState == &g_moduleState ? NULL : pNewState;
  return pOldState != NULL ? pOldState : &g_moduleState;
}

HINSTANCE AFXAPI AfxGetInstanceHandle() {
  AFX_MODULE_STATE* pState = AfxGetModuleState();
  if (pState->m_hCurrentInstanceHandle == NULL)
    pState->m_hCurrentInstanceHandle = ::GetModuleHandle(NULL);
  return pState->m_hCurrentInstanceHandle;
}

HINSTANCE AFXAPI AfxGetResourceHandle() {
  AFX_MODULE_STATE* pState = AfxGetModuleState();
  if (pState->m_hCurrentResourceHandle == NULL) return AfxGetInstanceHandle();
  return pState->m_hCurrentResourceHandle;
}

void AFXAPI AfxSetResourceHandle(HINSTANCE hInstResource) {
  AfxGetModuleState()->m_hCurrentResourceHandle = hInstResource;
}

// The program's resources come first, then those of its extension DLLs
// (CDynLinkLibrary), then those of the DLLs marked as system DLLs.
HINSTANCE AFXAPI AfxFindResourceHandle(LPCTSTR lpszName, LPCTSTR lpszType) {
  HINSTANCE hInst = AfxGetResourceHandle();
  CDynLinkLibrary* pFirstDLL = AfxGetModuleState()->m_pFirstDLL;
  if (pFirstDLL == NULL || ::FindResource(hInst, lpszName, lpszType) != NULL)
    return hInst;
  for (int nPass = 0; nPass < 2; nPass++) {
    for (CDynLinkLibrary* p = pFirstDLL; p != NULL; p = p->m_pNextDLL) {
      if (p->m_bSystem != (nPass == 1) || p->m_hResource == NULL) continue;
      if (::FindResource(p->m_hResource, lpszName, lpszType) != NULL) return p->m_hResource;
    }
  }
  return hInst;
}

// Whether the string table of hInst has string nID (strings are kept in
// blocks of 16, each a length and that many characters).
static BOOL HasString(HINSTANCE hInst, UINT nID) {
  HRSRC hRes = ::FindResource(hInst, MAKEINTRESOURCE((nID >> 4) + 1), RT_STRING);
  if (hRes == NULL) return FALSE;
  HGLOBAL hGlobal = ::LoadResource(hInst, hRes);
  const WORD* p = hGlobal != NULL ? (const WORD*)::LockResource(hGlobal) : NULL;
  if (p == NULL) return FALSE;
  const WORD* pEnd = (const WORD*)((const BYTE*)p + ::SizeofResource(hInst, hRes));
  for (UINT i = 0; i < (nID & 15) && p < pEnd; i++) p += 1 + *p;
  return p < pEnd && *p != 0;
}

HINSTANCE AFXAPI AfxFindStringResourceHandle(UINT nID) {
  HINSTANCE hInst = AfxGetResourceHandle();
  CDynLinkLibrary* pFirstDLL = AfxGetModuleState()->m_pFirstDLL;
  if (pFirstDLL == NULL || HasString(hInst, nID)) return hInst;
  for (int nPass = 0; nPass < 2; nPass++) {
    for (CDynLinkLibrary* p = pFirstDLL; p != NULL; p = p->m_pNextDLL) {
      if (p->m_bSystem != (nPass == 1) || p->m_hResource == NULL) continue;
      if (HasString(p->m_hResource, nID)) return p->m_hResource;
    }
  }
  return hInst;
}

LPCTSTR AFXAPI AfxGetAppName() { return AfxGetModuleState()->m_lpszCurrentAppName; }

CWinApp* AFXAPI AfxGetApp() { return AfxGetModuleState()->m_pCurrentWinApp; }

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

static void DeleteMaps(_AFX_HANDLE_MAPS* pMaps);

void AFXAPI AfxTermThreadState() {
  if (g_nGlobalsInit != 2) return;
  AFX_THREAD_STATE* pState = (AFX_THREAD_STATE*)::TlsGetValue(g_tlsIndex);
  if (pState == NULL) return;
  if (pState->m_hHookOldCbtFilter != NULL) ::UnhookWindowsHookEx(pState->m_hHookOldCbtFilter);
  if (pState->m_hHookOldMsgFilter != NULL) ::UnhookWindowsHookEx(pState->m_hHookOldMsgFilter);
  DeleteMaps(&pState->m_maps);
  while (pState->m_pDllMaps != NULL) {
    _AFX_DLL_MAPS* p = pState->m_pDllMaps;
    pState->m_pDllMaps = p->m_pNext;
    DeleteMaps(p);
    ::free(p);
  }
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

_AFX_HANDLE_MAPS* AFXAPI _AfxGetHandleMaps() {
  AFX_THREAD_STATE* pState = AfxGetThreadState();
  AFX_MODULE_STATE* pModuleState = pState->m_pModuleState;
  if (pModuleState == NULL) return &pState->m_maps;
  for (_AFX_DLL_MAPS* p = pState->m_pDllMaps; p != NULL; p = p->m_pNext)
    if (p->m_pModuleState == pModuleState) return p;
  _AFX_DLL_MAPS* p = (_AFX_DLL_MAPS*)::calloc(1, sizeof(_AFX_DLL_MAPS));
  if (p == NULL) AfxThrowMemoryException();
  p->m_pModuleState = pModuleState;
  p->m_pNext = pState->m_pDllMaps;
  pState->m_pDllMaps = p;
  return p;
}

static void DeleteMaps(_AFX_HANDLE_MAPS* pMaps) {
  delete pMaps->m_pmapHWND;
  delete pMaps->m_pmapHMENU;
  delete pMaps->m_pmapHDC;
  delete pMaps->m_pmapHGDIOBJ;
  delete pMaps->m_pmapHIMAGELIST;
}

// Windows first: their destructors may touch the other maps.
static void DeleteTemps(_AFX_HANDLE_MAPS* pMaps) {
  if (pMaps->m_pmapHWND != NULL) pMaps->m_pmapHWND->DeleteTemp();
  if (pMaps->m_pmapHMENU != NULL) pMaps->m_pmapHMENU->DeleteTemp();
  if (pMaps->m_pmapHDC != NULL) pMaps->m_pmapHDC->DeleteTemp();
  if (pMaps->m_pmapHGDIOBJ != NULL) pMaps->m_pmapHGDIOBJ->DeleteTemp();
  if (pMaps->m_pmapHIMAGELIST != NULL) pMaps->m_pmapHIMAGELIST->DeleteTemp();
}

void AFXAPI _AfxTermModuleMaps(AFX_MODULE_STATE* pModuleState) {
  if (g_nGlobalsInit != 2) return;
  AFX_THREAD_STATE* pState = (AFX_THREAD_STATE*)::TlsGetValue(g_tlsIndex);
  if (pState == NULL) return;
  for (_AFX_DLL_MAPS** pp = &pState->m_pDllMaps; *pp != NULL; pp = &(*pp)->m_pNext) {
    _AFX_DLL_MAPS* p = *pp;
    if (p->m_pModuleState != pModuleState) continue;
    *pp = p->m_pNext;
    DeleteTemps(p);
    DeleteMaps(p);
    ::free(p);
    return;
  }
}

CHandleMap* AFXAPI afxMapHWND(BOOL bCreate) {
  _AFX_HANDLE_MAPS* pMaps = _AfxGetHandleMaps();
  if (pMaps->m_pmapHWND == NULL && bCreate)
    pMaps->m_pmapHWND = new CHandleMap(RUNTIME_CLASS(CWnd), offsetof(CWnd, m_hWnd));
  return pMaps->m_pmapHWND;
}

CHandleMap* AFXAPI afxMapHMENU(BOOL bCreate) {
  _AFX_HANDLE_MAPS* pMaps = _AfxGetHandleMaps();
  if (pMaps->m_pmapHMENU == NULL && bCreate)
    pMaps->m_pmapHMENU = new CHandleMap(RUNTIME_CLASS(CMenu), offsetof(CMenu, m_hMenu));
  return pMaps->m_pmapHMENU;
}

CHandleMap* AFXAPI afxMapHDC(BOOL bCreate) {
  _AFX_HANDLE_MAPS* pMaps = _AfxGetHandleMaps();
  if (pMaps->m_pmapHDC == NULL && bCreate)
    pMaps->m_pmapHDC = new CHandleMap(RUNTIME_CLASS(CDC), offsetof(CDC, m_hDC), 2);
  return pMaps->m_pmapHDC;
}

CHandleMap* AFXAPI afxMapHGDIOBJ(BOOL bCreate) {
  _AFX_HANDLE_MAPS* pMaps = _AfxGetHandleMaps();
  if (pMaps->m_pmapHGDIOBJ == NULL && bCreate)
    pMaps->m_pmapHGDIOBJ =
        new CHandleMap(RUNTIME_CLASS(CGdiObject), offsetof(CGdiObject, m_hObject));
  return pMaps->m_pmapHGDIOBJ;
}

void AFXAPI AfxLockTempMaps() { ++AfxGetThreadState()->m_nTempMapLock; }

BOOL AFXAPI AfxUnlockTempMaps(BOOL bDeleteTemps) {
  AFX_THREAD_STATE* pState = AfxGetThreadState();
  if (pState->m_nTempMapLock != 0 && --pState->m_nTempMapLock == 0 && bDeleteTemps) {
    DeleteTemps(&pState->m_maps);
    for (_AFX_DLL_MAPS* p = pState->m_pDllMaps; p != NULL; p = p->m_pNext) DeleteTemps(p);
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
