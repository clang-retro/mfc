// olereg.cpp - registering OLE servers: AfxOleRegisterServerClass and
// AfxOleRegisterHelper.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include <afxole.h>

#include "afximpl.h"

// The keys each kind of server gets when the program gives no list.
// Every server: its class, ProgID and local server.
static LPCTSTR const s_rglpszBase[] = {
    _T("CLSID\\%1\0%5"),
    NULL,
};
static LPCTSTR const s_rglpszProgID[] = {
    _T("CLSID\\%1\\ProgID\0%2"),
    _T("%2\0%5"),
    _T("%2\\CLSID\0%1"),
    NULL,
};
static LPCTSTR const s_rglpszLocalServer[] = {
    _T("CLSID\\%1\\LocalServer32\0%3"),
    NULL,
};
static LPCTSTR const s_rglpszAutomation[] = {
    _T("CLSID\\%1\\LocalServer32\0%3 /Automation"),
    NULL,
};
// A document server's object: how containers show and run it.
static LPCTSTR const s_rglpszDocument[] = {
    _T("CLSID\\%1\\InprocHandler32\0ole32.dll"),
    _T("CLSID\\%1\\DefaultIcon\0%3,%7"),
    _T("CLSID\\%1\\AuxUserType\\2\0%4"),
    _T("CLSID\\%1\\AuxUserType\\3\0%6"),
    NULL,
};
static LPCTSTR const s_rglpszServer[] = {
    _T("CLSID\\%1\\Insertable\0"),
    _T("CLSID\\%1\\MiscStatus\0") _T("32"),  // OLEMISC_RECOMPOSEONRESIZE
    _T("CLSID\\%1\\verb\\0\0&Edit,0,2"),
    NULL,
};
static LPCTSTR const s_rglpszInsertable[] = {
    _T("%2\\Insertable\0"),
    NULL,
};
static LPCTSTR const s_rglpszInPlace[] = {
    _T("CLSID\\%1\\verb\\1\0&Open,0,2"),
    NULL,
};
static LPCTSTR const s_rglpszDocObject[] = {
    _T("CLSID\\%1\\DocObject\0") _T("0"),
    NULL,
};
static LPCTSTR const s_rglpszFilter[] = {
    _T("CLSID\\%1\\DefaultExtension\0%9, %8"),
    NULL,
};

// Replaces %1 to %9 (and %%) in lpsz.
static CString Substitute(LPCTSTR lpsz, LPCTSTR const* rglpszSymbols, int nSymbols) {
  CString str;
  for (; *lpsz != 0; lpsz++) {
    if (*lpsz == _T('%') && lpsz[1] >= _T('1') && lpsz[1] <= _T('9')) {
      int n = lpsz[1] - _T('1');
      if (n < nSymbols && rglpszSymbols[n] != NULL) str += rglpszSymbols[n];
      lpsz++;
    } else if (*lpsz == _T('%') && lpsz[1] == _T('%')) {
      str += _T('%');
      lpsz++;
    } else {
      str += *lpsz;
    }
  }
  return str;
}

BOOL AFXAPI AfxOleRegisterHelper(LPCTSTR const* rglpszRegister, LPCTSTR const* rglpszSymbols,
                                 int nSymbols, BOOL bReplace, HKEY hKeyRoot) {
  if (rglpszRegister == NULL) return TRUE;
  BOOL bResult = TRUE;
  for (; *rglpszRegister != NULL; rglpszRegister++) {
    LPCTSTR lpszKey = *rglpszRegister;
    LPCTSTR lpszValue = lpszKey + lstrlen(lpszKey) + 1;  // after the key's terminator
    CString strKey = Substitute(lpszKey, rglpszSymbols, nSymbols);
    CString strValue = Substitute(lpszValue, rglpszSymbols, nSymbols);
    HKEY hKey = NULL;
    if (::RegCreateKeyEx(hKeyRoot, strKey, 0, NULL, 0, KEY_READ | KEY_WRITE, NULL, &hKey, NULL) !=
        ERROR_SUCCESS) {
      bResult = FALSE;
      continue;
    }
    BOOL bWrite = bReplace;
    if (!bWrite) {
      DWORD dwType = 0, cb = 0;
      bWrite = ::RegQueryValueEx(hKey, NULL, NULL, &dwType, NULL, &cb) != ERROR_SUCCESS;
    }
    if (bWrite && ::RegSetValueEx(hKey, NULL, 0, REG_SZ, (const BYTE*)(LPCTSTR)strValue,
                                  (DWORD)((strValue.GetLength() + 1) * sizeof(TCHAR))) !=
                      ERROR_SUCCESS)
      bResult = FALSE;
    ::RegCloseKey(hKey);
  }
  return bResult;
}

static CString ClassIDString(REFCLSID clsid) {
  LPOLESTR psz = NULL;
  if (FAILED(::StringFromCLSID(clsid, &psz))) return CString();
  CString str(psz);
  ::CoTaskMemFree(psz);
  return str;
}

