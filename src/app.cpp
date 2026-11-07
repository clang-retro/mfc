// app.cpp - CWinApp, start-up and shutdown, the command line, message
// boxes and the wait cursor.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include "afximpl.h"

IMPLEMENT_DYNAMIC(CWinApp, CWinThread)

BEGIN_MESSAGE_MAP(CWinApp, CWinThread)
  ON_COMMAND(ID_APP_EXIT, OnAppExit)
  ON_UPDATE_COMMAND_UI(ID_FILE_MRU_FILE1, OnUpdateRecentFileMenu)
  ON_COMMAND_EX_RANGE(ID_FILE_MRU_FILE1, ID_FILE_MRU_FILE16, OnOpenRecentFile)
END_MESSAGE_MAP()

static LPTSTR _AfxStrDup(LPCTSTR psz) {
  if (psz == NULL) return NULL;
  size_t n = (size_t)lstrlen(psz) + 1;
  LPTSTR p = (LPTSTR)malloc(n * sizeof(TCHAR));
  if (p == NULL) AfxThrowMemoryException();
  memcpy(p, psz, n * sizeof(TCHAR));
  return p;
}

CWinApp::CWinApp(LPCTSTR lpszAppName) {
  m_pszAppName = _AfxStrDup(lpszAppName);
  m_hInstance = NULL;
  m_lpCmdLine = NULL;
  m_nCmdShow = SW_SHOWNORMAL;
  m_pszExeName = NULL;
  m_pszHelpFilePath = NULL;
  m_pszProfileName = NULL;
  m_pszRegistryKey = NULL;
  m_hLangResourceDLL = NULL;
  m_bHelpMode = FALSE;
  m_pDocManager = NULL;
  m_hDevMode = NULL;
  m_hDevNames = NULL;
  m_dwPromptContext = 0;
  m_nWaitCursorCount = 0;
  m_hcurWaitCursorRestore = NULL;
  m_pRecentFileList = NULL;
  m_atomApp = 0;
  m_atomSystemTopic = 0;
  m_nNumPreviewPages = 0;
  m_lpfnDaoTerm = NULL;

  // The application object is the main thread's CWinThread.
  m_bAutoDelete = FALSE;
  m_nThreadID = ::GetCurrentThreadId();
  AFX_MODULE_STATE* pModuleState = AfxGetModuleState();
  pModuleState->m_pCurrentWinApp = this;
  AfxGetThreadState()->m_pCurrentWinThread = this;
}

CWinApp::~CWinApp() {
  delete m_pDocManager;
  m_pDocManager = NULL;
  delete m_pRecentFileList;
  m_pRecentFileList = NULL;
  if (m_hDevMode != NULL) AfxGlobalFree(m_hDevMode);
  if (m_hDevNames != NULL) AfxGlobalFree(m_hDevNames);
  if (m_atomApp != 0) ::GlobalDeleteAtom(m_atomApp);
  if (m_atomSystemTopic != 0) ::GlobalDeleteAtom(m_atomSystemTopic);

  // These are allocated with malloc, as programs replace them that way.
  free((void*)m_pszAppName);
  free((void*)m_pszExeName);
  free((void*)m_pszHelpFilePath);
  free((void*)m_pszProfileName);
  free((void*)m_pszRegistryKey);

  AFX_MODULE_STATE* pModuleState = AfxGetModuleState();
  if (pModuleState->m_pCurrentWinApp == this) {
    pModuleState->m_pCurrentWinApp = NULL;
    pModuleState->m_lpszCurrentAppName = NULL;
  }
}

// ---------------------------------------------------------------------
// Start-up and shutdown

// Fills in the names derived from the executable's path.
static void _AfxSetCurrentHandles(CWinApp* pApp) {
  TCHAR szPath[MAX_PATH + 1];
  DWORD n = ::GetModuleFileName(pApp->m_hInstance, szPath, MAX_PATH);
  szPath[n] = 0;
  LPTSTR pszFileName = szPath;
  for (LPTSTR p = szPath; *p; p = ::CharNext(p))
    if (*p == '\\' || *p == '/' || *p == ':') pszFileName = p + 1;
  LPTSTR pszExt = NULL;
  for (LPTSTR p = pszFileName; *p; p = ::CharNext(p))
    if (*p == '.') pszExt = p;

  if (pApp->m_pszExeName == NULL) {
    CString strExe(pszFileName, pszExt != NULL ? (int)(pszExt - pszFileName) : lstrlen(pszFileName));
    pApp->m_pszExeName = _AfxStrDup(strExe);
  }
  if (pApp->m_pszAppName == NULL) {
    CString strTitle;
    if (strTitle.LoadString(AFX_IDS_APP_TITLE))
      pApp->m_pszAppName = _AfxStrDup(strTitle);
    else
      pApp->m_pszAppName = _AfxStrDup(pApp->m_pszExeName);
  }
  if (pApp->m_pszHelpFilePath == NULL) {
    CString strHelp(szPath, pszExt != NULL ? (int)(pszExt - szPath) : lstrlen(szPath));
    strHelp += _T(".HLP");
    pApp->m_pszHelpFilePath = _AfxStrDup(strHelp);
  }
  if (pApp->m_pszProfileName == NULL) {
    CString strIni(pApp->m_pszExeName);
    strIni += _T(".INI");
    pApp->m_pszProfileName = _AfxStrDup(strIni);
  }
  AfxGetModuleState()->m_lpszCurrentAppName = pApp->m_pszAppName;
}

