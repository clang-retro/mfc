// win16ole.cpp - the OLE parts of MFC that 16-bit Windows does not
// have: the IIDs of the OLE control interfaces (ocidl.h), a replacement
// for UnRegisterTypeLib, and the OLE parts of CWnd, CView and
// CDocTemplate that the 16-bit library does not have yet. Each of those
// says that the object has nothing of OLE. The file is empty in the
// other builds.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include "stdafx.h"

#ifdef _AFX_WIN16
#include "afximpl.h"
#include <ocidl.h>
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

/// Gives FALSE: a window cannot hold OLE controls.
BOOL CWnd::CreateControlSite(COleControlContainer* pContainer, COleControlSite** ppSite, UINT nID,
                             REFCLSID clsid) {
  (void)pContainer;
  (void)nID;
  (void)clsid;
  if (ppSite != NULL) *ppSite = NULL;
  return FALSE;
}

/// Gives DROPEFFECT_NONE (0): a view takes no OLE drops.
DROPEFFECT CView::OnDragEnter(COleDataObject* pDataObject, DWORD dwKeyState, CPoint point) {
  (void)pDataObject;
  (void)dwKeyState;
  (void)point;
  return 0;
}

/// Gives DROPEFFECT_NONE (0): a view takes no OLE drops.
DROPEFFECT CView::OnDragOver(COleDataObject* pDataObject, DWORD dwKeyState, CPoint point) {
  (void)pDataObject;
  (void)dwKeyState;
  (void)point;
  return 0;
}

/// Does nothing: a view takes no OLE drops.
void CView::OnDragLeave() {}

/// Gives FALSE: a view takes no OLE drops.
BOOL CView::OnDrop(COleDataObject* pDataObject, DROPEFFECT dropEffect, CPoint point) {
  (void)pDataObject;
  (void)dropEffect;
  (void)point;
  return FALSE;
}

/// Gives (DROPEFFECT)-1, so that OnDrop is called (and fails).
DROPEFFECT CView::OnDropEx(COleDataObject* pDataObject, DROPEFFECT dropDefault,
                           DROPEFFECT dropList, CPoint point) {
  (void)pDataObject;
  (void)dropDefault;
  (void)dropList;
  (void)point;
  return (DROPEFFECT)-1;
}

/// Gives DROPEFFECT_NONE (0): a view does not scroll for an OLE drag.
DROPEFFECT CView::OnDragScroll(DWORD dwKeyState, CPoint point) {
  (void)dwKeyState;
  (void)point;
  return 0;
}

/// Gives NULL: a document cannot be an OLE server.
CFrameWnd* CDocTemplate::CreateOleFrame(CWnd* pParentWnd, CDocument* pDoc, BOOL bCreateView) {
  (void)pParentWnd;
  (void)pDoc;
  (void)bCreateView;
  return NULL;
}

#endif  // _AFX_WIN16
