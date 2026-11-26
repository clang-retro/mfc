// win16oleui.cpp - the OLE common dialog functions (oledlg.h) for 16-bit
// Windows. 16-bit OLE 2 has no OLE dialog DLL: the OLE 2 SDK gave the
// dialogs as sample source (OLE2UI). This file gives the verb menu, the
// update of links, and the Insert Object, Paste Special, Links and
// Server Busy dialogs, from templates in memory. The other dialogs fail
// with OLEUI_ERR_DIALOGFAILURE, so that DoModal of the MFC dialog gives
// IDABORT. The file is empty in the other builds.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include "stdafx.h"

#ifdef _AFX_WIN16
#include "afximpl.h"
#include <oledlg.h>
#include <shellapi.h>

/// Removes the '&' marks from a menu text.
static CString StripAmpersands(LPCSTR lpsz) {
  CString str;
  for (; *lpsz != 0; lpsz++)
    if (*lpsz != '&') str += *lpsz;
  return str;
}

// The IDs of the controls in the dialogs.
enum {
  kNew = 0x500,
  kFromFile,
  kList,
  kFile,
  kIcon,
  kResult,
  kPaste,
  kPasteLink,
  kSource,
  kUpdateNow,
  kOpenSource,
  kBreakLink,
  kText,
  kSwitchTo,
  kRetry,
};

// The predefined control classes of a 16-bit dialog template.
enum { kButton = 0x80, kEdit = 0x81, kStatic = 0x82, kListBox = 0x83 };

/// Gives the notification code of a WM_COMMAND message. 16-bit Windows
/// puts it in the high word of lParam, not in wParam.
static UINT CommandCode(LPARAM lParam) { return HIWORD(lParam); }

// What all the dialogs keep: the hook and the caption of the caller.
struct OLEUI_COMMON {
  LPFNOLEUIHOOK lpfnHook;
  LPCSTR lpszCaption;
};

/// Gives the state of a dialog, which WM_INITDIALOG keeps in DWL_USER.
static void* GetState(HWND hDlg) { return (void*)::GetWindowLongPtr(hDlg, DWLP_USER); }

/// Sends a message to the hook of the caller. Gives TRUE when the hook
/// handled it.
static BOOL CallHook(OLEUI_COMMON* pCommon, HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam) {
  return pCommon != NULL && pCommon->lpfnHook != NULL &&
         pCommon->lpfnHook(hDlg, msg, wParam, lParam) != 0;
}

/// Keeps the state of a dialog in DWL_USER and sets the caption of the
/// caller, if there is one.
static void InitCommon(HWND hDlg, OLEUI_COMMON* pCommon) {
  ::SetWindowLongPtr(hDlg, DWLP_USER, (LONG_PTR)pCommon);
  if (pCommon->lpszCaption != NULL && *pCommon->lpszCaption != 0)
    ::SetWindowText(hDlg, pCommon->lpszCaption);
}

/// Runs a dialog from a 16-bit template. DialogBoxIndirectParam needs the
/// template in a global memory block. Gives the result of EndDialog, or
/// OLEUI_ERR_DIALOGFAILURE when there is no dialog.
static UINT RunDialog(const CAfxDlgTemplate& dt, HWND hWndOwner, DLGPROC lpfnProc, void* pState) {
  HGLOBAL hTemplate = ::GlobalAlloc(GMEM_MOVEABLE, (DWORD)dt.GetSize());
  if (hTemplate == NULL) return OLEUI_ERR_GLOBALMEMALLOC;
  memcpy(::GlobalLock(hTemplate), dt.GetTemplate(), (size_t)dt.GetSize());
  ::GlobalUnlock(hTemplate);
  int nResult = ::DialogBoxIndirectParam(AfxGetInstanceHandle(), hTemplate, hWndOwner, lpfnProc,
                                         (LPARAM)pState);
  ::GlobalFree(hTemplate);
  return nResult == -1 ? (UINT)OLEUI_ERR_DIALOGFAILURE : (UINT)nResult;
}

/// Makes the template of a dialog: a modal frame with the 16-bit dialog
/// font.
static CAfxDlgTemplate MakeTemplate(int cx, int cy, LPCWSTR lpszCaption) {
  return CAfxDlgTemplate(DS_MODALFRAME | WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_CENTER, cx, cy,
                         lpszCaption, L"MS Sans Serif", 8);
}