BOOL AFXAPI AfxWinInit(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPTSTR lpCmdLine,
                       int nCmdShow) {
  (void)hPrevInstance;
  AFX_MODULE_STATE* pModuleState = AfxGetModuleState();
  pModuleState->m_hCurrentInstanceHandle = hInstance;
  if (pModuleState->m_hCurrentResourceHandle == NULL)
    pModuleState->m_hCurrentResourceHandle = hInstance;

  CWinApp* pApp = AfxGetApp();
  if (pApp != NULL) {
    pApp->m_hInstance = hInstance;
    pApp->m_lpCmdLine = lpCmdLine;
    pApp->m_nCmdShow = nCmdShow;
    _AfxSetCurrentHandles(pApp);
  }
  return TRUE;
}

void AFXAPI AfxWinTerm() {
  AfxLockTempMaps();
  AfxUnlockTempMaps(TRUE);
}

int AFXAPI AfxWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPTSTR lpCmdLine,
                      int nCmdShow) {
  int nReturnCode = -1;
  CWinThread* pThread = AfxGetThread();
  CWinApp* pApp = AfxGetApp();

  if (!AfxWinInit(hInstance, hPrevInstance, lpCmdLine, nCmdShow)) goto InitFailure;
  if (pThread == NULL) {
    ::MessageBoxA(NULL, "This program has no CWinApp object.", NULL, MB_ICONSTOP);
    goto InitFailure;
  }
  if (pApp != NULL && !pApp->InitApplication()) goto InitFailure;
  if (!pThread->InitInstance()) {
    if (pThread->m_pMainWnd != NULL) pThread->m_pMainWnd->DestroyWindow();
    nReturnCode = pThread->ExitInstance();
    goto InitFailure;
  }
  nReturnCode = pThread->Run();

InitFailure:
  AfxWinTerm();
  return nReturnCode;
}

BOOL CWinApp::InitApplication() {
  LoadStdProfileSettings(0);
  return TRUE;
}

BOOL CWinApp::InitInstance() { return TRUE; }

int CWinApp::Run() {
  // A program run for OLE clients may have no window; it ends when its
  // last object goes.
  if (m_pMainWnd == NULL && AfxOleGetUserCtrl()) {
    // Without a main window nothing would ever end the message loop.
    TRACE(_T("Warning: m_pMainWnd is NULL in CWinApp::Run - quitting.\n"));
    AfxPostQuitMessage(0);
  }
  return CWinThread::Run();
}

void (AFXAPI* _afxPfnOleTerm)(BOOL) = NULL;  // set by AfxOleInit

int CWinApp::ExitInstance() {
  SaveStdProfileSettings();
  if (_afxPfnOleTerm != NULL) _afxPfnOleTerm(FALSE);
  return (int)m_msgCur.wParam;
}

BOOL CWinApp::OnIdle(LONG lCount) {
  if (lCount <= 0) {
    CWinThread::OnIdle(lCount);
    if (m_pDocManager != NULL) {
      POSITION pos = m_pDocManager->GetFirstDocTemplatePosition();
      while (pos != NULL) m_pDocManager->GetNextDocTemplate(pos)->OnIdle();
    }
  } else if (lCount == 1) {
    CWinThread::OnIdle(lCount);
  }
  return lCount < 1;
}

LRESULT CWinApp::ProcessWndProcException(CException* e, const MSG* pMsg) {
  return CWinThread::ProcessWndProcException(e, pMsg);
}

void CWinApp::OnAppExit() {
  // Closing the main window lets each document ask about saving.
  if (m_pMainWnd != NULL) m_pMainWnd->SendMessage(WM_CLOSE);
}

// ---------------------------------------------------------------------
// Resources

HCURSOR CWinApp::LoadCursor(LPCTSTR lpszResourceName) const {
  return ::LoadCursor(AfxFindResourceHandle(lpszResourceName, RT_GROUP_CURSOR), lpszResourceName);
}

HCURSOR CWinApp::LoadCursor(UINT nIDResource) const { return LoadCursor(MAKEINTRESOURCE(nIDResource)); }

HICON CWinApp::LoadIcon(LPCTSTR lpszResourceName) const {
  return ::LoadIcon(AfxFindResourceHandle(lpszResourceName, RT_GROUP_ICON), lpszResourceName);
}

HICON CWinApp::LoadIcon(UINT nIDResource) const { return LoadIcon(MAKEINTRESOURCE(nIDResource)); }

// ---------------------------------------------------------------------
// Message boxes and the wait cursor

static int _AfxDoMessageBox(LPCTSTR lpszPrompt, UINT nType, LPCTSTR lpszCaption) {
  HWND hWndTop;
  HWND hWnd = _AfxGetSafeOwner(NULL, &hWndTop);
  if ((nType & MB_ICONMASK) == 0) {
    switch (nType & MB_TYPEMASK) {
      case MB_OK:
      case MB_OKCANCEL:
        nType |= MB_ICONEXCLAMATION;
        break;
      case MB_YESNO:
      case MB_YESNOCANCEL:
        nType |= MB_ICONQUESTION;
        break;
      case MB_ABORTRETRYIGNORE:
      case MB_RETRYCANCEL:
        break;
    }
  }
  int nResult = ::MessageBox(hWnd, lpszPrompt, lpszCaption, nType);
  if (hWndTop != NULL) ::EnableWindow(hWndTop, TRUE);
  return nResult;
}

int CWinApp::DoMessageBox(LPCTSTR lpszPrompt, UINT nType, UINT nIDPrompt) {
  (void)nIDPrompt;
  return _AfxDoMessageBox(lpszPrompt, nType, m_pszAppName);
}

