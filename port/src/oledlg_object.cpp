// oledlg_object.cpp - the OLE dialogs about one object or link: Server
// Busy, Change Icon, Convert, Change Source, Object Properties (General,
// View and Link pages) and the progress of updating a document's links.
//
// Each dialog works in wide strings through callbacks; the ANSI and
// Unicode entry points fill the callbacks from their own structures and
// interfaces.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include "dlgtemplate.h"
#include "ole.h"
#include "user.h"

#include <commctrl.h>
#include <commdlg.h>
#include <oledlg.h>
#include <prsht.h>
#include <shellapi.h>
#include <string.h>

#include <algorithm>
#include <functional>
#include <string>
#include <vector>

using namespace winport;
using winport::ole::SafeRelease;

namespace {

enum {
  kText = 0x600,
  kSwitchTo,
  kRetry,
  kCurrent,
  kDefault,
  kFromFile,
  kFile,
  kBrowse,
  kLabel,
  kConvertTo,
  kActivateAs,
  kList,
  kAsIcon,
  kType,
  kSize,
  kLocation,
  kConvert,
  kAsContent,
  kScale,
  kRelative,
  kSource,
  kUpdated,
  kAutomatic,
  kManual,
  kUpdateNow,
  kOpenSource,
  kChangeSource,
  kBreakLink,
  kProgress,
};

std::wstring W(LPCSTR s) { return s ? Widen(s) : std::wstring(); }
std::wstring W(LPCWSTR s) { return s ? std::wstring(s) : std::wstring(); }

// A string the caller frees with CoTaskMemFree.
LPWSTR TaskW(const std::wstring& s) {
  LPWSTR p = (LPWSTR)CoTaskMemAlloc((s.size() + 1) * sizeof(WCHAR));
  if (p) memcpy(p, s.c_str(), (s.size() + 1) * sizeof(WCHAR));
  return p;
}
LPSTR TaskA(const std::wstring& s) {
  std::string n = Narrow(s.c_str());
  LPSTR p = (LPSTR)CoTaskMemAlloc(n.size() + 1);
  if (p) memcpy(p, n.c_str(), n.size() + 1);
  return p;
}
LPWSTR Task(const std::wstring& s, LPWSTR) { return TaskW(s); }
LPSTR Task(const std::wstring& s, LPSTR) { return TaskA(s); }

// A task string read and freed.
template <class Ch>
std::wstring Take(Ch* s) {
  std::wstring w = W((const Ch*)s);
  CoTaskMemFree(s);
  return w;
}

std::wstring Text(HWND h, int id) {
  HWND c = GetDlgItem(h, id);
  int n = GetWindowTextLengthW(c);
  std::wstring s(n + 1, L'\0');
  GetWindowTextW(c, &s[0], n + 1);
  s.resize(n);
  return s;
}

struct Common {
  LPFNOLEUIHOOK hook = NULL;
  std::wstring caption;
};

bool CallHook(Common* c, HWND h, UINT msg, WPARAM wp, LPARAM lp) {
  return c && c->hook && c->hook(h, msg, wp, lp) != 0;
}

// The state behind a dialog procedure: set at WM_INITDIALOG.
template <class S>
S* StateOf(HWND h, UINT msg, LPARAM lp) {
  if (msg == WM_INITDIALOG) {
    SetWindowLongPtrW(h, DWLP_USER, lp);
    S* st = (S*)lp;
    if (!st->caption.empty()) SetWindowTextW(h, st->caption.c_str());
    return st;
  }
  return (S*)GetWindowLongPtrW(h, DWLP_USER);
}

UINT Run(const Template& t, HWND owner, DLGPROC proc, void* state) {
  INT_PTR r = DialogBoxIndirectParamW(NULL, t.Get(), owner, proc, (LPARAM)state);
  if (r == -1) return OLEUI_ERR_DIALOGFAILURE;
  return (UINT)r;
}

void FreePicture(HGLOBAL h) {
  if (!h) return;
  if (METAFILEPICT* p = (METAFILEPICT*)GlobalLock(h)) {
    if (p->hMF) DeleteMetaFile(p->hMF);
    GlobalUnlock(h);
  }
  GlobalFree(h);
}

// The label of an icon's picture: the text it draws.
std::wstring PictureLabel(HGLOBAL h) {
  std::wstring label;
  METAFILEPICT* p = h ? (METAFILEPICT*)GlobalLock(h) : NULL;
  if (!p) return label;
  UINT n = GetMetaFileBitsEx(p->hMF, 0, NULL);
  std::vector<BYTE> v(n);
  if (n) GetMetaFileBitsEx(p->hMF, n, v.data());
  GlobalUnlock(h);
  auto word = [&](size_t at) -> int { return at + 1 < v.size() ? (short)(v[at] | v[at + 1] << 8) : 0; };
  size_t pos = 18;  // after the header
  while (pos + 6 <= v.size()) {
    DWORD size = (DWORD)(v[pos] | v[pos + 1] << 8 | v[pos + 2] << 16 | (DWORD)v[pos + 3] << 24);
    WORD fn = (WORD)word(pos + 4);
    if (size < 3 || pos + size * 2 > v.size()) break;
    size_t at = pos + 6;
    int len = -1;
    if (fn == 0x0521) {  // TextOut: count, string
      len = word(at);
      at += 2;
    } else if (fn == 0x0A32) {  // ExtTextOut: y, x, count, options, [rect], string
      len = word(at + 4);
      UINT opt = (UINT)(WORD)word(at + 6);
      at += 8 + ((opt & (ETO_OPAQUE | ETO_CLIPPED)) ? 8 : 0);
    }
    if (len > 0 && at + (size_t)len <= v.size()) {
      std::string s((const char*)&v[at], (size_t)len);
      label = Widen(s.c_str());
    }
    if (fn == 0) break;
    pos += size * 2;
  }
  return label;
}

std::wstring UserType(REFCLSID clsid, DWORD form) {
  LPOLESTR t = NULL;
  std::wstring s;
  if (SUCCEEDED(OleRegGetUserType(clsid, form, &t)) && t) s = t;
  CoTaskMemFree(t);
  return s;
}

// ---------------------------------------------------------------------
// Server Busy

struct BusyState : Common {
  DWORD flags = 0;
  HTASK task = 0;
  HWND* dialogOut = NULL;
};

BOOL CALLBACK FirstWindow(HWND h, LPARAM lp) {
  if (IsWindowVisible(h) && !GetWindow(h, GW_OWNER)) {
    *(HWND*)lp = h;
    return FALSE;
  }
  return TRUE;
}

INT_PTR CALLBACK BusyProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
  BusyState* st = StateOf<BusyState>(h, msg, lp);
  if (msg == WM_INITDIALOG) {
    if (st->dialogOut) *st->dialogOut = h;
    bool notResponding = (st->flags & BZ_NOTRESPONDINGDIALOG) != 0;
    SetDlgItemTextW(h, kText,
                    notResponding ? L"This action cannot be completed because the other program is not "
                                    L"responding. Choose \"Switch To\" and correct the problem."
                                  : L"This action cannot be completed because the other program is busy. "
                                    L"Choose \"Switch To\" to activate the busy program and correct the problem.");
    if (st->flags & BZ_DISABLESWITCHTOBUTTON) EnableWindow(GetDlgItem(h, kSwitchTo), FALSE);
    if ((st->flags & BZ_DISABLERETRYBUTTON) || notResponding) EnableWindow(GetDlgItem(h, kRetry), FALSE);
    if (st->flags & BZ_DISABLECANCELBUTTON) EnableWindow(GetDlgItem(h, IDCANCEL), FALSE);
    CallHook(st, h, msg, wp, lp);
    return TRUE;
  }
  if (CallHook(st, h, msg, wp, lp)) return TRUE;
  if (msg != WM_COMMAND) return FALSE;
  switch (LOWORD(wp)) {
    case kSwitchTo: {
      // The busy program's window, in front.
      HWND w = NULL;
      if (st->task) EnumThreadWindows((DWORD)(UINT_PTR)st->task, FirstWindow, (LPARAM)&w);
      if (w) {
        if (IsIconic(w)) ShowWindow(w, SW_RESTORE);
        SetForegroundWindow(w);
      }
      EndDialog(h, OLEUI_BZ_SWITCHTOSELECTED);
      return TRUE;
    }
    case kRetry:
      EndDialog(h, OLEUI_BZ_RETRYSELECTED);
      return TRUE;
    case IDCANCEL:
      if (!(st->flags & BZ_DISABLECANCELBUTTON)) EndDialog(h, OLEUI_CANCEL);
      return TRUE;
  }
  return FALSE;
}