BOOL AFXAPI AfxOleRegisterServerClass(REFCLSID clsid, LPCTSTR lpszClassName,
                                      LPCTSTR lpszShortTypeName, LPCTSTR lpszLongTypeName,
                                      OLE_APPTYPE nAppType, LPCTSTR* rglpszRegister,
                                      LPCTSTR* rglpszOverwrite, int nIconIndex,
                                      LPCTSTR lpszFilterName, LPCTSTR lpszFilterExt) {
  CString strClsid = ClassIDString(clsid);
  if (strClsid.IsEmpty()) return FALSE;
  if (lpszClassName == NULL) lpszClassName = _T("");
  TCHAR szPath[MAX_PATH];
  ::GetModuleFileName(AfxGetInstanceHandle(), szPath, MAX_PATH);
  CString strPath = szPath;
  if (strPath.Find(_T(' ')) >= 0) strPath = _T("\"") + strPath + _T("\"");
  CString strLong =
      lpszLongTypeName != NULL && *lpszLongTypeName ? lpszLongTypeName : lpszClassName;
  CString strShort = lpszShortTypeName != NULL && *lpszShortTypeName ? lpszShortTypeName
                                                                       : (LPCTSTR)strLong;
  CString strIcon;
  strIcon.Format(_T("%d"), nIconIndex);
  CString strFilterExt = lpszFilterExt != NULL ? lpszFilterExt : _T("");
  LPCTSTR rglpszSymbols[9] = {strClsid,
                              lpszClassName,
                              strPath,
                              strShort,
                              strLong,
                              AfxGetAppName(),
                              strIcon,
                              lpszFilterName != NULL ? lpszFilterName : _T(""),
                              strFilterExt};

  BOOL bResult;
  if (rglpszRegister != NULL) {
    bResult = AfxOleRegisterHelper(rglpszRegister, rglpszSymbols, 9, FALSE);
  } else {
    BOOL bServer = nAppType == OAT_INPLACE_SERVER || nAppType == OAT_SERVER ||
                   nAppType == OAT_DOC_OBJECT_SERVER;
    // Without a class name there is no ProgID to register.
    bResult = AfxOleRegisterHelper(s_rglpszBase, rglpszSymbols, 9, TRUE);
    if (*lpszClassName != 0)
      bResult = AfxOleRegisterHelper(s_rglpszProgID, rglpszSymbols, 9, TRUE) && bResult;
    bResult = AfxOleRegisterHelper(nAppType == OAT_DISPATCH_OBJECT ? s_rglpszAutomation
                                                                   : s_rglpszLocalServer,
                                   rglpszSymbols, 9, TRUE) &&
              bResult;
    if (nAppType != OAT_DISPATCH_OBJECT)
      bResult = AfxOleRegisterHelper(s_rglpszDocument, rglpszSymbols, 9, TRUE) && bResult;
    if (bServer) bResult = AfxOleRegisterHelper(s_rglpszServer, rglpszSymbols, 9, TRUE) && bResult;
    if (bServer && *lpszClassName != 0)
      bResult = AfxOleRegisterHelper(s_rglpszInsertable, rglpszSymbols, 9, TRUE) && bResult;
    if (bServer && nAppType != OAT_SERVER)
      bResult = AfxOleRegisterHelper(s_rglpszInPlace, rglpszSymbols, 9, TRUE) && bResult;
    if (nAppType == OAT_DOC_OBJECT_SERVER)
      bResult = AfxOleRegisterHelper(s_rglpszDocObject, rglpszSymbols, 9, TRUE) && bResult;
    if (lpszFilterExt != NULL && *lpszFilterExt != 0)
      bResult = AfxOleRegisterHelper(s_rglpszFilter, rglpszSymbols, 9, TRUE) && bResult;
  }
  if (rglpszOverwrite != NULL)
    bResult = AfxOleRegisterHelper(rglpszOverwrite, rglpszSymbols, 9, TRUE) && bResult;
  return bResult;
}

static void DeleteTree(HKEY hParent, LPCTSTR lpszKey) {
  HKEY hKey;
  if (::RegOpenKeyEx(hParent, lpszKey, 0, KEY_READ | KEY_WRITE, &hKey) != ERROR_SUCCESS) return;
  TCHAR szName[256];
  while (::RegEnumKey(hKey, 0, szName, _countof(szName)) == ERROR_SUCCESS) DeleteTree(hKey, szName);
  ::RegCloseKey(hKey);
  ::RegDeleteKey(hParent, lpszKey);
}

BOOL AFXAPI AfxOleUnregisterServerClass(REFCLSID clsid, LPCTSTR lpszClassName, LPCTSTR, LPCTSTR,
                                        OLE_APPTYPE, LPCTSTR*, LPCTSTR*) {
  CString strClsid = ClassIDString(clsid);
  if (strClsid.IsEmpty()) return FALSE;
  DeleteTree(HKEY_CLASSES_ROOT, _T("CLSID\\") + strClsid);
  if (lpszClassName != NULL && *lpszClassName != 0) DeleteTree(HKEY_CLASSES_ROOT, lpszClassName);
  return TRUE;
}