int AFXAPI AfxMessageBox(LPCTSTR lpszText, UINT nType, UINT nIDHelp) {
  CWinApp* pApp = AfxGetApp();
  if (pApp != NULL) return pApp->DoMessageBox(lpszText, nType, nIDHelp);
  return _AfxDoMessageBox(lpszText, nType, NULL);
}

int AFXAPI AfxMessageBox(UINT nIDPrompt, UINT nType, UINT nIDHelp) {
  CString string;
  AfxLoadString(nIDPrompt, string);
  if (nIDHelp == (UINT)-1) nIDHelp = nIDPrompt;
  return AfxMessageBox(string, nType, nIDHelp);
}

void CWinApp::DoWaitCursor(int nCode) {
  m_nWaitCursorCount += nCode;
  if (m_nWaitCursorCount > 0) {
    HCURSOR hcurPrev = ::SetCursor(::LoadCursor(NULL, IDC_WAIT));
    if (nCode > 0 && m_nWaitCursorCount == 1) m_hcurWaitCursorRestore = hcurPrev;
  } else {
    m_nWaitCursorCount = 0;
    ::SetCursor(m_hcurWaitCursorRestore);
  }
}

// ---------------------------------------------------------------------
// Profile settings: the registry after SetRegistryKey, an INI file
// otherwise.

void CWinApp::SetRegistryKey(LPCTSTR lpszRegistryKey) {
  free((void*)m_pszRegistryKey);
  m_pszRegistryKey = _AfxStrDup(lpszRegistryKey);
  free((void*)m_pszProfileName);
  m_pszProfileName = _AfxStrDup(m_pszAppName);
}

void CWinApp::SetRegistryKey(UINT nIDRegistryKey) {
  CString strKey;
  strKey.LoadString(nIDRegistryKey);
  SetRegistryKey(strKey);
}

HKEY CWinApp::GetAppRegistryKey() {
  HKEY hAppKey = NULL;
  HKEY hSoftKey = NULL;
  HKEY hCompanyKey = NULL;
  if (::RegOpenKeyEx(HKEY_CURRENT_USER, _T("Software"), 0, KEY_WRITE | KEY_READ, &hSoftKey) ==
      ERROR_SUCCESS) {
    DWORD dw;
    if (::RegCreateKeyEx(hSoftKey, m_pszRegistryKey, 0, NULL, REG_OPTION_NON_VOLATILE,
                         KEY_WRITE | KEY_READ, NULL, &hCompanyKey, &dw) == ERROR_SUCCESS) {
      ::RegCreateKeyEx(hCompanyKey, m_pszProfileName, 0, NULL, REG_OPTION_NON_VOLATILE,
                       KEY_WRITE | KEY_READ, NULL, &hAppKey, &dw);
    }
  }
  if (hSoftKey != NULL) ::RegCloseKey(hSoftKey);
  if (hCompanyKey != NULL) ::RegCloseKey(hCompanyKey);
  return hAppKey;
}

HKEY CWinApp::GetSectionKey(LPCTSTR lpszSection) {
  HKEY hSectionKey = NULL;
  HKEY hAppKey = GetAppRegistryKey();
  if (hAppKey == NULL) return NULL;
  DWORD dw;
  ::RegCreateKeyEx(hAppKey, lpszSection, 0, NULL, REG_OPTION_NON_VOLATILE, KEY_WRITE | KEY_READ,
                   NULL, &hSectionKey, &dw);
  ::RegCloseKey(hAppKey);
  return hSectionKey;
}

UINT CWinApp::GetProfileInt(LPCTSTR lpszSection, LPCTSTR lpszEntry, int nDefault) {
  if (m_pszRegistryKey != NULL) {
    HKEY hSecKey = GetSectionKey(lpszSection);
    if (hSecKey == NULL) return (UINT)nDefault;
    DWORD dwValue;
    DWORD dwType;
    DWORD dwCount = sizeof(DWORD);
    LONG lResult =
        ::RegQueryValueEx(hSecKey, lpszEntry, NULL, &dwType, (LPBYTE)&dwValue, &dwCount);
    ::RegCloseKey(hSecKey);
    if (lResult == ERROR_SUCCESS && dwType == REG_DWORD) return (UINT)dwValue;
    return (UINT)nDefault;
  }
  return ::GetPrivateProfileInt(lpszSection, lpszEntry, nDefault, m_pszProfileName);
}

BOOL CWinApp::WriteProfileInt(LPCTSTR lpszSection, LPCTSTR lpszEntry, int nValue) {
  if (m_pszRegistryKey != NULL) {
    HKEY hSecKey = GetSectionKey(lpszSection);
    if (hSecKey == NULL) return FALSE;
    LONG lResult = ::RegSetValueEx(hSecKey, lpszEntry, 0, REG_DWORD, (LPBYTE)&nValue, sizeof(nValue));
    ::RegCloseKey(hSecKey);
    return lResult == ERROR_SUCCESS;
  }
  TCHAR szT[16];
  wsprintf(szT, _T("%d"), nValue);
  return ::WritePrivateProfileString(lpszSection, lpszEntry, szT, m_pszProfileName);
}

