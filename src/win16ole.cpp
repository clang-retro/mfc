// win16ole.cpp - the OLE parts of MFC that 16-bit Windows does not
// have: the IIDs of the OLE control interfaces (ocidl.h) and of the
// document object interfaces (docobj.h), replacements
// for UnRegisterTypeLib and the CoTaskMem functions, and
// CWnd::CreateControlSite, which says that a window cannot hold OLE
// controls. The file is empty in the other builds.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include "stdafx.h"

#ifdef _AFX_WIN16
#include "afximpl.h"
#include <ocidl.h>
#include <docobj.h>
#include <shellapi.h>

// The IIDs of the OLE control interfaces, with the values of Win32.
extern "C" {
const IID FAR IID_IProvideClassInfo = {
    0xB196B283L, 0xBAB4, 0x101A, {0xB6, 0x9C, 0x00, 0xAA, 0x00, 0x34, 0x1D, 0x07}};
const IID FAR IID_IConnectionPointContainer = {
    0xB196B284L, 0xBAB4, 0x101A, {0xB6, 0x9C, 0x00, 0xAA, 0x00, 0x34, 0x1D, 0x07}};
const IID FAR IID_IEnumConnectionPoints = {
    0xB196B285L, 0xBAB4, 0x101A, {0xB6, 0x9C, 0x00, 0xAA, 0x00, 0x34, 0x1D, 0x07}};
const IID FAR IID_IConnectionPoint = {
    0xB196B286L, 0xBAB4, 0x101A, {0xB6, 0x9C, 0x00, 0xAA, 0x00, 0x34, 0x1D, 0x07}};
const IID FAR IID_IEnumConnections = {
    0xB196B287L, 0xBAB4, 0x101A, {0xB6, 0x9C, 0x00, 0xAA, 0x00, 0x34, 0x1D, 0x07}};
const IID FAR IID_IClassFactory2 = {
    0xB196B28FL, 0xBAB4, 0x101A, {0xB6, 0x9C, 0x00, 0xAA, 0x00, 0x34, 0x1D, 0x07}};
const IID FAR IID_IProvideClassInfo2 = {
    0xA6BC3AC0L, 0xDBAA, 0x11CE, {0x9D, 0xE3, 0x00, 0xAA, 0x00, 0x4B, 0xB8, 0x51}};
const IID FAR IID_IServiceProvider = {
    0x6D5140C1L, 0x7436, 0x11CE, {0x80, 0x34, 0x00, 0xAA, 0x00, 0x60, 0x09, 0xFA}};

// The IIDs of the document object interfaces (docobj.h of the portable
// layer), with the values of Win32.
#define _AFX_DOCOBJ_IID(l) {l, 0x4E68, 0x101B, {0xA2, 0xBC, 0x00, 0xAA, 0x00, 0x40, 0x47, 0x70}}
const IID IID_IOleDocument = _AFX_DOCOBJ_IID(0xB722BCC5L);
const IID IID_IOleDocumentView = _AFX_DOCOBJ_IID(0xB722BCC6L);
const IID IID_IOleDocumentSite = _AFX_DOCOBJ_IID(0xB722BCC7L);
const IID IID_IEnumOleDocumentViews = _AFX_DOCOBJ_IID(0xB722BCC8L);
const IID IID_IPrint = _AFX_DOCOBJ_IID(0xB722BCC9L);
const IID IID_IContinueCallback = _AFX_DOCOBJ_IID(0xB722BCCAL);
const IID IID_IOleCommandTarget = _AFX_DOCOBJ_IID(0xB722BCCBL);
}

/// Removes the registration of a type library version. 16-bit TYPELIB has
/// no UnRegisterTypeLib, so this deletes the key
/// TypeLib\{guid}\major.minor, and the key of the GUID when no version
/// stays. The 16-bit registry deletes a key with its subkeys. The locale
/// and the system kind are not used: a version has one entry for each.
HRESULT UnRegisterTypeLib(REFGUID libID, WORD wVerMajor, WORD wVerMinor, LCID lcid,
                          SYSKIND syskind) {
  (void)lcid;
  (void)syskind;
  char szGuid[40];
  if (::StringFromGUID2(libID, szGuid, sizeof(szGuid)) == 0) return E_INVALIDARG;
  char szKey[64];
  wsprintf(szKey, "TypeLib\\%s", (LPSTR)szGuid);
  HKEY hKey = 0;
  if (::RegOpenKey(HKEY_CLASSES_ROOT, szKey, &hKey) != ERROR_SUCCESS)
    return TYPE_E_REGISTRYACCESS;
  char szVersion[16];
  wsprintf(szVersion, "%x.%x", wVerMajor, wVerMinor);
  LONG lResult = ::RegDeleteKey(hKey, szVersion);
  char szName[16];
  BOOL bEmpty = ::RegEnumKey(hKey, 0, szName, sizeof(szName)) != ERROR_SUCCESS;
  ::RegCloseKey(hKey);
  if (lResult != ERROR_SUCCESS) return TYPE_E_REGISTRYACCESS;
  if (bEmpty) ::RegDeleteKey(HKEY_CLASSES_ROOT, szKey);
  return S_OK;
}

/// Allocates cb bytes with the IMalloc of the task. Gives NULL when
/// there is no task allocator (OLE is not initialized) or no memory.
LPVOID CoTaskMemAlloc(ULONG cb) {
  LPMALLOC pMalloc = NULL;
  if (::CoGetMalloc(MEMCTX_TASK, &pMalloc) != S_OK || pMalloc == NULL) return NULL;
  LPVOID pv = pMalloc->Alloc(cb);
  pMalloc->Release();
  return pv;
}

/// Changes the size of a block of the task allocator, as IMalloc::Realloc.
LPVOID CoTaskMemRealloc(LPVOID pv, ULONG cb) {
  LPMALLOC pMalloc = NULL;
  if (::CoGetMalloc(MEMCTX_TASK, &pMalloc) != S_OK || pMalloc == NULL) return NULL;
  LPVOID pNew = pMalloc->Realloc(pv, cb);
  pMalloc->Release();
  return pNew;
}

/// Frees a block of the task allocator. A NULL pointer does nothing.
void CoTaskMemFree(LPVOID pv) {
  if (pv == NULL) return;
  LPMALLOC pMalloc = NULL;
  if (::CoGetMalloc(MEMCTX_TASK, &pMalloc) != S_OK || pMalloc == NULL) return;
  pMalloc->Free(pv);
  pMalloc->Release();
}

/// Counts the entries of an accelerator table. In 16-bit Windows the
/// table is the resource itself: 5-byte entries (flags, key, command),
/// and the flag 0x80 marks the last entry.
int _AfxW16AcceleratorCount(HACCEL hAccel) {
  if (hAccel == NULL) return 0;
  const BYTE FAR* p = (const BYTE FAR*)::LockResource((HGLOBAL)hAccel);
  if (p == NULL) return 0;
  int n = 0;
  for (;;) {
    n++;
    if (p[0] & 0x80) break;
    p += 5;
  }
  ::UnlockResource((HGLOBAL)hAccel);
  return n;
}

/// Gives FALSE: a window cannot hold OLE controls.
BOOL CWnd::CreateControlSite(COleControlContainer* pContainer, COleControlSite** ppSite, UINT nID,
                             REFCLSID clsid) {
  (void)pContainer;
  (void)nID;
  (void)clsid;
  if (ppSite != NULL) *ppSite = NULL;
  return FALSE;
}

#endif  // _AFX_WIN16