UINT Busy(BusyState* st, HWND owner) {
  Template t(0, 250, 84, L"Server Busy");
  t.Add(Template::kStatic, SS_LEFT, 10, 10, 230, 40, kText, L"");
  t.Add(Template::kButton, BS_DEFPUSHBUTTON | WS_TABSTOP | WS_GROUP, 40, 62, 56, 14, kSwitchTo, L"&Switch To...");
  t.Add(Template::kButton, BS_PUSHBUTTON | WS_TABSTOP, 102, 62, 56, 14, kRetry, L"&Retry");
  t.Add(Template::kButton, BS_PUSHBUTTON | WS_TABSTOP, 164, 62, 56, 14, IDCANCEL, L"Cancel");
  return Run(t, owner, BusyProc, st);
}

template <class S>
UINT BusyFrom(S* bz) {
  if (!bz) return OLEUI_ERR_STRUCTURENULL;
  if (bz->cbStruct != sizeof *bz) return OLEUI_ERR_CBSTRUCTINCORRECT;
  BusyState st;
  st.hook = bz->lpfnHook;
  st.caption = W(bz->lpszCaption);
  st.flags = bz->dwFlags;
  st.task = bz->hTask;
  st.dialogOut = bz->lphWndDialog;
  return Busy(&st, bz->hWndOwner);
}

// ---------------------------------------------------------------------
// Change Icon: the picture's current icon, the class's, or one from a
// file, with a label.

struct IconState : Common {
  DWORD flags = 0;
  CLSID clsid = CLSID_NULL;
  HGLOBAL current = NULL;
  std::wstring file;
  HGLOBAL result = NULL;  // the new picture, on OK
};

void EnableFile(HWND h) {
  bool file = IsDlgButtonChecked(h, kFromFile) == BST_CHECKED;
  EnableWindow(GetDlgItem(h, kFile), file);
  EnableWindow(GetDlgItem(h, kBrowse), file);
}

INT_PTR CALLBACK IconProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
  IconState* st = StateOf<IconState>(h, msg, lp);
  if (msg == WM_INITDIALOG) {
    int pick = (st->flags & CIF_SELECTFROMFILE) ? kFromFile
               : (st->flags & CIF_SELECTDEFAULT) || !st->current ? kDefault
                                                                 : kCurrent;
    if (!st->current) EnableWindow(GetDlgItem(h, kCurrent), FALSE);
    CheckRadioButton(h, kCurrent, kFromFile, pick);
    std::wstring label = PictureLabel(st->current);
    if (label.empty()) label = UserType(st->clsid, USERCLASSTYPE_SHORT);
    SetDlgItemTextW(h, kLabel, label.c_str());
    SetDlgItemTextW(h, kFile, st->file.c_str());
    EnableFile(h);
    CallHook(st, h, msg, wp, lp);
    return TRUE;
  }
  if (CallHook(st, h, msg, wp, lp)) return TRUE;
  if (msg != WM_COMMAND) return FALSE;
  switch (LOWORD(wp)) {
    case kCurrent:
    case kDefault:
    case kFromFile:
      EnableFile(h);
      return TRUE;
    case kBrowse: {
      WCHAR name[MAX_PATH] = {};
      std::wstring f = Text(h, kFile);
      wcsncpy(name, f.c_str(), MAX_PATH - 1);
      OPENFILENAMEW ofn = {};
      ofn.lStructSize = sizeof ofn;
      ofn.hwndOwner = h;
      ofn.lpstrFilter = L"Programs and icons\0*.exe;*.dll;*.ico\0All files\0*\0";
      ofn.lpstrFile = name;
      ofn.nMaxFile = MAX_PATH;
      ofn.Flags = OFN_FILEMUSTEXIST | OFN_HIDEREADONLY;
      if (GetOpenFileNameW(&ofn)) SetDlgItemTextW(h, kFile, name);
      return TRUE;
    }
    case IDOK: {
      std::wstring label = Text(h, kLabel);
      if (IsDlgButtonChecked(h, kFromFile) == BST_CHECKED) {
        std::wstring file = Text(h, kFile);
        HICON icon = ExtractIconW(NULL, file.c_str(), 0);
        if (!icon || icon == (HICON)1) {
          MessageBeep(MB_ICONEXCLAMATION);
          SetFocus(GetDlgItem(h, kFile));
          return TRUE;
        }
        st->result = OleMetafilePictFromIconAndLabel(icon, (LPOLESTR)label.c_str(), (LPOLESTR)file.c_str(), 0);
        DestroyIcon(icon);
        st->file = file;
      } else if (IsDlgButtonChecked(h, kCurrent) == BST_CHECKED && label == PictureLabel(st->current)) {
        st->result = NULL;  // as it is
      } else {
        // The class's icon (the current one's look is the class's too).
        st->result = OleGetIconOfClass(st->clsid, (LPOLESTR)label.c_str(), FALSE);
      }
      EndDialog(h, OLEUI_OK);
      return TRUE;
    }
    case IDCANCEL:
      EndDialog(h, OLEUI_CANCEL);
      return TRUE;
  }
  return FALSE;
}