CString CWinApp::GetProfileString(LPCTSTR lpszSection, LPCTSTR lpszEntry, LPCTSTR lpszDefault) {
  if (lpszDefault == NULL) lpszDefault = _T("");
  if (m_pszRegistryKey != NULL) {
    HKEY hSecKey = GetSectionKey(lpszSection);
    if (hSecKey == NULL) return lpszDefault;
    CString strValue;
    DWORD dwType = REG_NONE;
    DWORD dwCount = 0;
    LONG lResult = ::RegQueryValueEx(hSecKey, lpszEntry, NULL, &dwType, NULL, &dwCount);
    if (lResult == ERROR_SUCCESS && dwType == REG_SZ) {
      lResult = ::RegQueryValueEx(hSecKey, lpszEntry, NULL, &dwType,
                                  (LPBYTE)strValue.GetBuffer((int)(dwCount / sizeof(TCHAR))),
                                  &dwCount);
      strValue.ReleaseBuffer();
    }
    ::RegCloseKey(hSecKey);
    if (lResult == ERROR_SUCCESS && dwType == REG_SZ) return strValue;
    return lpszDefault;
  }
  // GetPrivateProfileString truncates silently; grow until it fits.
  CString strValue;
  for (DWORD nSize = 256;; nSize *= 2) {
    DWORD nLen = ::GetPrivateProfileString(lpszSection, lpszEntry, lpszDefault,
                                           strValue.GetBuffer((int)nSize), nSize, m_pszProfileName);
    strValue.ReleaseBuffer((int)nLen);
    if (nLen < nSize - 2) break;
  }
  return strValue;
}

BOOL CWinApp::WriteProfileString(LPCTSTR lpszSection, LPCTSTR lpszEntry, LPCTSTR lpszValue) {
  if (m_pszRegistryKey != NULL) {
    LONG lResult;
    if (lpszEntry == NULL) {
      // Delete the whole section.
      HKEY hAppKey = GetAppRegistryKey();
      if (hAppKey == NULL) return FALSE;
      lResult = ::RegDeleteKey(hAppKey, lpszSection);
      ::RegCloseKey(hAppKey);
    } else if (lpszValue == NULL) {
      HKEY hSecKey = GetSectionKey(lpszSection);
      if (hSecKey == NULL) return FALSE;
      lResult = ::RegDeleteValue(hSecKey, (LPTSTR)lpszEntry);
      ::RegCloseKey(hSecKey);
    } else {
      HKEY hSecKey = GetSectionKey(lpszSection);
      if (hSecKey == NULL) return FALSE;
      lResult = ::RegSetValueEx(hSecKey, lpszEntry, 0, REG_SZ, (LPBYTE)lpszValue,
                                (DWORD)((lstrlen(lpszValue) + 1) * sizeof(TCHAR)));
      ::RegCloseKey(hSecKey);
    }
    return lResult == ERROR_SUCCESS;
  }
  return ::WritePrivateProfileString(lpszSection, lpszEntry, lpszValue, m_pszProfileName);
}

BOOL CWinApp::GetProfileBinary(LPCTSTR lpszSection, LPCTSTR lpszEntry, LPBYTE* ppData,
                               UINT* pBytes) {
  *ppData = NULL;
  *pBytes = 0;
  if (m_pszRegistryKey != NULL) {
    HKEY hSecKey = GetSectionKey(lpszSection);
    if (hSecKey == NULL) return FALSE;
    DWORD dwType = REG_NONE;
    DWORD dwCount = 0;
    LONG lResult = ::RegQueryValueEx(hSecKey, lpszEntry, NULL, &dwType, NULL, &dwCount);
    if (lResult == ERROR_SUCCESS && dwType == REG_BINARY) {
      *ppData = new BYTE[dwCount];
      lResult = ::RegQueryValueEx(hSecKey, lpszEntry, NULL, &dwType, *ppData, &dwCount);
      *pBytes = dwCount;
    }
    ::RegCloseKey(hSecKey);
    if (lResult == ERROR_SUCCESS && dwType == REG_BINARY) return TRUE;
    delete[] * ppData;
    *ppData = NULL;
    *pBytes = 0;
    return FALSE;
  }
  // In an INI file each byte is two letters, low nibble first.
  CString str = GetProfileString(lpszSection, lpszEntry, NULL);
  if (str.IsEmpty()) return FALSE;
  int nLen = str.GetLength();
  *pBytes = (UINT)(nLen / 2);
  *ppData = new BYTE[*pBytes];
  for (int i = 0; i < nLen / 2; i++)
    (*ppData)[i] = (BYTE)(((str[2 * i + 1] - 'A') << 4) + (str[2 * i] - 'A'));
  return TRUE;
}

BOOL CWinApp::WriteProfileBinary(LPCTSTR lpszSection, LPCTSTR lpszEntry, LPBYTE pData,
                                 UINT nBytes) {
  if (m_pszRegistryKey != NULL) {
    HKEY hSecKey = GetSectionKey(lpszSection);
    if (hSecKey == NULL) return FALSE;
    LONG lResult = ::RegSetValueEx(hSecKey, lpszEntry, 0, REG_BINARY, pData, nBytes);
    ::RegCloseKey(hSecKey);
    return lResult == ERROR_SUCCESS;
  }
  CString str;
  LPTSTR lpsz = str.GetBuffer((int)nBytes * 2);
  for (UINT i = 0; i < nBytes; i++) {
    lpsz[i * 2] = (TCHAR)((pData[i] & 0x0F) + 'A');
    lpsz[i * 2 + 1] = (TCHAR)(((pData[i] >> 4) & 0x0F) + 'A');
  }
  str.ReleaseBuffer((int)nBytes * 2);
  return WriteProfileString(lpszSection, lpszEntry, str);
}

static const TCHAR _afxFileSection[] = _T("Recent File List");
static const TCHAR _afxFileEntry[] = _T("File%d");
static const TCHAR _afxPreviewSection[] = _T("Settings");
static const TCHAR _afxPreviewEntry[] = _T("PreviewPages");