/// Gives the item data of the selected item of a list box, or -1 when no
/// item is selected.
static LONG SelectedData(HWND hDlg) {
  HWND hList = ::GetDlgItem(hDlg, kList);
  int nSel = (int)::SendMessage(hList, LB_GETCURSEL, 0, 0);
  return nSel < 0 ? -1L : (LONG)::SendMessage(hList, LB_GETITEMDATA, nSel, 0);
}

/// Adds a string with its item data to a list box.
static void AddListItem(HWND hDlg, LPCSTR lpszText, LONG lData) {
  HWND hList = ::GetDlgItem(hDlg, kList);
  int nItem = (int)::SendMessage(hList, LB_ADDSTRING, 0, (LPARAM)lpszText);
  if (nItem >= 0) ::SendMessage(hList, LB_SETITEMDATA, nItem, lData);
}

// ---------------------------------------------------------------------
// Insert Object

// The state of the Insert Object dialog.
struct OLEUI_INSERT : OLEUI_COMMON {
  LPOLEUIINSERTOBJECTA lpio;
  CArray<CLSID, const CLSID&> classes;
  CStringArray names;
  CString strFile;
};

/// Gives TRUE when the caller excludes a class from the list.
static BOOL IsExcluded(LPOLEUIINSERTOBJECTA lpio, REFCLSID clsid) {
  for (UINT i = 0; lpio->lpClsidExclude != NULL && i < lpio->cClsidExclude; i++)
    if (IsEqualCLSID(lpio->lpClsidExclude[i], clsid)) return TRUE;
  return FALSE;
}

/// Finds the classes that the registration database says can be
/// inserted: the keys under HKEY_CLASSES_ROOT\CLSID with an Insertable
/// key.
static void FindInsertable(OLEUI_INSERT* pState) {
  HKEY hRoot;
  if (::RegOpenKey(HKEY_CLASSES_ROOT, "CLSID", &hRoot) != ERROR_SUCCESS) return;
  char szName[64];
  for (DWORD i = 0; ::RegEnumKey(hRoot, i, szName, sizeof(szName)) == ERROR_SUCCESS; i++) {
    HKEY hKey, hInsertable;
    if (::RegOpenKey(hRoot, szName, &hKey) != ERROR_SUCCESS) continue;
    if (::RegOpenKey(hKey, "Insertable", &hInsertable) == ERROR_SUCCESS) {
      ::RegCloseKey(hInsertable);
      CLSID clsid;
      if (SUCCEEDED(::CLSIDFromString(szName, &clsid)) && !IsExcluded(pState->lpio, clsid)) {
        char szType[128];
        LONG cb = sizeof(szType);
        if (::RegQueryValue(hKey, NULL, szType, &cb) != ERROR_SUCCESS || szType[0] == 0)
          lstrcpy(szType, szName);
        pState->classes.Add(clsid);
        pState->names.Add(szType);
      }
    }
    ::RegCloseKey(hKey);
  }
  ::RegCloseKey(hRoot);
}

/// Enables the class list for Create New, or the file name for Create
/// from File.
static void ShowInsertMode(HWND hDlg, BOOL bFromFile) {
  ::EnableWindow(::GetDlgItem(hDlg, kList), !bFromFile);
  ::EnableWindow(::GetDlgItem(hDlg, kFile), bFromFile);
}

/// Reads the choice of the user into the structure of the caller.
static void ReadInsertChoice(HWND hDlg, OLEUI_INSERT* pState) {
  LPOLEUIINSERTOBJECTA lpio = pState->lpio;
  BOOL bFromFile = ::IsDlgButtonChecked(hDlg, kFromFile) == BST_CHECKED;
  lpio->dwFlags &= ~(IOF_SELECTCREATENEW | IOF_SELECTCREATEFROMFILE | IOF_CHECKDISPLAYASICON);
  lpio->dwFlags |= bFromFile ? IOF_SELECTCREATEFROMFILE : IOF_SELECTCREATENEW;
  if (::IsDlgButtonChecked(hDlg, kIcon) == BST_CHECKED) lpio->dwFlags |= IOF_CHECKDISPLAYASICON;
  LONG nClass = SelectedData(hDlg);
  if (nClass >= 0 && nClass < pState->classes.GetSize()) lpio->clsid = pState->classes[nClass];
  char szFile[260];  // MAX_PATH of the 32-bit SDK
  ::GetDlgItemText(hDlg, kFile, szFile, sizeof(szFile));
  pState->strFile = szFile;
}