UINT ChangeIcon(IconState* st, HWND owner) {
  Template t(0, 250, 110, L"Change Icon");
  t.Add(Template::kButton, BS_AUTORADIOBUTTON | WS_GROUP | WS_TABSTOP, 10, 10, 120, 10, kCurrent, L"&Current");
  t.Add(Template::kButton, BS_AUTORADIOBUTTON, 10, 24, 120, 10, kDefault, L"&Default");
  t.Add(Template::kButton, BS_AUTORADIOBUTTON, 10, 38, 120, 10, kFromFile, L"From &File:");
  t.Add(Template::kEdit, ES_AUTOHSCROLL | WS_BORDER | WS_TABSTOP | WS_GROUP, 22, 52, 120, 12, kFile, L"");
  t.Add(Template::kButton, BS_PUSHBUTTON | WS_TABSTOP, 146, 51, 40, 14, kBrowse, L"&Browse...");
  t.Add(Template::kStatic, SS_LEFT, 10, 74, 40, 8, -1, L"&Label:");
  t.Add(Template::kEdit, ES_AUTOHSCROLL | WS_BORDER | WS_TABSTOP, 50, 72, 136, 12, kLabel, L"");
  t.Add(Template::kButton, BS_DEFPUSHBUTTON | WS_TABSTOP | WS_GROUP, 194, 10, 50, 14, IDOK, L"OK");
  t.Add(Template::kButton, BS_PUSHBUTTON | WS_TABSTOP, 194, 28, 50, 14, IDCANCEL, L"Cancel");
  return Run(t, owner, IconProc, st);
}

template <class S>
UINT ChangeIconFrom(S* ci) {
  if (!ci) return OLEUI_ERR_STRUCTURENULL;
  if (ci->cbStruct != sizeof *ci) return OLEUI_ERR_CBSTRUCTINCORRECT;
  IconState st;
  st.hook = ci->lpfnHook;
  st.caption = W(ci->lpszCaption);
  st.flags = ci->dwFlags;
  st.clsid = ci->clsid;
  st.current = ci->hMetaPict;
  if (ci->dwFlags & CIF_USEICONEXE) st.file = W(ci->szIconExe);
  UINT r = ChangeIcon(&st, ci->hWndOwner);
  if (r == OLEUI_OK && st.result) {
    FreePicture(ci->hMetaPict);
    ci->hMetaPict = st.result;
  }
  return r;
}

// ---------------------------------------------------------------------
// Convert: to a class that reads and writes the object's format, or
// activated as one that reads it.

struct ConvertState : Common {
  DWORD flags = 0;
  CLSID clsid = CLSID_NULL, convertDefault = CLSID_NULL, activateDefault = CLSID_NULL;
  WORD format = 0;
  bool link = false;
  DWORD aspect = DVASPECT_CONTENT;
  std::vector<CLSID> exclude;
  std::vector<CLSID> shown;  // the list's classes
  // Out.
  CLSID clsidNew = CLSID_NULL;
  bool iconChanged = false;
  HGLOBAL icon = NULL;
};

// Whether a class's Conversion key ("Readable" or "Readwritable") lists
// a format.
bool ClassTakes(const wchar_t* guid, const wchar_t* kind, const std::wstring& format) {
  if (format.empty()) return false;
  std::wstring key = std::wstring(L"CLSID\\") + guid + L"\\Conversion\\" + kind + L"\\Main";
  WCHAR buf[1024];
  LONG cb = sizeof buf;
  if (RegQueryValueW(HKEY_CLASSES_ROOT, key.c_str(), buf, &cb) != ERROR_SUCCESS) return false;
  std::wstring list = buf;
  for (size_t start = 0;;) {
    size_t c = list.find(L',', start);
    std::wstring item = list.substr(start, c == std::wstring::npos ? c : c - start);
    while (!item.empty() && item[0] == L' ') item.erase(0, 1);
    while (!item.empty() && item.back() == L' ') item.pop_back();
    if (lstrcmpiW(item.c_str(), format.c_str()) == 0) return true;
    if (c == std::wstring::npos) return false;
    start = c + 1;
  }
}

std::wstring FormatName(WORD cf) {
  if (!cf) return L"";
  WCHAR name[256];
  int n = GetClipboardFormatNameW(cf, name, 256);
  return n > 0 ? std::wstring(name, n) : std::wstring();
}

void FillClasses(HWND h, ConvertState* st) {
  bool activate = IsDlgButtonChecked(h, kActivateAs) == BST_CHECKED;
  HWND list = GetDlgItem(h, kList);
  SendMessageW(list, LB_RESETCONTENT, 0, 0);
  st->shown.clear();
  std::vector<CLSID> classes = {st->clsid};
  std::wstring format = FormatName(st->format);
  if (format.empty()) {
    // No format kept with the object: its class's file format, or its
    // ProgID, names it.
    WCHAR guid[40];
    StringFromGUID2(st->clsid, guid, 40);
    for (const wchar_t* sub : {L"\\DataFormats\\DefaultFile", L"\\ProgID"}) {
      std::wstring key = std::wstring(L"CLSID\\") + guid + sub;
      WCHAR buf[256];
      LONG cb = sizeof buf;
      if (RegQueryValueW(HKEY_CLASSES_ROOT, key.c_str(), buf, &cb) == ERROR_SUCCESS && buf[0]) {
        format = buf;
        break;
      }
    }
  }
  HKEY root;
  if (!st->link && RegOpenKeyExW(HKEY_CLASSES_ROOT, L"CLSID", 0, KEY_READ, &root) == ERROR_SUCCESS) {
    for (DWORD i = 0;; i++) {
      WCHAR guid[64];
      if (RegEnumKeyW(root, i, guid, 64) != ERROR_SUCCESS) break;
      CLSID c;
      if (FAILED(CLSIDFromString(guid, &c)) || IsEqualCLSID(c, st->clsid)) continue;
      if (ClassTakes(guid, activate ? L"Readable" : L"Readwritable", format)) classes.push_back(c);
    }
    RegCloseKey(root);
  }
  for (const CLSID& c : classes) {
    bool out = false;
    for (const CLSID& x : st->exclude) out = out || IsEqualCLSID(x, c);
    if (out) continue;
    std::wstring name = UserType(c, USERCLASSTYPE_FULL);
    if (name.empty()) continue;
    int k = (int)SendMessageW(list, LB_ADDSTRING, 0, (LPARAM)name.c_str());
    SendMessageW(list, LB_SETITEMDATA, k, (LPARAM)st->shown.size());
    st->shown.push_back(c);
  }
  // The default, or the object's own class.
  const CLSID& want = activate ? ((st->flags & CF_SETACTIVATEDEFAULT) ? st->activateDefault : st->clsid)
                               : ((st->flags & CF_SETCONVERTDEFAULT) ? st->convertDefault : st->clsid);
  int n = (int)SendMessageW(list, LB_GETCOUNT, 0, 0);
  int sel = 0;
  for (int k = 0; k < n; k++) {
    size_t i = (size_t)SendMessageW(list, LB_GETITEMDATA, k, 0);
    if (i < st->shown.size() && IsEqualCLSID(st->shown[i], want)) sel = k;
  }
  SendMessageW(list, LB_SETCURSEL, sel, 0);
}