void CWinApp::LoadStdProfileSettings(UINT nMaxMRU) {
  if (nMaxMRU != 0 && m_pRecentFileList == NULL) {
    m_pRecentFileList = new CRecentFileList(0, _afxFileSection, _afxFileEntry, (int)nMaxMRU);
    m_pRecentFileList->ReadList();
  }
  m_nNumPreviewPages = GetProfileInt(_afxPreviewSection, _afxPreviewEntry, 0);
}

void CWinApp::SaveStdProfileSettings() {
  if (m_pRecentFileList != NULL) m_pRecentFileList->WriteList();
  if (m_nNumPreviewPages != 0)
    WriteProfileInt(_afxPreviewSection, _afxPreviewEntry, (int)m_nNumPreviewPages);
}

// ---------------------------------------------------------------------
// Command line

// Splits the command line the way the Microsoft C runtime does: spaces
// separate arguments, quotes group them, and backslashes escape quotes.
static void _AfxSplitCommandLine(LPCTSTR lpsz, CStringArray& args) {
  CString arg;
  BOOL bInArg = FALSE;
  BOOL bInQuotes = FALSE;
  while (*lpsz != 0) {
    if (!bInQuotes && (*lpsz == ' ' || *lpsz == '\t')) {
      if (bInArg) {
        args.Add(arg);
        arg.Empty();
        bInArg = FALSE;
      }
      lpsz++;
      continue;
    }
    bInArg = TRUE;
    if (*lpsz == '\\') {
      int nSlashes = 0;
      while (*lpsz == '\\') {
        nSlashes++;
        lpsz++;
      }
      if (*lpsz == '"') {
        for (int i = 0; i < nSlashes / 2; i++) arg += '\\';
        if (nSlashes % 2) {
          arg += '"';
          lpsz++;
        }
      } else {
        for (int i = 0; i < nSlashes; i++) arg += '\\';
      }
      continue;
    }
    if (*lpsz == '"') {
      if (bInQuotes && lpsz[1] == '"') {
        arg += '"';
        lpsz += 2;
        continue;
      }
      bInQuotes = !bInQuotes;
      lpsz++;
      continue;
    }
    arg += *lpsz++;
  }
  if (bInArg) args.Add(arg);
}

void CWinApp::ParseCommandLine(CCommandLineInfo& rCmdInfo) {
  CStringArray args;
  _AfxSplitCommandLine(::GetCommandLine(), args);
  for (int i = 1; i < (int)args.GetSize(); i++) {
    LPCTSTR pszParam = args[i];
    BOOL bFlag = FALSE;
    BOOL bLast = (i == (int)args.GetSize() - 1);
    if (pszParam[0] == '-' || pszParam[0] == '/') {
      bFlag = TRUE;
      ++pszParam;
    }
    rCmdInfo.ParseParam(pszParam, bFlag, bLast);
  }
}

BOOL CWinApp::ProcessShellCommand(CCommandLineInfo& rCmdInfo) {
  BOOL bResult = TRUE;
  switch (rCmdInfo.m_nShellCommand) {
    case CCommandLineInfo::FileNew:
      if (!AfxGetApp()->OnCmdMsg(ID_FILE_NEW, 0, NULL, NULL)) OnFileNew();
      if (m_pMainWnd == NULL) bResult = FALSE;
      break;
    case CCommandLineInfo::FileOpen:
      if (!OpenDocumentFile(rCmdInfo.m_strFileName)) bResult = FALSE;
      break;
    case CCommandLineInfo::FilePrintTo:
    case CCommandLineInfo::FilePrint:
      m_nCmdShow = SW_HIDE;
      if (OpenDocumentFile(rCmdInfo.m_strFileName) != NULL && m_pMainWnd != NULL)
        m_pMainWnd->SendMessage(WM_COMMAND, ID_FILE_PRINT_DIRECT);
      bResult = FALSE;  // print and exit
      break;
    case CCommandLineInfo::FileDDE:
      // DDE is not supported; start the way a plain run would.
      m_nCmdShow = SW_SHOWNORMAL;
      if (!AfxGetApp()->OnCmdMsg(ID_FILE_NEW, 0, NULL, NULL)) OnFileNew();
      if (m_pMainWnd == NULL) bResult = FALSE;
      break;
    case CCommandLineInfo::AppRegister:
      RegisterShellFileTypes(FALSE);
      bResult = FALSE;
      break;
    case CCommandLineInfo::AppUnregister:
      UnregisterShellFileTypes();
      bResult = FALSE;
      break;
    case CCommandLineInfo::FileNothing:
      break;
  }
  return bResult;
}

CCommandLineInfo::CCommandLineInfo()
    : m_bShowSplash(TRUE),
      m_bRunEmbedded(FALSE),
      m_bRunAutomated(FALSE),
      m_bRegisterPerUser(FALSE),
      m_nShellCommand(FileNew) {}

CCommandLineInfo::~CCommandLineInfo() {}

void CCommandLineInfo::ParseParam(const TCHAR* pszParam, BOOL bFlag, BOOL bLast) {
  if (bFlag) {
    CStringA strParam(pszParam);
    ParseParamFlag(strParam);
  } else {
    ParseParamNotFlag(pszParam);
  }
  ParseLast(bLast);
}

