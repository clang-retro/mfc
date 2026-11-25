// win16oleui.cpp - the OLE common dialog functions (oledlg.h) for 16-bit
// Windows. 16-bit OLE 2 has no OLE dialog DLL: the OLE 2 SDK gave the
// dialogs as sample source (OLE2UI). This file gives the verb menu and
// the update of links. The dialogs fail with OLEUI_ERR_DIALOGFAILURE, so
// that DoModal of an MFC OLE dialog gives IDABORT. The file is empty in
// the other builds.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include "stdafx.h"

#ifdef _AFX_WIN16
#include "afximpl.h"
#include <oledlg.h>

/// Removes the '&' marks from a menu text.
static CString StripAmpersands(LPCSTR lpsz) {
  CString str;
  for (; *lpsz != 0; lpsz++)
    if (*lpsz != '&') str += *lpsz;
  return str;
}

extern "C" {

/// Puts the verbs of an object at the item pos of a menu: one item for one
/// verb, or a pop-up menu for more verbs (and for Convert). Without an
/// object, the item is a gray "&Object". Gives the pop-up menu in
/// *lphMenuOut when it makes one.
BOOL WINAPI OleUIAddVerbMenuA(LPOLEOBJECT lpOleObj, LPCSTR lpszShortType, HMENU hMenu, UINT uPos,
                              UINT uIDVerbMin, UINT uIDVerbMax, BOOL bAddConvert, UINT idConvert,
                              HMENU FAR* lphMenuOut) {
  if (lphMenuOut != NULL) *lphMenuOut = NULL;
  if (hMenu == NULL) return FALSE;
  UINT nOldID = ::GetMenuItemID(hMenu, (int)uPos);
  if (lpOleObj == NULL) {
    ::ModifyMenu(hMenu, uPos, MF_BYPOSITION | MF_STRING | MF_GRAYED, nOldID, "&Object");
    return FALSE;
  }
  CString strType(lpszShortType != NULL ? lpszShortType : "");
  if (strType.IsEmpty()) {
    LPSTR lpszType = NULL;
    if (SUCCEEDED(lpOleObj->GetUserType(USERCLASSTYPE_SHORT, &lpszType)) && lpszType != NULL)
      strType = lpszType;
    ::CoTaskMemFree(lpszType);
  }
  CDWordArray verbIDs;
  CStringArray verbNames;
  LPENUMOLEVERB lpEnum = NULL;
  if (SUCCEEDED(lpOleObj->EnumVerbs(&lpEnum)) && lpEnum != NULL) {
    OLEVERB verb;
    ULONG nFetched = 0;
    while (lpEnum->Next(1, &verb, &nFetched) == S_OK && nFetched == 1) {
      if (verb.lVerb >= 0 && (verb.grfAttribs & OLEVERBATTRIB_ONCONTAINERMENU) &&
          verb.lpszVerbName != NULL &&
          (uIDVerbMax == 0 || uIDVerbMin + (UINT)verb.lVerb <= uIDVerbMax)) {
        verbIDs.Add((DWORD)verb.lVerb);
        verbNames.Add(verb.lpszVerbName);
      }
      ::CoTaskMemFree(verb.lpszVerbName);
    }
    lpEnum->Release();
  }
  CString strLabel = strType.IsEmpty() ? CString("&Object") : strType + " &Object";
  if (verbIDs.GetSize() == 0) {
    ::ModifyMenu(hMenu, uPos, MF_BYPOSITION | MF_STRING, uIDVerbMin, strLabel);
    return TRUE;
  }
  if (verbIDs.GetSize() == 1 && !bAddConvert) {
    CString strText = StripAmpersands(verbNames[0]) + " " + strLabel;
    ::ModifyMenu(hMenu, uPos, MF_BYPOSITION | MF_STRING, uIDVerbMin + (UINT)verbIDs[0], strText);
    return TRUE;
  }
  HMENU hPopup = ::CreatePopupMenu();
  for (INT_PTR i = 0; i < verbIDs.GetSize(); i++)
    ::AppendMenu(hPopup, MF_STRING, uIDVerbMin + (UINT)verbIDs[i], verbNames[i]);
  if (bAddConvert) {
    ::AppendMenu(hPopup, MF_SEPARATOR, 0, NULL);
    ::AppendMenu(hPopup, MF_STRING, idConvert, "&Convert...");
  }
  ::ModifyMenu(hMenu, uPos, MF_BYPOSITION | MF_STRING | MF_POPUP, (UINT)hPopup, strLabel);
  if (lphMenuOut != NULL) *lphMenuOut = hPopup;
  return TRUE;
}

/// Updates each link of a container that is set to update always. There
/// is no progress dialog. Gives FALSE when an update fails.
BOOL WINAPI OleUIUpdateLinksA(LPOLEUILINKCONTAINERA lpOleUILinkCntr, HWND hwndParent,
                              LPSTR lpszTitle, int cLinks) {
  (void)hwndParent;
  (void)lpszTitle;
  (void)cLinks;
  if (lpOleUILinkCntr == NULL) return FALSE;
  BOOL bOK = TRUE;
  for (DWORD dwLink = lpOleUILinkCntr->GetNextLink(0); dwLink != 0;
       dwLink = lpOleUILinkCntr->GetNextLink(dwLink)) {
    DWORD dwOptions = 0;
    if (FAILED(lpOleUILinkCntr->GetLinkUpdateOptions(dwLink, &dwOptions))) continue;
    if (dwOptions == OLEUPDATE_ALWAYS && FAILED(lpOleUILinkCntr->UpdateLink(dwLink, FALSE, FALSE)))
      bOK = FALSE;
  }
  return bOK;
}

/// Fails: 16-bit Windows has no Insert Object dialog.
UINT WINAPI OleUIInsertObjectA(LPOLEUIINSERTOBJECTA lp) {
  (void)lp;
  return OLEUI_ERR_DIALOGFAILURE;
}

/// Fails: 16-bit Windows has no Paste Special dialog.
UINT WINAPI OleUIPasteSpecialA(LPOLEUIPASTESPECIALA lp) {
  (void)lp;
  return OLEUI_ERR_DIALOGFAILURE;
}

/// Fails: 16-bit Windows has no Links dialog.
UINT WINAPI OleUIEditLinksA(LPOLEUIEDITLINKSA lp) {
  (void)lp;
  return OLEUI_ERR_DIALOGFAILURE;
}

/// Fails: 16-bit Windows has no Change Icon dialog.
UINT WINAPI OleUIChangeIconA(LPOLEUICHANGEICONA lp) {
  (void)lp;
  return OLEUI_ERR_DIALOGFAILURE;
}

/// Fails: 16-bit Windows has no Convert dialog.
UINT WINAPI OleUIConvertA(LPOLEUICONVERTA lp) {
  (void)lp;
  return OLEUI_ERR_DIALOGFAILURE;
}

/// Fails: 16-bit Windows has no Server Busy dialog.
UINT WINAPI OleUIBusyA(LPOLEUIBUSYA lp) {
  (void)lp;
  return OLEUI_ERR_DIALOGFAILURE;
}

/// Fails: 16-bit Windows has no Change Source dialog.
UINT WINAPI OleUIChangeSourceA(LPOLEUICHANGESOURCEA lp) {
  (void)lp;
  return OLEUI_ERR_DIALOGFAILURE;
}

/// Fails: 16-bit Windows has no Object Properties dialog.
UINT WINAPI OleUIObjectPropertiesA(LPOLEUIOBJECTPROPSA lp) {
  (void)lp;
  return OLEUI_ERR_DIALOGFAILURE;
}

}  // extern "C"

#endif  // _AFX_WIN16