INT_PTR CALLBACK ConvertProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
  ConvertState* st = StateOf<ConvertState>(h, msg, lp);
  if (msg == WM_INITDIALOG) {
    std::wstring type = L"Current type: " + UserType(st->clsid, USERCLASSTYPE_FULL);
    SetDlgItemTextW(h, kType, type.c_str());
    bool noActivate = (st->flags & (CF_DISABLEACTIVATEAS | CF_CONVERTONLY)) || st->link;
    CheckRadioButton(h, kConvertTo, kActivateAs,
                     (st->flags & CF_SELECTACTIVATEAS) && !noActivate ? kActivateAs : kConvertTo);
    if (noActivate) EnableWindow(GetDlgItem(h, kActivateAs), FALSE);
    CheckDlgButton(h, kAsIcon, st->aspect == DVASPECT_ICON ? BST_CHECKED : BST_UNCHECKED);
    if (st->flags & CF_DISABLEDISPLAYASICON) EnableWindow(GetDlgItem(h, kAsIcon), FALSE);
    FillClasses(h, st);
    CallHook(st, h, msg, wp, lp);
    return TRUE;
  }
  if (CallHook(st, h, msg, wp, lp)) return TRUE;
  if (msg != WM_COMMAND) return FALSE;
  switch (LOWORD(wp)) {
    case kConvertTo:
    case kActivateAs:
      FillClasses(h, st);
      return TRUE;
    case kList:
      if (HIWORD(wp) == LBN_DBLCLK) PostMessageW(h, WM_COMMAND, IDOK, 0);
      return TRUE;
    case IDOK: {
      HWND list = GetDlgItem(h, kList);
      int k = (int)SendMessageW(list, LB_GETCURSEL, 0, 0);
      size_t i = k >= 0 ? (size_t)SendMessageW(list, LB_GETITEMDATA, k, 0) : st->shown.size();
      st->clsidNew = i < st->shown.size() ? st->shown[i] : st->clsid;
      st->flags &= ~(DWORD)(CF_SELECTCONVERTTO | CF_SELECTACTIVATEAS);
      st->flags |= IsDlgButtonChecked(h, kActivateAs) == BST_CHECKED ? CF_SELECTACTIVATEAS : CF_SELECTCONVERTTO;
      DWORD aspect = IsDlgButtonChecked(h, kAsIcon) == BST_CHECKED ? DVASPECT_ICON : DVASPECT_CONTENT;
      // A new icon when the object becomes one, or its class changes.
      if (aspect == DVASPECT_ICON && (st->aspect != DVASPECT_ICON || !IsEqualCLSID(st->clsidNew, st->clsid))) {
        st->icon = OleGetIconOfClass(st->clsidNew, NULL, TRUE);
        st->iconChanged = st->icon != NULL;
      }
      st->aspect = aspect;
      EndDialog(h, OLEUI_OK);
      return TRUE;
    }
    case IDCANCEL:
      EndDialog(h, OLEUI_CANCEL);
      return TRUE;
  }
  return FALSE;
}

UINT Convert(ConvertState* st, HWND owner) {
  Template t(0, 260, 130, L"Convert");
  t.Add(Template::kStatic, SS_LEFT, 10, 8, 180, 8, kType, L"");
  t.Add(Template::kButton, BS_AUTORADIOBUTTON | WS_GROUP | WS_TABSTOP, 10, 24, 70, 10, kConvertTo, L"&Convert to:");
  t.Add(Template::kButton, BS_AUTORADIOBUTTON, 10, 38, 70, 10, kActivateAs, L"&Activate as:");
  t.Add(Template::kListBox, LBS_NOTIFY | LBS_SORT | WS_VSCROLL | WS_BORDER | WS_TABSTOP | WS_GROUP, 84, 24, 110,
        72, kList, L"");
  t.Add(Template::kButton, BS_AUTOCHECKBOX | WS_TABSTOP, 84, 102, 110, 10, kAsIcon, L"&Display As Icon");
  t.Add(Template::kButton, BS_DEFPUSHBUTTON | WS_TABSTOP | WS_GROUP, 202, 8, 50, 14, IDOK, L"OK");
  t.Add(Template::kButton, BS_PUSHBUTTON | WS_TABSTOP, 202, 26, 50, 14, IDCANCEL, L"Cancel");
  return Run(t, owner, ConvertProc, st);
}

template <class S>
UINT ConvertFrom(S* cv) {
  if (!cv) return OLEUI_ERR_STRUCTURENULL;
  if (cv->cbStruct != sizeof *cv) return OLEUI_ERR_CBSTRUCTINCORRECT;
  ConvertState st;
  st.hook = cv->lpfnHook;
  st.caption = W(cv->lpszCaption);
  st.flags = cv->dwFlags;
  st.clsid = cv->clsid;
  st.convertDefault = cv->clsidConvertDefault;
  st.activateDefault = cv->clsidActivateDefault;
  st.format = cv->wFormat;
  st.link = cv->fIsLinkedObject != FALSE;
  st.aspect = cv->dvAspect ? cv->dvAspect : DVASPECT_CONTENT;
  if (cv->lpClsidExclude) st.exclude.assign(cv->lpClsidExclude, cv->lpClsidExclude + cv->cClsidExclude);
  UINT r = Convert(&st, cv->hWndOwner);
  if (r == OLEUI_OK) {
    cv->clsidNew = st.clsidNew;
    cv->dwFlags = st.flags;
    cv->dvAspect = st.aspect;
    cv->fObjectsIconChanged = st.iconChanged;
    if (st.icon) {
      FreePicture(cv->hMetaPict);
      cv->hMetaPict = st.icon;
    }
  }
  return r;
}

// ---------------------------------------------------------------------
// Change Source