void CCommandLineInfo::ParseParamFlag(const char* pszParam) {
  if (lstrcmpiA(pszParam, "pt") == 0)
    m_nShellCommand = FilePrintTo;
  else if (lstrcmpiA(pszParam, "p") == 0)
    m_nShellCommand = FilePrint;
  else if (lstrcmpiA(pszParam, "Register") == 0 || lstrcmpiA(pszParam, "Regserver") == 0)
    m_nShellCommand = AppRegister;
  else if (lstrcmpiA(pszParam, "Unregister") == 0 || lstrcmpiA(pszParam, "Unregserver") == 0)
    m_nShellCommand = AppUnregister;
  else if (lstrcmpiA(pszParam, "dde") == 0)
    m_nShellCommand = FileDDE;
  else if (lstrcmpiA(pszParam, "Embedding") == 0) {
    m_bRunEmbedded = TRUE;
    AfxOleSetUserCtrl(FALSE);
  } else if (lstrcmpiA(pszParam, "Automation") == 0) {
    m_bRunAutomated = TRUE;
    AfxOleSetUserCtrl(FALSE);
  }
}

// ---------------------------------------------------------------------
// The OLE object count

static LONG g_nOleObjects = 0;
static BOOL g_bOleUserCtrl = TRUE;

void AFXAPI AfxOleLockApp() { ::InterlockedIncrement(&g_nOleObjects); }

void AFXAPI AfxOleUnlockApp() {
  if (::InterlockedDecrement(&g_nOleObjects) != 0 || AfxOleGetUserCtrl()) return;
  // A DLL waits for DllCanUnloadNow; the program it is in goes on.
  if (AfxGetModuleState()->m_bDLL) return;
  CWnd* pMainWnd = AfxGetApp() != NULL ? AfxGetMainWnd() : NULL;
  if (pMainWnd != NULL && pMainWnd->m_hWnd != NULL)
    pMainWnd->PostMessage(WM_CLOSE);
  else
    AfxPostQuitMessage(0);
}

BOOL AFXAPI AfxOleCanExitApp() { return g_nOleObjects == 0; }
BOOL AFXAPI AfxOleGetUserCtrl() { return g_bOleUserCtrl; }
void AFXAPI AfxOleSetUserCtrl(BOOL bUserCtrl) { g_bOleUserCtrl = bUserCtrl; }

void CCommandLineInfo::ParseParamNotFlag(const TCHAR* pszParam) {
  if (m_strFileName.IsEmpty())
    m_strFileName = pszParam;
  else if (m_nShellCommand == FilePrintTo && m_strPrinterName.IsEmpty())
    m_strPrinterName = pszParam;
  else if (m_nShellCommand == FilePrintTo && m_strDriverName.IsEmpty())
    m_strDriverName = pszParam;
  else if (m_nShellCommand == FilePrintTo && m_strPortName.IsEmpty())
    m_strPortName = pszParam;
}

void CCommandLineInfo::ParseLast(BOOL bLast) {
  if (bLast) {
    if (m_nShellCommand == FileNew && !m_strFileName.IsEmpty()) m_nShellCommand = FileOpen;
    m_bShowSplash = !m_bRunEmbedded && !m_bRunAutomated;
  }
}

// ---------------------------------------------------------------------
// Documents (forwarded to the document manager)

void CWinApp::AddDocTemplate(CDocTemplate* pTemplate) {
  if (m_pDocManager == NULL) m_pDocManager = new CDocManager;
  m_pDocManager->AddDocTemplate(pTemplate);
}

POSITION CWinApp::GetFirstDocTemplatePosition() const {
  return m_pDocManager != NULL ? m_pDocManager->GetFirstDocTemplatePosition() : NULL;
}

CDocTemplate* CWinApp::GetNextDocTemplate(POSITION& pos) const {
  return m_pDocManager->GetNextDocTemplate(pos);
}

CDocument* CWinApp::OpenDocumentFile(LPCTSTR lpszFileName) {
  return m_pDocManager != NULL ? m_pDocManager->OpenDocumentFile(lpszFileName) : NULL;
}

CDocument* CWinApp::OpenDocumentFile(LPCTSTR lpszFileName, BOOL bAddToMRU) {
  return m_pDocManager != NULL ? m_pDocManager->OpenDocumentFile(lpszFileName, bAddToMRU) : NULL;
}

void CWinApp::AddToRecentFileList(LPCTSTR lpszPathName) {
  if (m_pRecentFileList != NULL) m_pRecentFileList->Add(lpszPathName);
}

BOOL CWinApp::SaveAllModified() {
  return m_pDocManager != NULL ? m_pDocManager->SaveAllModified() : TRUE;
}

void CWinApp::CloseAllDocuments(BOOL bEndSession) {
  if (m_pDocManager != NULL) m_pDocManager->CloseAllDocuments(bEndSession);
}

BOOL CWinApp::DoPromptFileName(CString& fileName, UINT nIDSTitle, DWORD lFlags,
                               BOOL bOpenFileDialog, CDocTemplate* pTemplate) {
  if (m_pDocManager == NULL) m_pDocManager = new CDocManager;
  return m_pDocManager->DoPromptFileName(fileName, nIDSTitle, lFlags, bOpenFileDialog, pTemplate);
}

void CWinApp::OnFileNew() {
  if (m_pDocManager != NULL) m_pDocManager->OnFileNew();
}

void CWinApp::OnFileOpen() {
  if (m_pDocManager == NULL) m_pDocManager = new CDocManager;
  m_pDocManager->OnFileOpen();
}

void CWinApp::OnUpdateRecentFileMenu(CCmdUI* pCmdUI) {
  if (m_pRecentFileList == NULL)
    pCmdUI->Enable(FALSE);
  else
    m_pRecentFileList->UpdateMenu(pCmdUI);
}

BOOL CWinApp::OnOpenRecentFile(UINT nID) {
  if (m_pRecentFileList == NULL) return FALSE;
  int nIndex = (int)(nID - ID_FILE_MRU_FILE1);
  if (nIndex < 0 || nIndex >= m_pRecentFileList->GetSize()) return FALSE;
  if (OpenDocumentFile((*m_pRecentFileList)[nIndex]) == NULL)
    m_pRecentFileList->Remove(nIndex);
  return TRUE;
}

