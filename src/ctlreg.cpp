// ctlreg.cpp - COleControlModule and the registration helpers of
// controls and property pages.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include "afximpl.h"
#include <afxctl.h>

IMPLEMENT_DYNAMIC(COleControlModule, CWinApp)

BOOL COleControlModule::InitInstance() { return CWinApp::InitInstance(); }

int COleControlModule::ExitInstance() { return CWinApp::ExitInstance(); }

// ---------------------------------------------------------------------
// Registry helpers

static BOOL SetKey(LPCTSTR lpszKey, LPCTSTR lpszValueName, LPCTSTR lpszValue) {
  HKEY hKey;
  if (::RegCreateKeyEx(HKEY_CLASSES_ROOT, lpszKey, 0, NULL, 0, KEY_WRITE, NULL, &hKey, NULL) !=
      ERROR_SUCCESS)
    return FALSE;
  LONG l = ::RegSetValueEx(hKey, lpszValueName, 0, REG_SZ, (const BYTE*)lpszValue,
                           (DWORD)((lstrlen(lpszValue) + 1) * sizeof(TCHAR)));
  ::RegCloseKey(hKey);
  return l == ERROR_SUCCESS;
}

// RegDeleteTree is not on Windows XP.
static void DeleteTree(HKEY hParent, LPCTSTR lpszKey) {
  HKEY hKey;
  if (::RegOpenKeyEx(hParent, lpszKey, 0, KEY_READ | KEY_WRITE, &hKey) != ERROR_SUCCESS) return;
  TCHAR szName[256];
  while (::RegEnumKey(hKey, 0, szName, _countof(szName)) == ERROR_SUCCESS) DeleteTree(hKey, szName);
  ::RegCloseKey(hKey);
  ::RegDeleteKey(hParent, lpszKey);
}

static CString GuidString(REFGUID guid) {
  LPOLESTR psz = NULL;
  if (FAILED(::StringFromCLSID(guid, &psz))) return CString();
  CString str(psz);
  ::CoTaskMemFree(psz);
  return str;
}

static CString ModulePath(HINSTANCE hInstance) {
  TCHAR szPath[MAX_PATH];
  szPath[0] = 0;
  ::GetModuleFileName(hInstance, szPath, MAX_PATH);
  return szPath;
}

static CString LoadName(HINSTANCE hInstance, UINT id) {
  TCHAR sz[256];
  sz[0] = 0;
  if (id != 0) ::LoadString(hInstance, id, sz, _countof(sz));
  return sz;
}

// The class key, its server and its threading model.
static BOOL RegisterServer(HINSTANCE hInstance, const CString& strKey, const CString& strName,
                           int nRegFlags) {
  if (!SetKey(strKey, NULL, strName)) return FALSE;
  CString strPath = ModulePath(hInstance);
  if (hInstance != NULL && hInstance == ::GetModuleHandle(NULL)) {
    // The control lives in an EXE.
    if (strPath.Find(_T(' ')) >= 0) strPath = _T("\"") + strPath + _T("\"");
    return SetKey(strKey + _T("\\LocalServer32"), NULL, strPath);
  }
  if (!SetKey(strKey + _T("\\InprocServer32"), NULL, strPath)) return FALSE;
  LPCTSTR lpszModel = NULL;
  if (nRegFlags & afxRegFreeThreading)
    lpszModel = (nRegFlags & afxRegApartmentThreading) ? _T("Both") : _T("Free");
  else if (nRegFlags & afxRegApartmentThreading)
    lpszModel = _T("Apartment");
  if (lpszModel != NULL) SetKey(strKey + _T("\\InprocServer32"), _T("ThreadingModel"), lpszModel);
  return TRUE;
}

// ---------------------------------------------------------------------
// Controls

BOOL AFXAPI AfxOleRegisterControlClass(HINSTANCE hInstance, REFCLSID clsid, LPCTSTR pszProgID,
                                       UINT idTypeName, UINT idBitmap, int nRegFlags,
                                       DWORD dwMiscStatus, REFGUID tlid, WORD wVerMajor,
                                       WORD wVerMinor) {
  if (hInstance == NULL) hInstance = AfxGetResourceHandle();
  CString strClsid = GuidString(clsid);
  if (strClsid.IsEmpty()) return FALSE;
  CString strKey = _T("CLSID\\") + strClsid;
  CString strName = LoadName(hInstance, idTypeName);
  if (strName.IsEmpty() && pszProgID != NULL) strName = pszProgID;

  if (!RegisterServer(hInstance, strKey, strName, nRegFlags)) return FALSE;

  if (pszProgID != NULL) {
    SetKey(pszProgID, NULL, strName);
    SetKey(CString(pszProgID) + _T("\\CLSID"), NULL, strClsid);
    SetKey(strKey + _T("\\ProgID"), NULL, pszProgID);
  }
  SetKey(strKey + _T("\\Control"), NULL, _T(""));
  if (nRegFlags & afxRegInsertable) {
    SetKey(strKey + _T("\\Insertable"), NULL, _T(""));
    if (pszProgID != NULL) SetKey(CString(pszProgID) + _T("\\Insertable"), NULL, _T(""));
  }
  if (idBitmap != 0) {
    CString strBitmap;
    strBitmap.Format(_T("%s, %u"), (LPCTSTR)ModulePath(hInstance), idBitmap);
    SetKey(strKey + _T("\\ToolboxBitmap32"), NULL, strBitmap);
  }

  CString strMisc;
  strMisc.Format(_T("%lu"), (unsigned long)dwMiscStatus);
  SetKey(strKey + _T("\\MiscStatus"), NULL, _T("0"));
  SetKey(strKey + _T("\\MiscStatus\\1"), NULL, strMisc);


  if (tlid != GUID_NULL) {
    SetKey(strKey + _T("\\TypeLib"), NULL, GuidString(tlid));
    CString strVer;
    strVer.Format(_T("%u.%u"), wVerMajor, wVerMinor);
    SetKey(strKey + _T("\\Version"), NULL, strVer);
  }
  return TRUE;
}

BOOL AFXAPI AfxOleUnregisterClass(REFCLSID clsid, LPCTSTR pszProgID) {
  CString strClsid = GuidString(clsid);
  if (strClsid.IsEmpty()) return FALSE;
  DeleteTree(HKEY_CLASSES_ROOT, _T("CLSID\\") + strClsid);
  if (pszProgID != NULL) DeleteTree(HKEY_CLASSES_ROOT, pszProgID);
  return TRUE;
}

// ---------------------------------------------------------------------
// Property pages

BOOL AFXAPI AfxOleRegisterPropertyPageClass(HINSTANCE hInstance, REFCLSID clsid, UINT idTypeName,
                                            int nRegFlags) {
  if (hInstance == NULL) hInstance = AfxGetResourceHandle();
  CString strClsid = GuidString(clsid);
  if (strClsid.IsEmpty()) return FALSE;
  return RegisterServer(hInstance, _T("CLSID\\") + strClsid, LoadName(hInstance, idTypeName),
                        nRegFlags);
}