struct SourceState : Common {
  DWORD flags = 0;
  std::wstring name;  // the source's display name
  ULONG fileLength = 0;
  std::wstring from, to;
};

INT_PTR CALLBACK SourceProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
  SourceState* st = StateOf<SourceState>(h, msg, lp);
  if (msg == WM_INITDIALOG) {
    SetDlgItemTextW(h, kSource, st->name.c_str());
    CallHook(st, h, msg, wp, lp);
    return TRUE;
  }
  if (CallHook(st, h, msg, wp, lp)) return TRUE;
  if (msg != WM_COMMAND) return FALSE;
  switch (LOWORD(wp)) {
    case kBrowse: {
      // A new file; the item, if any, stays.
      std::wstring now = Text(h, kSource);
      size_t len = std::min((size_t)st->fileLength, st->name.size());
      std::wstring item = now.size() >= st->name.size() - len &&
                                  now.compare(now.size() - (st->name.size() - len), std::wstring::npos,
                                              st->name, len, std::wstring::npos) == 0
                              ? st->name.substr(len)
                              : std::wstring();
      WCHAR file[MAX_PATH] = {};
      wcsncpy(file, now.substr(0, now.size() - item.size()).c_str(), MAX_PATH - 1);
      OPENFILENAMEW ofn = {};
      ofn.lStructSize = sizeof ofn;
      ofn.hwndOwner = h;
      ofn.lpstrFilter = L"All files\0*\0";
      ofn.lpstrFile = file;
      ofn.nMaxFile = MAX_PATH;
      ofn.Flags = OFN_FILEMUSTEXIST | OFN_HIDEREADONLY;
      if (GetOpenFileNameW(&ofn)) SetDlgItemTextW(h, kSource, (std::wstring(file) + item).c_str());
      return TRUE;
    }
    case IDOK: {
      std::wstring now = Text(h, kSource);
      // Valid when it names something.
      IBindCtx* bc = NULL;
      IMoniker* mk = NULL;
      ULONG eaten = 0;
      bool valid = SUCCEEDED(CreateBindCtx(0, &bc)) && SUCCEEDED(MkParseDisplayName(bc, now.c_str(), &eaten, &mk));
      SafeRelease(mk);
      SafeRelease(bc);
      if (valid)
        st->flags |= CSF_VALIDSOURCE;
      else
        st->flags &= ~(DWORD)CSF_VALIDSOURCE;
      // The file part: the same item after it as before.
      size_t oldLen = std::min((size_t)st->fileLength, st->name.size());
      std::wstring item = st->name.substr(oldLen);
      ULONG newLen = (ULONG)now.size();
      if (!item.empty() && now.size() >= item.size() && now.compare(now.size() - item.size(), item.size(), item) == 0)
        newLen = (ULONG)(now.size() - item.size());
      // What changed: the two names without their common end.
      size_t a = st->name.size(), b = now.size();
      while (a > 0 && b > 0 && st->name[a - 1] == now[b - 1]) a--, b--;
      st->from = st->name.substr(0, a);
      st->to = now.substr(0, b);
      st->name = now;
      st->fileLength = newLen;
      EndDialog(h, OLEUI_OK);
      return TRUE;
    }
    case IDCANCEL:
      EndDialog(h, OLEUI_CANCEL);
      return TRUE;
  }
  return FALSE;
}

UINT ChangeSource(SourceState* st, HWND owner) {
  Template t(0, 280, 64, L"Change Source");
  t.Add(Template::kStatic, SS_LEFT, 10, 10, 60, 8, -1, L"&Source:");
  t.Add(Template::kEdit, ES_AUTOHSCROLL | WS_BORDER | WS_TABSTOP, 10, 22, 200, 12, kSource, L"");
  t.Add(Template::kButton, BS_PUSHBUTTON | WS_TABSTOP, 10, 42, 50, 14, kBrowse, L"&Browse...");
  t.Add(Template::kButton, BS_DEFPUSHBUTTON | WS_TABSTOP | WS_GROUP, 222, 8, 50, 14, IDOK, L"OK");
  t.Add(Template::kButton, BS_PUSHBUTTON | WS_TABSTOP, 222, 26, 50, 14, IDCANCEL, L"Cancel");
  return Run(t, owner, SourceProc, st);
}

template <class S, class Ch>
UINT ChangeSourceFrom(S* cs, Ch*) {
  if (!cs) return OLEUI_ERR_STRUCTURENULL;
  if (cs->cbStruct != sizeof *cs) return OLEUI_ERR_CBSTRUCTINCORRECT;
  SourceState st;
  st.hook = cs->lpfnHook;
  st.caption = W(cs->lpszCaption);
  st.flags = cs->dwFlags;
  st.name = W(cs->lpszDisplayName);
  st.fileLength = cs->nFileLength;
  if (!cs->lpszDisplayName && cs->lpOleUILinkContainer && cs->dwLink) {
    Ch* name = NULL;
    ULONG len = 0;
    if (SUCCEEDED(cs->lpOleUILinkContainer->GetLinkSource(cs->dwLink, &name, &len, NULL, NULL, NULL, NULL))) {
      st.name = Take(name);
      st.fileLength = len;
    }
  }
  UINT r = ChangeSource(&st, cs->hWndOwner);
  if (r == OLEUI_OK) {
    CoTaskMemFree(cs->lpszDisplayName);
    CoTaskMemFree(cs->lpszFrom);
    CoTaskMemFree(cs->lpszTo);
    cs->lpszDisplayName = Task(st.name, (Ch*)NULL);
    cs->nFileLength = st.fileLength;
    cs->lpszFrom = Task(st.from, (Ch*)NULL);
    cs->lpszTo = Task(st.to, (Ch*)NULL);
    cs->dwFlags = st.flags;
  }
  return r;
}

// ---------------------------------------------------------------------
// Updating links: each automatic link, with a progress bar and Stop.

struct UpdateState : Common {
  std::function<DWORD(DWORD)> next;
  std::function<DWORD(DWORD)> options;
  std::function<HRESULT(DWORD)> update;
  bool stopped = false;
};

INT_PTR CALLBACK UpdateProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
  UpdateState* st = StateOf<UpdateState>(h, msg, lp);
  if (msg == WM_INITDIALOG) return TRUE;
  if (msg == WM_COMMAND && LOWORD(wp) == IDCANCEL) {
    st->stopped = true;
    return TRUE;
  }
  return FALSE;
}