/// The procedure of the Insert Object dialog.
static INT_PTR CALLBACK InsertProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam) {
  OLEUI_INSERT* pState = (OLEUI_INSERT*)GetState(hDlg);
  if (msg == WM_INITDIALOG) {
    pState = (OLEUI_INSERT*)lParam;
    InitCommon(hDlg, pState);
    DWORD dwFlags = pState->lpio->dwFlags;
    for (INT_PTR i = 0; i < pState->classes.GetSize(); i++)
      AddListItem(hDlg, pState->names[i], (LONG)i);
    ::SendDlgItemMessage(hDlg, kList, LB_SETCURSEL, 0, 0);
    BOOL bFromFile = (dwFlags & IOF_SELECTCREATEFROMFILE) != 0;
    ::CheckRadioButton(hDlg, kNew, kFromFile, bFromFile ? kFromFile : kNew);
    ::CheckDlgButton(hDlg, kIcon, (dwFlags & IOF_CHECKDISPLAYASICON) ? BST_CHECKED : BST_UNCHECKED);
    if (dwFlags & IOF_DISABLEDISPLAYASICON) ::EnableWindow(::GetDlgItem(hDlg, kIcon), FALSE);
    ::SetDlgItemText(hDlg, kFile, pState->strFile);
    ShowInsertMode(hDlg, bFromFile);
    CallHook(pState, hDlg, msg, wParam, lParam);
    return TRUE;
  }
  if (CallHook(pState, hDlg, msg, wParam, lParam)) return TRUE;
  if (msg != WM_COMMAND || pState == NULL) return FALSE;
  switch (wParam) {
    case kNew:
    case kFromFile:
      ShowInsertMode(hDlg, wParam == kFromFile);
      return TRUE;
    case kList:
      if (CommandCode(lParam) != LBN_DBLCLK) return TRUE;
      // A double click chooses the class.
      // fall through
    case IDOK:
      ReadInsertChoice(hDlg, pState);
      ::EndDialog(hDlg, OLEUI_OK);
      return TRUE;
    case IDCANCEL:
      ::EndDialog(hDlg, OLEUI_CANCEL);
      return TRUE;
  }
  return FALSE;
}

/// Makes the object that the user chose, when the flags of the caller ask
/// for it, and keeps the result in sc.
static void CreateInsertedObject(OLEUI_INSERT* pState) {
  LPOLEUIINSERTOBJECTA lpio = pState->lpio;
  lpio->sc = S_OK;
  if (lpio->ppvObj == NULL) return;
  DWORD dwFlags = lpio->dwFlags;
  if ((dwFlags & IOF_SELECTCREATENEW) && (dwFlags & IOF_CREATENEWOBJECT)) {
    lpio->sc = ::OleCreate(lpio->clsid, lpio->iid, lpio->oleRender, lpio->lpFormatEtc,
                           lpio->lpIOleClientSite, lpio->lpIStorage, lpio->ppvObj);
  } else if ((dwFlags & IOF_SELECTCREATEFROMFILE) &&
             (dwFlags & (IOF_CREATEFILEOBJECT | IOF_CREATELINKOBJECT))) {
    if (dwFlags & IOF_CHECKLINK)
      lpio->sc = ::OleCreateLinkToFile(pState->strFile, lpio->iid, lpio->oleRender,
                                       lpio->lpFormatEtc, lpio->lpIOleClientSite,
                                       lpio->lpIStorage, lpio->ppvObj);
    else
      lpio->sc = ::OleCreateFromFile(CLSID_NULL, pState->strFile, lpio->iid, lpio->oleRender,
                                     lpio->lpFormatEtc, lpio->lpIOleClientSite,
                                     lpio->lpIStorage, lpio->ppvObj);
  }
}