BOOL CWinApp::OnDDECommand(LPTSTR lpszCommand) {
  (void)lpszCommand;
  return FALSE;
}

void CWinApp::HideApplication() {
  if (m_pMainWnd == NULL) return;
  m_pMainWnd->ShowWindow(SW_HIDE);
  m_pMainWnd->ShowOwnedPopups(FALSE);
  m_pMainWnd->SetWindowPos(&CWnd::wndBottom, 0, 0, 0, 0,
                           SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

void CWinApp::EnableShellOpen() {
  if (m_atomApp == 0) m_atomApp = ::GlobalAddAtom(m_pszExeName);
  if (m_atomSystemTopic == 0) m_atomSystemTopic = ::GlobalAddAtom(_T("system"));
}

void CWinApp::RegisterShellFileTypes(BOOL bCompat) {
  if (m_pDocManager != NULL) m_pDocManager->RegisterShellFileTypes(bCompat);
}

void CWinApp::UnregisterShellFileTypes() {
  if (m_pDocManager != NULL) m_pDocManager->UnregisterShellFileTypes();
}

// ---------------------------------------------------------------------
// Help

void CWinApp::WinHelp(DWORD_PTR dwData, UINT nCmd) {
  CWnd* pMainWnd = AfxGetMainWnd();
  if (pMainWnd != NULL) pMainWnd->WinHelp(dwData, nCmd);
}

void CWinApp::HtmlHelp(DWORD_PTR dwData, UINT nCmd) { WinHelp(dwData, nCmd == 0x000F ? HELP_CONTEXT : nCmd); }

void CWinApp::OnHelp() {
  CWnd* pMainWnd = AfxGetMainWnd();
  if (pMainWnd != NULL) pMainWnd->SendMessage(WM_COMMANDHELP, 0, 0);
}

void CWinApp::OnHelpIndex() { WinHelp(0L, HELP_INDEX); }
void CWinApp::OnHelpFinder() { WinHelp(0L, HELP_FINDER); }
void CWinApp::OnHelpUsing() { WinHelp(0L, HELP_HELPONHELP); }
void CWinApp::OnContextHelp() {}

// ---------------------------------------------------------------------
// Printer selection

void CWinApp::SelectPrinter(HANDLE hDevNames, HANDLE hDevMode, BOOL bFreeOld) {
  if (bFreeOld) {
    if (m_hDevMode != NULL && m_hDevMode != hDevMode) AfxGlobalFree(m_hDevMode);
    if (m_hDevNames != NULL && m_hDevNames != hDevNames) AfxGlobalFree(m_hDevNames);
  }
  m_hDevMode = hDevMode;
  m_hDevNames = hDevNames;
}

void CWinApp::UpdatePrinterSelection(BOOL bForceDefaults) {
  if (m_hDevNames != NULL && !bForceDefaults) return;
  PRINTDLG pd;
  memset(&pd, 0, sizeof(pd));
  pd.lStructSize = sizeof(pd);
  pd.Flags = PD_RETURNDEFAULT;
  if (::PrintDlg(&pd)) SelectPrinter(pd.hDevNames, pd.hDevMode, TRUE);
}

BOOL CWinApp::GetPrinterDeviceDefaults(PRINTDLG* pPrintDlg) {
  UpdatePrinterSelection(m_hDevNames == NULL);
  if (m_hDevNames == NULL) return FALSE;
  pPrintDlg->hDevNames = m_hDevNames;
  pPrintDlg->hDevMode = m_hDevMode;
  return TRUE;
}

BOOL CWinApp::CreatePrinterDC(CDC& dc) {
  UpdatePrinterSelection(FALSE);
  if (m_hDevNames == NULL) return FALSE;
  LPDEVNAMES lpDevNames = (LPDEVNAMES)::GlobalLock(m_hDevNames);
  LPDEVMODE lpDevMode = m_hDevMode != NULL ? (LPDEVMODE)::GlobalLock(m_hDevMode) : NULL;
  HDC hDC = ::CreateDC((LPCTSTR)lpDevNames + lpDevNames->wDriverOffset,
                       (LPCTSTR)lpDevNames + lpDevNames->wDeviceOffset,
                       (LPCTSTR)lpDevNames + lpDevNames->wOutputOffset, lpDevMode);
  ::GlobalUnlock(m_hDevNames);
  if (m_hDevMode != NULL) ::GlobalUnlock(m_hDevMode);
  return dc.Attach(hDC);
}

void CWinApp::OnFilePrintSetup() {
  PRINTDLG pd;
  memset(&pd, 0, sizeof(pd));
  pd.lStructSize = sizeof(pd);
  pd.hwndOwner = AfxGetMainWnd()->GetSafeHwnd();
  pd.Flags = PD_PRINTSETUP;
  UpdatePrinterSelection(FALSE);
  pd.hDevMode = m_hDevMode;
  pd.hDevNames = m_hDevNames;
  if (::PrintDlg(&pd)) SelectPrinter(pd.hDevNames, pd.hDevMode, FALSE);
}

// ---------------------------------------------------------------------
// CRecentFileList

CRecentFileList::CRecentFileList(UINT nStart, LPCTSTR lpszSection, LPCTSTR lpszEntryFormat,
                                 int nSize, int nMaxDispLen)
    : m_nSize(nSize),
      m_strSectionName(lpszSection),
      m_strEntryFormat(lpszEntryFormat),
      m_nStart(nStart),
      m_nMaxDisplayLength(nMaxDispLen) {
  m_arrNames = new CString[nSize];
}

CRecentFileList::~CRecentFileList() { delete[] m_arrNames; }

void CRecentFileList::Add(LPCTSTR lpszPathName) {
  TCHAR szTemp[MAX_PATH];
  if (!AfxFullPath(szTemp, lpszPathName)) lstrcpyn(szTemp, lpszPathName, MAX_PATH);

  // Move the name to the front, removing an older copy.
  int iMRU;
  for (iMRU = 0; iMRU < m_nSize - 1; iMRU++) {
    if (AfxComparePath(m_arrNames[iMRU], szTemp)) break;
  }
  for (; iMRU > 0; iMRU--) m_arrNames[iMRU] = m_arrNames[iMRU - 1];
  m_arrNames[0] = szTemp;
}

void CRecentFileList::Remove(int nIndex) {
  for (int iMRU = nIndex; iMRU < m_nSize - 1; iMRU++) m_arrNames[iMRU] = m_arrNames[iMRU + 1];
  m_arrNames[m_nSize - 1].Empty();
}

BOOL CRecentFileList::GetDisplayName(CString& strName, int nIndex, LPCTSTR lpszCurDir,
                                     int nCurDir, BOOL bAtLeastName) const {
  if (m_arrNames[nIndex].IsEmpty()) return FALSE;
  LPCTSTR lpszName = m_arrNames[nIndex];
  // Files in the current directory are shown without it.
  if (lpszCurDir != NULL && nCurDir > 0 && _tcsnicmp(lpszName, lpszCurDir, nCurDir) == 0 &&
      _tcschr(lpszName + nCurDir, '\\') == NULL) {
    strName = lpszName + nCurDir;
  } else if (m_nMaxDisplayLength != -1 && lstrlen(lpszName) > m_nMaxDisplayLength) {
    // Too long: show the drive, an ellipsis and the end of the path.
    TCHAR szTitle[MAX_PATH];
    AfxGetFileName(lpszName, szTitle, MAX_PATH);
    CString strPath(lpszName);
    int nTitle = lstrlen(szTitle);
    if (bAtLeastName && nTitle + 7 > m_nMaxDisplayLength) {
      strName = szTitle;
    } else {
      int nKeep = m_nMaxDisplayLength - nTitle - 4;
      strName = strPath.Left(nKeep > 3 ? nKeep : 3) + _T("...\\") + szTitle;
    }
  } else {
    strName = lpszName;
  }
  return TRUE;
}

void CRecentFileList::UpdateMenu(CCmdUI* pCmdUI) {
  if (m_strOriginal.IsEmpty() && pCmdUI->m_pMenu != NULL)
    pCmdUI->m_pMenu->GetMenuString(pCmdUI->m_nID, m_strOriginal, MF_BYCOMMAND);

  if (m_arrNames[0].IsEmpty()) {
    // No files: show the original item, disabled.
    if (!m_strOriginal.IsEmpty()) pCmdUI->SetText(m_strOriginal);
    pCmdUI->Enable(FALSE);
    return;
  }
  if (pCmdUI->m_pMenu == NULL) return;

  CMenu* pMenu = pCmdUI->m_pMenu;
  int iMRU;
  for (iMRU = 0; iMRU < m_nSize; iMRU++)
    pMenu->DeleteMenu(pCmdUI->m_nID + iMRU, MF_BYCOMMAND);

  TCHAR szCurDir[MAX_PATH];
  DWORD nCurDir = ::GetCurrentDirectory(MAX_PATH - 1, szCurDir);
  if (nCurDir > 0 && szCurDir[nCurDir - 1] != '\\') {
    szCurDir[nCurDir++] = '\\';
    szCurDir[nCurDir] = 0;
  }

  CString strName;
  CString strTemp;
  for (iMRU = 0; iMRU < m_nSize; iMRU++) {
    if (!GetDisplayName(strName, iMRU, szCurDir, (int)nCurDir)) break;
    // Double any '&' so it is not taken for a mnemonic.
    strName.Replace(_T("&"), _T("&&"));
    int nItem = (iMRU + 1 + (int)m_nStart) % 10;
    strTemp.Format(_T("&%d %s"), nItem, (LPCTSTR)strName);
    pMenu->InsertMenu(pCmdUI->m_nIndex++, MF_STRING | MF_BYPOSITION, pCmdUI->m_nID++, strTemp);
  }
  // Tell the menu updater where we stopped.
  pCmdUI->m_nIndex--;
  pCmdUI->m_nIndexMax = pMenu->GetMenuItemCount();
  pCmdUI->m_bEnableChanged = TRUE;
}

void CRecentFileList::ReadList() {
  CWinApp* pApp = AfxGetApp();
  CString strEntry;
  for (int iMRU = 0; iMRU < m_nSize; iMRU++) {
    strEntry.Format(m_strEntryFormat, iMRU + 1);
    m_arrNames[iMRU] = pApp->GetProfileString(m_strSectionName, strEntry, NULL);
  }
}

void CRecentFileList::WriteList() {
  CWinApp* pApp = AfxGetApp();
  CString strEntry;
  pApp->WriteProfileString(m_strSectionName, NULL, NULL);
  for (int iMRU = 0; iMRU < m_nSize; iMRU++) {
    strEntry.Format(m_strEntryFormat, iMRU + 1);
    if (!m_arrNames[iMRU].IsEmpty())
      pApp->WriteProfileString(m_strSectionName, strEntry, m_arrNames[iMRU]);
  }
}