BOOL UpdateLinks(UpdateState* st, HWND owner, int count) {
  Template t(0, 220, 64, L"Update Links");
  t.Add(Template::kStatic, SS_LEFT, 10, 8, 200, 8, kText, L"Updating links...");
  t.Add(L"msctls_progress32", WS_BORDER, 10, 22, 200, 10, kProgress, L"");
  t.Add(Template::kButton, BS_DEFPUSHBUTTON | WS_TABSTOP, 85, 42, 50, 14, IDCANCEL, L"&Stop");
  HWND h = CreateDialogIndirectParamW(NULL, t.Get(), owner, UpdateProc, (LPARAM)st);
  if (h) {
    if (owner) EnableWindow(owner, FALSE);
    ShowWindow(h, SW_SHOW);
    SendDlgItemMessageW(h, kProgress, PBM_SETRANGE32, 0, count > 0 ? count : 1);
  }
  BOOL ok = TRUE;
  int done = 0;
  for (DWORD id = st->next(0); id && !st->stopped; id = st->next(id)) {
    if (st->options(id) == OLEUPDATE_ALWAYS && FAILED(st->update(id))) ok = FALSE;
    done++;
    if (h) SendDlgItemMessageW(h, kProgress, PBM_SETPOS, done, 0);
    MSG m;
    while (PeekMessageW(&m, NULL, 0, 0, PM_REMOVE)) {
      if (h && IsDialogMessageW(h, &m)) continue;
      TranslateMessage(&m);
      DispatchMessageW(&m);
    }
  }
  if (h) {
    if (owner) EnableWindow(owner, TRUE);
    DestroyWindow(h);
  }
  return ok && !st->stopped;
}

template <class C, class Ch>
BOOL UpdateLinksFrom(C* c, HWND parent, const Ch* title, int count) {
  if (!c) return FALSE;
  UpdateState st;
  st.caption = W(title);
  st.next = [c](DWORD id) { return c->GetNextLink(id); };
  st.options = [c](DWORD id) {
    DWORD opt = 0;
    return SUCCEEDED(c->GetLinkUpdateOptions(id, &opt)) ? opt : 0;
  };
  st.update = [c](DWORD id) { return c->UpdateLink(id, FALSE, FALSE); };
  return UpdateLinks(&st, parent, count);
}

// ---------------------------------------------------------------------
// Object Properties: a property sheet of General, View and (for a link)
// Link pages.

struct PropsState {
  DWORD flags = 0;
  DWORD object = 0, link = 0;
  LPFNOLEUIHOOK hooks[3] = {};
  int scaleMin = 0, scaleMax = 0;
  DWORD viewFlags = 0;
  // The object.
  std::function<HRESULT(std::wstring*, std::wstring*, DWORD*)> info;  // type, location, size
  std::function<HRESULT(HGLOBAL*, DWORD*, int*)> getView;
  std::function<HRESULT(HGLOBAL, DWORD, int, BOOL)> setView;
  std::function<HRESULT(CLSID*, WORD*)> convertInfo;
  std::function<HRESULT(REFCLSID)> convert;
  // The link.
  std::function<HRESULT(std::wstring*)> source;
  std::function<HRESULT(FILETIME*)> lastUpdate;
  std::function<HRESULT(DWORD*)> getOptions;
  std::function<HRESULT(DWORD)> setOptions;
  std::function<HRESULT(const std::wstring&, ULONG)> setSource;
  std::function<HRESULT()> updateNow, openSource, breakLink;
  ULONG fileLength = 0;
  // The view as it was.
  HGLOBAL picture = NULL;
  DWORD aspect = DVASPECT_CONTENT;
  int scale = 100;
};

struct PageState {
  PropsState* props;
  int page;  // 0 General, 1 View, 2 Link
};

void ShowLink(HWND h, PropsState* p) {
  std::wstring name;
  p->source(&name);
  SetDlgItemTextW(h, kSource, name.c_str());
  FILETIME ft = {};
  std::wstring when = L"Unknown";
  if (SUCCEEDED(p->lastUpdate(&ft)) && (ft.dwLowDateTime || ft.dwHighDateTime)) {
    FILETIME local;
    SYSTEMTIME t;
    FileTimeToLocalFileTime(&ft, &local);
    FileTimeToSystemTime(&local, &t);
    WCHAR d[64] = {}, tm[64] = {};
    GetDateFormatW(LOCALE_USER_DEFAULT, DATE_SHORTDATE, &t, NULL, d, 64);
    GetTimeFormatW(LOCALE_USER_DEFAULT, 0, &t, NULL, tm, 64);
    when = std::wstring(d) + L" " + tm;
  }
  SetDlgItemTextW(h, kUpdated, (L"Last updated: " + when).c_str());
}