// ---------------------------------------------------------------------
// Paste Special

// The state of the Paste Special dialog.
struct OLEUI_PASTE : OLEUI_COMMON {
  LPOLEUIPASTESPECIALA lpps;
};

/// Gives the text of an entry with "Object" in place of %s.
static CString ExpandEntryText(LPCSTR lpszText) {
  CString str(lpszText != NULL ? lpszText : "");
  int nPos = str.Find("%s");
  if (nPos >= 0) str = str.Left(nPos) + "Object" + str.Mid(nPos + 2);
  return str;
}

/// Fills the format list with the entries that the data object has: for
/// Paste Link, the entries that link; for Paste, the others.
static void FillPasteList(HWND hDlg, OLEUI_PASTE* pState) {
  LPOLEUIPASTESPECIALA lpps = pState->lpps;
  ::SendDlgItemMessage(hDlg, kList, LB_RESETCONTENT, 0, 0);
  BOOL bLink = ::IsDlgButtonChecked(hDlg, kPasteLink) == BST_CHECKED;
  for (int i = 0; lpps->arrPasteEntries != NULL && i < lpps->cPasteEntries; i++) {
    OLEUIPASTEENTRYA& entry = lpps->arrPasteEntries[i];
    BOOL bLinks = (entry.dwFlags & (0xFF | OLEUIPASTE_LINKANYTYPE)) != 0;
    if (bLink ? !bLinks : (bLinks && !(entry.dwFlags & OLEUIPASTE_PASTE))) continue;
    FORMATETC fe = entry.fmtetc;
    if (lpps->lpSrcDataObj == NULL || lpps->lpSrcDataObj->QueryGetData(&fe) != S_OK) continue;
    AddListItem(hDlg, ExpandEntryText(entry.lpstrFormatName), (LONG)i);
  }
  ::SendDlgItemMessage(hDlg, kList, LB_SETCURSEL, 0, 0);
}

/// Shows the result text of the selected entry.
static void ShowPasteResult(HWND hDlg, OLEUI_PASTE* pState) {
  LPOLEUIPASTESPECIALA lpps = pState->lpps;
  LONG nEntry = SelectedData(hDlg);
  CString strText;
  if (nEntry >= 0 && nEntry < lpps->cPasteEntries)
    strText = ExpandEntryText(lpps->arrPasteEntries[nEntry].lpstrResultText);
  ::SetDlgItemText(hDlg, kResult, strText);
}

/// Reads the choice of the user into the structure of the caller.
static void ReadPasteChoice(HWND hDlg, OLEUI_PASTE* pState) {
  LPOLEUIPASTESPECIALA lpps = pState->lpps;
  lpps->nSelectedIndex = (int)SelectedData(hDlg);
  lpps->fLink = ::IsDlgButtonChecked(hDlg, kPasteLink) == BST_CHECKED;
  lpps->dwFlags &= ~(PSF_SELECTPASTE | PSF_SELECTPASTELINK | PSF_CHECKDISPLAYASICON);
  lpps->dwFlags |= lpps->fLink ? PSF_SELECTPASTELINK : PSF_SELECTPASTE;
  if (::IsDlgButtonChecked(hDlg, kIcon) == BST_CHECKED) lpps->dwFlags |= PSF_CHECKDISPLAYASICON;
}