INT_PTR CALLBACK PageProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
  PageState* ps = (PageState*)GetWindowLongPtrW(h, DWLP_USER);
  if (msg == WM_INITDIALOG) {
    PROPSHEETPAGEW* page = (PROPSHEETPAGEW*)lp;
    ps = (PageState*)page->lParam;
    SetWindowLongPtrW(h, DWLP_USER, (LONG_PTR)ps);
    PropsState* p = ps->props;
    if (ps->page == 0) {
      std::wstring type, location;
      DWORD size = (DWORD)-1;
      p->info(&type, &location, &size);
      SetDlgItemTextW(h, kType, (L"Type: " + type).c_str());
      WCHAR buf[64];
      if (size == (DWORD)-1)
        wcscpy(buf, L"Size: Unknown");
      else if (size == (DWORD)-2)
        wcscpy(buf, L"Size: Too large to tell");
      else
        swprintf(buf, 64, L"Size: %lu bytes", (unsigned long)size);
      SetDlgItemTextW(h, kSize, buf);
      SetDlgItemTextW(h, kLocation, (L"Location: " + location).c_str());
      if (p->flags & OPF_DISABLECONVERT) EnableWindow(GetDlgItem(h, kConvert), FALSE);
    } else if (ps->page == 1) {
      CheckRadioButton(h, kAsContent, kAsIcon, p->aspect == DVASPECT_ICON ? kAsIcon : kAsContent);
      SetDlgItemInt(h, kScale, (UINT)p->scale, FALSE);
      CheckDlgButton(h, kRelative, (p->viewFlags & VPF_SELECTRELATIVE) ? BST_CHECKED : BST_UNCHECKED);
      if (p->viewFlags & VPF_DISABLERELATIVE) EnableWindow(GetDlgItem(h, kRelative), FALSE);
      if (p->viewFlags & VPF_DISABLESCALE) EnableWindow(GetDlgItem(h, kScale), FALSE);
    } else {
      ShowLink(h, p);
      DWORD opt = OLEUPDATE_ALWAYS;
      p->getOptions(&opt);
      CheckRadioButton(h, kAutomatic, kManual, opt == OLEUPDATE_ONCALL ? kManual : kAutomatic);
    }
    if (p->hooks[ps->page]) p->hooks[ps->page](h, msg, wp, lp);
    return TRUE;
  }
  if (!ps) return FALSE;
  PropsState* p = ps->props;
  if (p->hooks[ps->page] && p->hooks[ps->page](h, msg, wp, lp)) return TRUE;
  if (msg == WM_NOTIFY && ((NMHDR*)lp)->code == PSN_APPLY) {
    LONG_PTR result = PSNRET_NOERROR;
    if (ps->page == 1) {
      BOOL got = FALSE;
      int scale = (int)GetDlgItemInt(h, kScale, &got, FALSE);
      if (!got || (p->scaleMax > p->scaleMin && (scale < p->scaleMin || scale > p->scaleMax))) {
        result = PSNRET_INVALID_NOCHANGEPAGE;
      } else {
        DWORD aspect = IsDlgButtonChecked(h, kAsIcon) == BST_CHECKED ? DVASPECT_ICON : DVASPECT_CONTENT;
        BOOL relative = IsDlgButtonChecked(h, kRelative) == BST_CHECKED;
        if (aspect != p->aspect || scale != p->scale) p->setView(p->picture, aspect, scale, relative);
        p->aspect = aspect;
        p->scale = scale;
      }
    } else if (ps->page == 2) {
      p->setOptions(IsDlgButtonChecked(h, kManual) == BST_CHECKED ? OLEUPDATE_ONCALL : OLEUPDATE_ALWAYS);
    }
    SetWindowLongPtrW(h, DWLP_MSGRESULT, result);
    return TRUE;
  }
  if (msg != WM_COMMAND) return FALSE;
  HWND sheet = GetParent(h);
  switch (LOWORD(wp)) {
    case kAsContent:
    case kAsIcon:
    case kRelative:
    case kAutomatic:
    case kManual:
      SendMessageW(sheet, PSM_CHANGED, (WPARAM)h, 0);
      return TRUE;
    case kScale:
      if (HIWORD(wp) == EN_CHANGE) SendMessageW(sheet, PSM_CHANGED, (WPARAM)h, 0);
      return TRUE;
    case kConvert: {
      ConvertState cs;
      if (FAILED(p->convertInfo(&cs.clsid, &cs.format))) return TRUE;
      cs.flags = CF_SELECTCONVERTTO | CF_DISABLEDISPLAYASICON;
      cs.link = (p->flags & OPF_OBJECTISLINK) != 0;
      if (Convert(&cs, h) == OLEUI_OK && !IsEqualCLSID(cs.clsidNew, cs.clsid)) p->convert(cs.clsidNew);
      return TRUE;
    }
    case kUpdateNow:
      p->updateNow();
      ShowLink(h, p);
      return TRUE;
    case kOpenSource:
      p->openSource();
      return TRUE;
    case kChangeSource: {
      SourceState ss;
      p->source(&ss.name);
      ss.fileLength = p->fileLength ? p->fileLength : (ULONG)ss.name.size();
      if (ChangeSource(&ss, h) == OLEUI_OK && (ss.flags & CSF_VALIDSOURCE)) {
        p->setSource(ss.name, ss.fileLength);
        ShowLink(h, p);
      }
      return TRUE;
    }
    case kBreakLink:
      p->breakLink();
      // Not a link any more: the sheet is done.
      PostMessageW(sheet, WM_COMMAND, IDOK, 0);
      return TRUE;
  }
  return FALSE;
}

UINT ObjectProperties(PropsState* p, HWND owner, const std::wstring& caption) {
  Template general(WS_CHILD | DS_CONTROL, 220, 120, L"General");
  general.Add(Template::kStatic, SS_LEFT | SS_NOPREFIX, 10, 10, 200, 8, kType, L"");
  general.Add(Template::kStatic, SS_LEFT, 10, 24, 200, 8, kSize, L"");
  general.Add(Template::kStatic, SS_LEFT | SS_NOPREFIX, 10, 38, 200, 24, kLocation, L"");
  general.Add(Template::kButton, BS_PUSHBUTTON | WS_TABSTOP, 10, 96, 60, 14, kConvert, L"&Convert...");
  Template view(WS_CHILD | DS_CONTROL, 220, 120, L"View");
  view.Add(Template::kButton, BS_AUTORADIOBUTTON | WS_GROUP | WS_TABSTOP, 10, 10, 160, 10, kAsContent,
           L"Display as &editable information");
  view.Add(Template::kButton, BS_AUTORADIOBUTTON, 10, 24, 160, 10, kAsIcon, L"Display as &icon");
  view.Add(Template::kStatic, SS_LEFT, 10, 46, 40, 8, -1, L"&Scale (%):");
  view.Add(Template::kEdit, ES_NUMBER | ES_AUTOHSCROLL | WS_BORDER | WS_TABSTOP | WS_GROUP, 54, 44, 40, 12, kScale,
           L"");
  view.Add(Template::kButton, BS_AUTOCHECKBOX | WS_TABSTOP, 10, 64, 160, 10, kRelative,
           L"&Relative to original size");
  Template link(WS_CHILD | DS_CONTROL, 220, 120, L"Link");
  link.Add(Template::kStatic, SS_LEFT | SS_NOPREFIX, 10, 10, 200, 20, kSource, L"");
  link.Add(Template::kStatic, SS_LEFT, 10, 32, 200, 8, kUpdated, L"");
  link.Add(Template::kButton, BS_AUTORADIOBUTTON | WS_GROUP | WS_TABSTOP, 10, 48, 70, 10, kAutomatic, L"&Automatic");
  link.Add(Template::kButton, BS_AUTORADIOBUTTON, 84, 48, 70, 10, kManual, L"&Manual");
  link.Add(Template::kButton, BS_PUSHBUTTON | WS_TABSTOP | WS_GROUP, 10, 96, 46, 14, kOpenSource, L"&Open Source");
  link.Add(Template::kButton, BS_PUSHBUTTON | WS_TABSTOP, 60, 96, 46, 14, kUpdateNow, L"&Update Now");
  link.Add(Template::kButton, BS_PUSHBUTTON | WS_TABSTOP, 110, 96, 50, 14, kChangeSource, L"Change &Source...");
  link.Add(Template::kButton, BS_PUSHBUTTON | WS_TABSTOP, 164, 96, 46, 14, kBreakLink, L"&Break Link");

  PageState states[3] = {{p, 0}, {p, 1}, {p, 2}};
  const Template* templates[3] = {&general, &view, &link};
  int count = (p->flags & OPF_OBJECTISLINK) ? 3 : 2;
  PROPSHEETPAGEW pages[3] = {};
  for (int i = 0; i < count; i++) {
    pages[i].dwSize = sizeof pages[i];
    pages[i].dwFlags = PSP_DLGINDIRECT;
    pages[i].pResource = (LPCDLGTEMPLATE)templates[i]->Get();
    pages[i].pfnDlgProc = PageProc;
    pages[i].lParam = (LPARAM)&states[i];
  }
  PROPSHEETHEADERW h = {};
  h.dwSize = sizeof h;
  h.dwFlags = PSH_PROPSHEETPAGE | PSH_NOAPPLYNOW;
  h.hwndParent = owner;
  h.pszCaption = caption.c_str();
  h.nPages = (UINT)count;
  h.ppsp = pages;
  INT_PTR r = PropertySheetW(&h);
  if (r < 0) return OLEUI_ERR_DIALOGFAILURE;
  return r > 0 ? OLEUI_OK : OLEUI_CANCEL;
}