/// The procedure of the Paste Special dialog.
static INT_PTR CALLBACK PasteProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam) {
  OLEUI_PASTE* pState = (OLEUI_PASTE*)GetState(hDlg);
  if (msg == WM_INITDIALOG) {
    pState = (OLEUI_PASTE*)lParam;
    InitCommon(hDlg, pState);
    DWORD dwFlags = pState->lpps->dwFlags;
    ::CheckRadioButton(hDlg, kPaste, kPasteLink,
                       (dwFlags & PSF_SELECTPASTELINK) ? kPasteLink : kPaste);
    ::CheckDlgButton(hDlg, kIcon, (dwFlags & PSF_CHECKDISPLAYASICON) ? BST_CHECKED : BST_UNCHECKED);
    if (dwFlags & PSF_DISABLEDISPLAYASICON) ::EnableWindow(::GetDlgItem(hDlg, kIcon), FALSE);
    FillPasteList(hDlg, pState);
    ShowPasteResult(hDlg, pState);
    CallHook(pState, hDlg, msg, wParam, lParam);
    return TRUE;
  }
  if (CallHook(pState, hDlg, msg, wParam, lParam)) return TRUE;
  if (msg != WM_COMMAND || pState == NULL) return FALSE;
  switch (wParam) {
    case kPaste:
    case kPasteLink:
      FillPasteList(hDlg, pState);
      ShowPasteResult(hDlg, pState);
      return TRUE;
    case kList:
      if (CommandCode(lParam) == LBN_SELCHANGE) ShowPasteResult(hDlg, pState);
      if (CommandCode(lParam) != LBN_DBLCLK) return TRUE;
      // A double click chooses the format.
      // fall through
    case IDOK:
      ReadPasteChoice(hDlg, pState);
      ::EndDialog(hDlg, OLEUI_OK);
      return TRUE;
    case IDCANCEL:
      ::EndDialog(hDlg, OLEUI_CANCEL);
      return TRUE;
  }
  return FALSE;
}

// ---------------------------------------------------------------------
// Links

// The state of the Links dialog.
struct OLEUI_LINKS : OLEUI_COMMON {
  LPOLEUILINKCONTAINERA lpContainer;
};

/// Fills the list with the links of the container: the source and the
/// type of each link, with the link as the item data. Disables the
/// buttons when there is no link.
static void FillLinks(HWND hDlg, OLEUI_LINKS* pState) {
  LPOLEUILINKCONTAINERA lpContainer = pState->lpContainer;
  ::SendDlgItemMessage(hDlg, kList, LB_RESETCONTENT, 0, 0);
  int nLinks = 0;
  for (DWORD dwLink = lpContainer->GetNextLink(0); dwLink != 0 && nLinks < 1000;
       dwLink = lpContainer->GetNextLink(dwLink), nLinks++) {
    LPSTR lpszName = NULL, lpszFullType = NULL, lpszShortType = NULL;
    ULONG nLen = 0;
    BOOL bAvailable = FALSE, bSelected = FALSE;
    CString strText;
    if (SUCCEEDED(lpContainer->GetLinkSource(dwLink, &lpszName, &nLen, &lpszFullType,
                                             &lpszShortType, &bAvailable, &bSelected))) {
      LPCSTR lpszType = lpszShortType != NULL ? lpszShortType : lpszFullType;
      strText = CString(lpszName != NULL ? lpszName : "") + "\t" +
                (lpszType != NULL ? lpszType : "");
    }
    ::CoTaskMemFree(lpszName);
    ::CoTaskMemFree(lpszFullType);
    ::CoTaskMemFree(lpszShortType);
    AddListItem(hDlg, strText, (LONG)dwLink);
  }
  ::SendDlgItemMessage(hDlg, kList, LB_SETCURSEL, 0, 0);
  static const int buttons[] = {kUpdateNow, kOpenSource, kBreakLink};
  for (int i = 0; i < 3; i++) ::EnableWindow(::GetDlgItem(hDlg, buttons[i]), nLinks > 0);
}

/// The procedure of the Links dialog.
static INT_PTR CALLBACK LinksProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam) {
  OLEUI_LINKS* pState = (OLEUI_LINKS*)GetState(hDlg);
  if (msg == WM_INITDIALOG) {
    pState = (OLEUI_LINKS*)lParam;
    InitCommon(hDlg, pState);
    FillLinks(hDlg, pState);
    CallHook(pState, hDlg, msg, wParam, lParam);
    return TRUE;
  }
  if (CallHook(pState, hDlg, msg, wParam, lParam)) return TRUE;
  if (msg != WM_COMMAND || pState == NULL) return FALSE;
  DWORD dwLink = (DWORD)SelectedData(hDlg);
  BOOL bLink = (LONG)dwLink != -1L && dwLink != 0;
  switch (wParam) {
    case kUpdateNow:
      if (bLink) pState->lpContainer->UpdateLink(dwLink, TRUE, FALSE);
      FillLinks(hDlg, pState);
      return TRUE;
    case kOpenSource:
      if (bLink) pState->lpContainer->OpenLinkSource(dwLink);
      return TRUE;
    case kBreakLink:
      if (bLink) pState->lpContainer->CancelLink(dwLink);
      FillLinks(hDlg, pState);
      return TRUE;
    case IDOK:
      ::EndDialog(hDlg, OLEUI_OK);
      return TRUE;
    case IDCANCEL:
      ::EndDialog(hDlg, OLEUI_CANCEL);
      return TRUE;
  }
  return FALSE;
}

// ---------------------------------------------------------------------
// Server Busy

// The state of the Server Busy dialog.
struct OLEUI_BUSY : OLEUI_COMMON {
  LPOLEUIBUSYA lpbz;
  HWND hWndTask;
};

/// Keeps the first visible top-level window of the busy task.
static BOOL CALLBACK FindTaskWindow(HWND hWnd, LPARAM lParam) {
  if (!::IsWindowVisible(hWnd) || ::GetParent(hWnd) != NULL) return TRUE;
  *(HWND FAR*)lParam = hWnd;
  return FALSE;
}

/// The procedure of the Server Busy dialog. Switch To activates a window
/// of the busy task.
static INT_PTR CALLBACK BusyProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam) {
  OLEUI_BUSY* pState = (OLEUI_BUSY*)GetState(hDlg);
  if (msg == WM_INITDIALOG) {
    pState = (OLEUI_BUSY*)lParam;
    InitCommon(hDlg, pState);
    LPOLEUIBUSYA lpbz = pState->lpbz;
    if (lpbz->lphWndDialog != NULL) *lpbz->lphWndDialog = hDlg;
    char szApp[80] = "the other program";
    if (pState->hWndTask != NULL) ::GetWindowText(pState->hWndTask, szApp, sizeof(szApp));
    CString strText;
    if (lpbz->dwFlags & BZ_NOTRESPONDINGDIALOG)
      strText.Format("This action cannot be completed because %s is not responding. "
                     "Choose \"Switch To\" and correct the problem.",
                     szApp);
    else
      strText.Format("This action cannot be completed because %s is busy. "
                     "Choose \"Switch To\" to activate it and correct the problem.",
                     szApp);
    ::SetDlgItemText(hDlg, kText, strText);
    DWORD dwFlags = lpbz->dwFlags;
    if (dwFlags & BZ_DISABLESWITCHTOBUTTON) ::EnableWindow(::GetDlgItem(hDlg, kSwitchTo), FALSE);
    if (dwFlags & BZ_DISABLERETRYBUTTON) ::EnableWindow(::GetDlgItem(hDlg, kRetry), FALSE);
    if (dwFlags & BZ_DISABLECANCELBUTTON) ::EnableWindow(::GetDlgItem(hDlg, IDCANCEL), FALSE);
    CallHook(pState, hDlg, msg, wParam, lParam);
    return TRUE;
  }
  if (CallHook(pState, hDlg, msg, wParam, lParam)) return TRUE;
  if (msg != WM_COMMAND || pState == NULL) return FALSE;
  switch (wParam) {
    case kSwitchTo:
      if (pState->hWndTask != NULL) {
        if (::IsIconic(pState->hWndTask)) ::ShowWindow(pState->hWndTask, SW_RESTORE);
        ::SetActiveWindow(pState->hWndTask);
      }
      ::EndDialog(hDlg, OLEUI_BZ_SWITCHTOSELECTED);
      return TRUE;
    case kRetry:
      ::EndDialog(hDlg, OLEUI_BZ_RETRYSELECTED);
      return TRUE;
    case IDCANCEL:
      if (!(pState->lpbz->dwFlags & BZ_DISABLECANCELBUTTON)) ::EndDialog(hDlg, OLEUI_CANCEL);
      return TRUE;
  }
  return FALSE;
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

/// Shows the Insert Object dialog: the classes that can be inserted, or
/// a file. Makes the object when the flags ask for it.
UINT WINAPI OleUIInsertObjectA(LPOLEUIINSERTOBJECTA lpio) {
  if (lpio == NULL) return OLEUI_ERR_STRUCTURENULL;
  if (lpio->cbStruct != sizeof(*lpio)) return OLEUI_ERR_CBSTRUCTINCORRECT;
  OLEUI_INSERT state;
  state.lpfnHook = lpio->lpfnHook;
  state.lpszCaption = lpio->lpszCaption;
  state.lpio = lpio;
  if (lpio->lpszFile != NULL) state.strFile = lpio->lpszFile;
  FindInsertable(&state);

  CAfxDlgTemplate dt = MakeTemplate(290, 136, L"Insert Object");
  dt.AddItem(kButton, BS_AUTORADIOBUTTON | WS_GROUP | WS_TABSTOP, 7, 16, 76, 10, kNew,
             L"Create &New");
  dt.AddItem(kButton, BS_AUTORADIOBUTTON, 7, 32, 76, 10, kFromFile, L"Create from &File");
  dt.AddItem(kStatic, SS_LEFT, 90, 7, 120, 8, (WORD)-1, L"Object &Type:");
  dt.AddItem(kListBox, LBS_NOTIFY | LBS_SORT | WS_VSCROLL | WS_BORDER | WS_TABSTOP | WS_GROUP, 90,
             18, 136, 70, kList, L"");
  dt.AddItem(kEdit, ES_AUTOHSCROLL | WS_BORDER | WS_TABSTOP, 90, 92, 136, 12, kFile, L"");
  dt.AddItem(kButton, BS_AUTOCHECKBOX | WS_TABSTOP, 90, 110, 100, 10, kIcon, L"&Display As Icon");
  dt.AddItem(kButton, BS_DEFPUSHBUTTON | WS_TABSTOP | WS_GROUP, 233, 7, 50, 14, IDOK, L"OK");
  dt.AddItem(kButton, BS_PUSHBUTTON | WS_TABSTOP, 233, 24, 50, 14, IDCANCEL, L"Cancel");
  UINT nResult = RunDialog(dt, lpio->hWndOwner, InsertProc, &state);
  if (nResult != OLEUI_OK) return nResult;
  if (lpio->lpszFile != NULL && lpio->cchFile != 0) {
    lstrcpyn(lpio->lpszFile, state.strFile, (int)lpio->cchFile);
  }
  CreateInsertedObject(&state);
  return nResult;
}

/// Shows the Paste Special dialog: the formats of the caller that the
/// data object has. Without a data object, it uses the data on the
/// clipboard, which the caller releases.
UINT WINAPI OleUIPasteSpecialA(LPOLEUIPASTESPECIALA lpps) {
  if (lpps == NULL) return OLEUI_ERR_STRUCTURENULL;
  if (lpps->cbStruct != sizeof(*lpps)) return OLEUI_ERR_CBSTRUCTINCORRECT;
  if (lpps->lpSrcDataObj == NULL) ::OleGetClipboard(&lpps->lpSrcDataObj);
  OLEUI_PASTE state;
  state.lpfnHook = lpps->lpfnHook;
  state.lpszCaption = lpps->lpszCaption;
  state.lpps = lpps;

  CAfxDlgTemplate dt = MakeTemplate(290, 130, L"Paste Special");
  dt.AddItem(kStatic, SS_LEFT, 7, 7, 220, 8, kSource, L"Source: the Clipboard");
  dt.AddItem(kButton, BS_AUTORADIOBUTTON | WS_GROUP | WS_TABSTOP, 7, 30, 60, 10, kPaste, L"&Paste");
  dt.AddItem(kButton, BS_AUTORADIOBUTTON, 7, 46, 60, 10, kPasteLink, L"Paste &Link");
  dt.AddItem(kStatic, SS_LEFT, 74, 20, 60, 8, (WORD)-1, L"&As:");
  dt.AddItem(kListBox, LBS_NOTIFY | WS_VSCROLL | WS_BORDER | WS_TABSTOP | WS_GROUP, 74, 30, 150,
             56, kList, L"");
  dt.AddItem(kButton, BS_AUTOCHECKBOX | WS_TABSTOP, 74, 90, 100, 10, kIcon, L"&Display As Icon");
  dt.AddItem(kStatic, SS_LEFT, 7, 106, 220, 18, kResult, L"");
  dt.AddItem(kButton, BS_DEFPUSHBUTTON | WS_TABSTOP | WS_GROUP, 233, 7, 50, 14, IDOK, L"OK");
  dt.AddItem(kButton, BS_PUSHBUTTON | WS_TABSTOP, 233, 24, 50, 14, IDCANCEL, L"Cancel");
  return RunDialog(dt, lpps->hWndOwner, PasteProc, &state);
}

/// Shows the Links dialog: the links of the container, which the user
/// can update, open or break.
UINT WINAPI OleUIEditLinksA(LPOLEUIEDITLINKSA lpel) {
  if (lpel == NULL) return OLEUI_ERR_STRUCTURENULL;
  if (lpel->cbStruct != sizeof(*lpel)) return OLEUI_ERR_CBSTRUCTINCORRECT;
  if (lpel->lpOleUILinkContainer == NULL) return OLEUI_ERR_STRUCTUREINVALID;
  OLEUI_LINKS state;
  state.lpfnHook = lpel->lpfnHook;
  state.lpszCaption = lpel->lpszCaption;
  state.lpContainer = lpel->lpOleUILinkContainer;

  CAfxDlgTemplate dt = MakeTemplate(290, 120, L"Links");
  dt.AddItem(kStatic, SS_LEFT, 7, 7, 220, 8, (WORD)-1, L"&Links:");
  dt.AddItem(kListBox, LBS_NOTIFY | LBS_USETABSTOPS | WS_VSCROLL | WS_BORDER | WS_TABSTOP, 7, 18,
             220, 94, kList, L"");
  dt.AddItem(kButton, BS_DEFPUSHBUTTON | WS_TABSTOP | WS_GROUP, 233, 7, 50, 14, IDOK, L"Close");
  dt.AddItem(kButton, BS_PUSHBUTTON | WS_TABSTOP, 233, 26, 50, 14, kUpdateNow, L"&Update Now");
  dt.AddItem(kButton, BS_PUSHBUTTON | WS_TABSTOP, 233, 44, 50, 14, kOpenSource, L"&Open Source");
  dt.AddItem(kButton, BS_PUSHBUTTON | WS_TABSTOP, 233, 62, 50, 14, kBreakLink, L"&Break Link");
  dt.AddItem(kButton, BS_PUSHBUTTON | WS_TABSTOP, 233, 80, 50, 14, IDCANCEL, L"Cancel");
  return RunDialog(dt, lpel->hWndOwner, LinksProc, &state);
}

/// Shows the Server Busy dialog for the task of a server: Switch To,
/// Retry or Cancel.
UINT WINAPI OleUIBusyA(LPOLEUIBUSYA lpbz) {
  if (lpbz == NULL) return OLEUI_ERR_STRUCTURENULL;
  if (lpbz->cbStruct != sizeof(*lpbz)) return OLEUI_ERR_CBSTRUCTINCORRECT;
  if (lpbz->hTask == NULL || !::IsTask(lpbz->hTask)) return OLEUI_BZERR_HTASKINVALID;
  OLEUI_BUSY state;
  state.lpfnHook = lpbz->lpfnHook;
  state.lpszCaption = lpbz->lpszCaption;
  state.lpbz = lpbz;
  state.hWndTask = NULL;
  ::EnumTaskWindows(lpbz->hTask, FindTaskWindow, (LPARAM)(HWND FAR*)&state.hWndTask);

  CAfxDlgTemplate dt = MakeTemplate(240, 74, L"Server Busy");
  dt.AddItem(kStatic, SS_LEFT, 7, 7, 226, 36, kText, L"");
  dt.AddItem(kButton, BS_DEFPUSHBUTTON | WS_TABSTOP | WS_GROUP, 24, 52, 60, 14, kSwitchTo,
             L"&Switch To...");
  dt.AddItem(kButton, BS_PUSHBUTTON | WS_TABSTOP, 90, 52, 60, 14, kRetry, L"&Retry");
  dt.AddItem(kButton, BS_PUSHBUTTON | WS_TABSTOP, 156, 52, 60, 14, IDCANCEL, L"Cancel");
  UINT nResult = RunDialog(dt, lpbz->hWndOwner, BusyProc, &state);
  if (lpbz->lphWndDialog != NULL) *lpbz->lphWndDialog = NULL;
  return nResult;
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