template <class OP, class Ch>
UINT ObjectPropertiesFrom(OP* op, Ch*) {
  if (!op) return OLEUI_ERR_STRUCTURENULL;
  if (op->cbStruct != sizeof *op) return OLEUI_ERR_CBSTRUCTINCORRECT;
  if (!op->lpObjInfo || !op->lpGP || !op->lpVP || !op->lpPS) return OLEUI_ERR_STRUCTUREINVALID;
  bool isLink = (op->dwFlags & OPF_OBJECTISLINK) != 0;
  if (isLink && (!op->lpLinkInfo || !op->lpLP)) return OLEUI_ERR_STRUCTUREINVALID;
  PropsState p;
  p.flags = op->dwFlags;
  p.object = op->dwObject;
  p.link = op->dwLink;
  p.hooks[0] = op->lpGP->lpfnHook;
  p.hooks[1] = op->lpVP->lpfnHook;
  p.hooks[2] = isLink ? op->lpLP->lpfnHook : NULL;
  p.scaleMin = op->lpVP->nScaleMin;
  p.scaleMax = op->lpVP->nScaleMax;
  p.viewFlags = op->lpVP->dwFlags;
  auto* info = op->lpObjInfo;
  auto* li = op->lpLinkInfo;
  DWORD obj = op->dwObject, lk = op->dwLink;
  p.info = [info, obj](std::wstring* type, std::wstring* location, DWORD* size) {
    Ch *label = NULL, *t = NULL, *shortType = NULL, *loc = NULL;
    HRESULT hr = info->GetObjectInfo(obj, size, &label, &t, &shortType, &loc);
    *type = Take(t);
    *location = Take(loc);
    CoTaskMemFree(label);
    CoTaskMemFree(shortType);
    return hr;
  };
  p.getView = [info, obj](HGLOBAL* pic, DWORD* aspect, int* scale) { return info->GetViewInfo(obj, pic, aspect, scale); };
  p.setView = [info, obj](HGLOBAL pic, DWORD aspect, int scale, BOOL rel) {
    return info->SetViewInfo(obj, pic, aspect, scale, rel);
  };
  p.convertInfo = [info, obj](CLSID* c, WORD* f) { return info->GetConvertInfo(obj, c, f, NULL, NULL, NULL); };
  p.convert = [info, obj](REFCLSID c) { return info->ConvertObject(obj, c); };
  if (isLink) {
    ULONG* fileLen = &p.fileLength;
    p.source = [li, lk, fileLen](std::wstring* name) {
      Ch* n = NULL;
      HRESULT hr = li->GetLinkSource(lk, &n, fileLen, NULL, NULL, NULL, NULL);
      *name = Take(n);
      return hr;
    };
    p.lastUpdate = [li, lk](FILETIME* ft) { return li->GetLastUpdate(lk, ft); };
    p.getOptions = [li, lk](DWORD* opt) { return li->GetLinkUpdateOptions(lk, opt); };
    p.setOptions = [li, lk](DWORD opt) { return li->SetLinkUpdateOptions(lk, opt); };
    p.setSource = [li, lk](const std::wstring& name, ULONG len) {
      Ch* n = Task(name, (Ch*)NULL);
      ULONG eaten = 0;
      HRESULT hr = li->SetLinkSource(lk, n, len, &eaten, TRUE);
      CoTaskMemFree(n);
      return hr;
    };
    p.updateNow = [li, lk]() { return li->UpdateLink(lk, TRUE, FALSE); };
    p.openSource = [li, lk]() { return li->OpenLinkSource(lk); };
    p.breakLink = [li, lk]() { return li->CancelLink(lk); };
  }
  p.getView(&p.picture, &p.aspect, &p.scale);
  if (p.scale <= 0) p.scale = 100;
  std::wstring caption = W(op->lpPS->pszCaption);
  if (caption.empty()) {
    std::wstring type, location;
    DWORD size = 0;
    p.info(&type, &location, &size);
    caption = (type.empty() ? std::wstring(L"Object") : type) + L" Properties";
  }
  UINT r = ObjectProperties(&p, op->lpPS->hwndParent, caption);
  FreePicture(p.picture);
  return r;
}

}  // namespace

extern "C" {

UINT WINAPI OleUIBusyW(LPOLEUIBUSYW bz) { return BusyFrom(bz); }
UINT WINAPI OleUIBusyA(LPOLEUIBUSYA bz) { return BusyFrom(bz); }

UINT WINAPI OleUIChangeIconW(LPOLEUICHANGEICONW ci) { return ChangeIconFrom(ci); }
UINT WINAPI OleUIChangeIconA(LPOLEUICHANGEICONA ci) { return ChangeIconFrom(ci); }

UINT WINAPI OleUIConvertW(LPOLEUICONVERTW cv) { return ConvertFrom(cv); }
UINT WINAPI OleUIConvertA(LPOLEUICONVERTA cv) { return ConvertFrom(cv); }

UINT WINAPI OleUIChangeSourceW(LPOLEUICHANGESOURCEW cs) { return ChangeSourceFrom(cs, (WCHAR*)NULL); }
UINT WINAPI OleUIChangeSourceA(LPOLEUICHANGESOURCEA cs) { return ChangeSourceFrom(cs, (CHAR*)NULL); }

UINT WINAPI OleUIObjectPropertiesW(LPOLEUIOBJECTPROPSW op) { return ObjectPropertiesFrom(op, (WCHAR*)NULL); }
UINT WINAPI OleUIObjectPropertiesA(LPOLEUIOBJECTPROPSA op) { return ObjectPropertiesFrom(op, (CHAR*)NULL); }

BOOL WINAPI OleUIUpdateLinksW(LPOLEUILINKCONTAINERW c, HWND parent, LPWSTR title, int count) {
  return UpdateLinksFrom(c, parent, title, count);
}
BOOL WINAPI OleUIUpdateLinksA(LPOLEUILINKCONTAINERA c, HWND parent, LPSTR title, int count) {
  return UpdateLinksFrom(c, parent, title, count);
}

}  // extern "C"
