// oledlg.cpp - the OLE dialogs: Insert Object (the insertable classes in
// the registry, or a file), Paste Special (the formats of the data on
// the clipboard the caller can take) and Links (the container's links).
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include "dlgtemplate.h"
#include "ole.h"
#include "user.h"

#include <oledlg.h>
#include <string.h>

#include <functional>
#include <string>
#include <vector>

using namespace winport;

namespace {

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
};

// What the dialog procedures share: the hook and the caption.
struct Common {
  LPFNOLEUIHOOK hook = NULL;
  std::wstring caption;
};

bool CallHook(Common* c, HWND h, UINT msg, WPARAM wp, LPARAM lp) {
  return c && c->hook && c->hook(h, msg, wp, lp) != 0;
}

void InitCommon(HWND h, Common* c) {
  if (!c->caption.empty()) SetWindowTextW(h, c->caption.c_str());
}

std::wstring Utf(LPCSTR s) { return s ? Widen(s) : std::wstring(); }

UINT Run(const Template& t, HWND owner, DLGPROC proc, void* state) {
  INT_PTR r = DialogBoxIndirectParamW(NULL, t.Get(), owner, proc, (LPARAM)state);
  if (r == -1) return OLEUI_ERR_DIALOGFAILURE;
  return (UINT)r;
}

// ---------------------------------------------------------------------
// Insert Object

struct InsertState : Common {
  std::vector<std::pair<std::wstring, CLSID>> classes;
  std::vector<CLSID> exclude;
  DWORD flags = 0;
  CLSID clsid = CLSID_NULL;
  std::wstring file;
};

bool Excluded(const InsertState& st, const CLSID& c) {
  for (const CLSID& x : st.exclude)
    if (IsEqualCLSID(x, c)) return true;
  return false;
}

// The classes the registry says can be inserted.
void FindInsertable(InsertState* st) {
  HKEY root;
  if (RegOpenKeyExW(HKEY_CLASSES_ROOT, L"CLSID", 0, KEY_READ, &root) != ERROR_SUCCESS) return;
  wchar_t name[128];
  for (DWORD i = 0;; i++) {
    DWORD n = 128;
    if (RegEnumKeyExW(root, i, name, &n, NULL, NULL, NULL, NULL) != ERROR_SUCCESS) break;
    HKEY key, ins;
    if (RegOpenKeyExW(root, name, 0, KEY_READ, &key) != ERROR_SUCCESS) continue;
    if (RegOpenKeyExW(key, L"Insertable", 0, KEY_READ, &ins) == ERROR_SUCCESS) {
      RegCloseKey(ins);
      CLSID clsid;
      if (SUCCEEDED(CLSIDFromString(name, &clsid)) && !Excluded(*st, clsid)) {
        wchar_t type[256] = {0};
        LONG cb = sizeof type;
        if (RegQueryValueW(key, NULL, type, &cb) != ERROR_SUCCESS || !type[0]) wcscpy(type, name);
        st->classes.push_back({type, clsid});
      }
    }
    RegCloseKey(key);
  }
  RegCloseKey(root);
}

void ShowInsertMode(HWND h, bool fromFile) {
  EnableWindow(GetDlgItem(h, kList), !fromFile);
  EnableWindow(GetDlgItem(h, kFile), fromFile);
}

INT_PTR CALLBACK InsertProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
  InsertState* st = (InsertState*)GetWindowLongPtrW(h, DWLP_USER);
  if (msg == WM_INITDIALOG) {
    st = (InsertState*)lp;
    SetWindowLongPtrW(h, DWLP_USER, (LONG_PTR)st);
    InitCommon(h, st);
    HWND list = GetDlgItem(h, kList);
    for (size_t i = 0; i < st->classes.size(); i++) {
      int k = (int)SendMessageW(list, LB_ADDSTRING, 0, (LPARAM)st->classes[i].first.c_str());
      SendMessageW(list, LB_SETITEMDATA, k, (LPARAM)i);
    }
    SendMessageW(list, LB_SETCURSEL, 0, 0);
    bool fromFile = (st->flags & IOF_SELECTCREATEFROMFILE) != 0;
    CheckRadioButton(h, kNew, kFromFile, fromFile ? kFromFile : kNew);
    CheckDlgButton(h, kIcon, (st->flags & IOF_CHECKDISPLAYASICON) ? BST_CHECKED : BST_UNCHECKED);
    if (st->flags & IOF_DISABLEDISPLAYASICON) EnableWindow(GetDlgItem(h, kIcon), FALSE);
    SetDlgItemTextW(h, kFile, st->file.c_str());
    ShowInsertMode(h, fromFile);
    CallHook(st, h, msg, wp, lp);
    return TRUE;
  }
  if (CallHook(st, h, msg, wp, lp)) return TRUE;
  if (msg != WM_COMMAND) return FALSE;
  switch (LOWORD(wp)) {
    case kNew:
    case kFromFile:
      ShowInsertMode(h, LOWORD(wp) == kFromFile);
      return TRUE;
    case kList:
      if (HIWORD(wp) != LBN_DBLCLK) return TRUE;
      // fall through: a double click chooses
    case IDOK: {
      bool fromFile = IsDlgButtonChecked(h, kFromFile) == BST_CHECKED;
      st->flags &= ~(IOF_SELECTCREATENEW | IOF_SELECTCREATEFROMFILE | IOF_CHECKDISPLAYASICON);
      st->flags |= fromFile ? IOF_SELECTCREATEFROMFILE : IOF_SELECTCREATENEW;
      if (IsDlgButtonChecked(h, kIcon) == BST_CHECKED) st->flags |= IOF_CHECKDISPLAYASICON;
      HWND list = GetDlgItem(h, kList);
      int k = (int)SendMessageW(list, LB_GETCURSEL, 0, 0);
      if (k >= 0) {
        size_t i = (size_t)SendMessageW(list, LB_GETITEMDATA, k, 0);
        if (i < st->classes.size()) st->clsid = st->classes[i].second;
      }
      wchar_t file[MAX_PATH] = {0};
      GetDlgItemTextW(h, kFile, file, MAX_PATH);
      st->file = file;
      EndDialog(h, OLEUI_OK);
      return TRUE;
    }
    case IDCANCEL:
      EndDialog(h, OLEUI_CANCEL);
      return TRUE;
  }
  return FALSE;
}

UINT InsertObject(InsertState* st, HWND owner) {
  FindInsertable(st);
  Template t(0, 290, 136, L"Insert Object");
  t.Add(Template::kButton, BS_AUTORADIOBUTTON | WS_GROUP | WS_TABSTOP, 7, 16, 76, 10, kNew, L"Create &New");
  t.Add(Template::kButton, BS_AUTORADIOBUTTON, 7, 32, 76, 10, kFromFile, L"Create from &File");
  t.Add(Template::kStatic, SS_LEFT, 90, 7, 120, 8, -1, L"Object &Type:");
  t.Add(Template::kListBox, LBS_NOTIFY | LBS_SORT | WS_VSCROLL | WS_BORDER | WS_TABSTOP | WS_GROUP, 90, 18, 136,
        70, kList, L"");
  t.Add(Template::kEdit, ES_AUTOHSCROLL | WS_BORDER | WS_TABSTOP, 90, 92, 136, 12, kFile, L"");
  t.Add(Template::kButton, BS_AUTOCHECKBOX | WS_TABSTOP, 90, 110, 100, 10, kIcon, L"&Display As Icon");
  t.Add(Template::kButton, BS_DEFPUSHBUTTON | WS_TABSTOP | WS_GROUP, 233, 7, 50, 14, IDOK, L"OK");
  t.Add(Template::kButton, BS_PUSHBUTTON | WS_TABSTOP, 233, 24, 50, 14, IDCANCEL, L"Cancel");
  return Run(t, owner, InsertProc, st);
}

template <class IO>
void ReadInsert(const IO* io, InsertState* st) {
  st->hook = io->lpfnHook;
  st->flags = io->dwFlags;
  st->clsid = io->clsid;
  for (UINT i = 0; io->lpClsidExclude && i < io->cClsidExclude; i++) st->exclude.push_back(io->lpClsidExclude[i]);
}

template <class IO>
void FinishInsert(IO* io, InsertState* st, UINT r) {
  if (r != OLEUI_OK) return;
  io->dwFlags = st->flags;
  io->clsid = st->clsid;
  io->sc = S_OK;
  if ((st->flags & IOF_SELECTCREATENEW) && (st->flags & IOF_CREATENEWOBJECT) && io->ppvObj)
    io->sc = OleCreate(st->clsid, io->iid, io->oleRender, io->lpFormatEtc, io->lpIOleClientSite, io->lpIStorage,
                       io->ppvObj);
  if ((st->flags & IOF_SELECTCREATEFROMFILE) && (st->flags & (IOF_CREATEFILEOBJECT | IOF_CREATELINKOBJECT)) &&
      io->ppvObj) {
    if (st->flags & IOF_CHECKLINK)
      io->sc = OleCreateLinkToFile(st->file.c_str(), io->iid, io->oleRender, io->lpFormatEtc, io->lpIOleClientSite,
                                   io->lpIStorage, io->ppvObj);
    else
      io->sc = OleCreateFromFile(CLSID_NULL, st->file.c_str(), io->iid, io->oleRender, io->lpFormatEtc,
                                 io->lpIOleClientSite, io->lpIStorage, io->ppvObj);
  }
}

// ---------------------------------------------------------------------
// Paste Special

struct PasteState : Common {
  IDataObject* data = NULL;
  std::vector<std::wstring> names, results;
  std::vector<FORMATETC> formats;
  std::vector<DWORD> entryFlags;
  DWORD flags = 0;
  int selected = -1;
  bool link = false;
};

std::wstring Expand(std::wstring s) {
  size_t p = s.find(L"%s");
  if (p != std::wstring::npos) s.replace(p, 2, L"Object");
  return s;
}

void FillPasteList(HWND h, PasteState* st) {
  HWND list = GetDlgItem(h, kList);
  SendMessageW(list, LB_RESETCONTENT, 0, 0);
  bool link = IsDlgButtonChecked(h, kPasteLink) == BST_CHECKED;
  for (size_t i = 0; i < st->formats.size(); i++) {
    // Entries that only link show for Paste Link, the others for Paste.
    DWORD f = st->entryFlags[i];
    bool links = (f & (0xFF | OLEUIPASTE_LINKANYTYPE)) != 0;
    if (link ? !links : (links && !(f & OLEUIPASTE_PASTE))) continue;
    FORMATETC fe = st->formats[i];
    if (!st->data || st->data->QueryGetData(&fe) != S_OK) continue;
    int k = (int)SendMessageW(list, LB_ADDSTRING, 0, (LPARAM)Expand(st->names[i]).c_str());
    SendMessageW(list, LB_SETITEMDATA, k, (LPARAM)i);
  }
  SendMessageW(list, LB_SETCURSEL, 0, 0);
}

void ShowPasteResult(HWND h, PasteState* st) {
  HWND list = GetDlgItem(h, kList);
  int k = (int)SendMessageW(list, LB_GETCURSEL, 0, 0);
  std::wstring text;
  if (k >= 0) {
    size_t i = (size_t)SendMessageW(list, LB_GETITEMDATA, k, 0);
    if (i < st->results.size()) text = Expand(st->results[i]);
  }
  SetDlgItemTextW(h, kResult, text.c_str());
}

INT_PTR CALLBACK PasteProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
  PasteState* st = (PasteState*)GetWindowLongPtrW(h, DWLP_USER);
  if (msg == WM_INITDIALOG) {
    st = (PasteState*)lp;
    SetWindowLongPtrW(h, DWLP_USER, (LONG_PTR)st);
    InitCommon(h, st);
    CheckRadioButton(h, kPaste, kPasteLink, (st->flags & PSF_SELECTPASTELINK) ? kPasteLink : kPaste);
    CheckDlgButton(h, kIcon, (st->flags & PSF_CHECKDISPLAYASICON) ? BST_CHECKED : BST_UNCHECKED);
    if (st->flags & PSF_DISABLEDISPLAYASICON) EnableWindow(GetDlgItem(h, kIcon), FALSE);
    FillPasteList(h, st);
    ShowPasteResult(h, st);
    CallHook(st, h, msg, wp, lp);
    return TRUE;
  }
  if (CallHook(st, h, msg, wp, lp)) return TRUE;
  if (msg != WM_COMMAND) return FALSE;
  switch (LOWORD(wp)) {
    case kPaste:
    case kPasteLink:
      FillPasteList(h, st);
      ShowPasteResult(h, st);
      return TRUE;
    case kList:
      if (HIWORD(wp) == LBN_SELCHANGE) ShowPasteResult(h, st);
      if (HIWORD(wp) != LBN_DBLCLK) return TRUE;
      // fall through: a double click chooses
    case IDOK: {
      HWND list = GetDlgItem(h, kList);
      int k = (int)SendMessageW(list, LB_GETCURSEL, 0, 0);
      st->selected = k >= 0 ? (int)SendMessageW(list, LB_GETITEMDATA, k, 0) : -1;
      st->link = IsDlgButtonChecked(h, kPasteLink) == BST_CHECKED;
      st->flags &= ~(PSF_SELECTPASTE | PSF_SELECTPASTELINK | PSF_CHECKDISPLAYASICON);
      st->flags |= st->link ? PSF_SELECTPASTELINK : PSF_SELECTPASTE;
      if (IsDlgButtonChecked(h, kIcon) == BST_CHECKED) st->flags |= PSF_CHECKDISPLAYASICON;
      EndDialog(h, OLEUI_OK);
      return TRUE;
    }
    case IDCANCEL:
      EndDialog(h, OLEUI_CANCEL);
      return TRUE;
  }
  return FALSE;
}

UINT PasteSpecial(PasteState* st, HWND owner) {
  Template t(0, 290, 130, L"Paste Special");
  t.Add(Template::kStatic, SS_LEFT, 7, 7, 220, 8, kSource, L"Source: the Clipboard");
  t.Add(Template::kButton, BS_AUTORADIOBUTTON | WS_GROUP | WS_TABSTOP, 7, 30, 60, 10, kPaste, L"&Paste");
  t.Add(Template::kButton, BS_AUTORADIOBUTTON, 7, 46, 60, 10, kPasteLink, L"Paste &Link");
  t.Add(Template::kStatic, SS_LEFT, 74, 20, 60, 8, -1, L"&As:");
  t.Add(Template::kListBox, LBS_NOTIFY | WS_VSCROLL | WS_BORDER | WS_TABSTOP | WS_GROUP, 74, 30, 150, 56, kList,
        L"");
  t.Add(Template::kButton, BS_AUTOCHECKBOX | WS_TABSTOP, 74, 90, 100, 10, kIcon, L"&Display As Icon");
  t.Add(Template::kStatic, SS_LEFT, 7, 106, 220, 18, kResult, L"");
  t.Add(Template::kButton, BS_DEFPUSHBUTTON | WS_TABSTOP | WS_GROUP, 233, 7, 50, 14, IDOK, L"OK");
  t.Add(Template::kButton, BS_PUSHBUTTON | WS_TABSTOP, 233, 24, 50, 14, IDCANCEL, L"Cancel");
  return Run(t, owner, PasteProc, st);
}

template <class PS, class Conv>
void ReadPaste(PS* ps, PasteState* st, Conv conv) {
  st->hook = ps->lpfnHook;
  st->flags = ps->dwFlags;
  if (!ps->lpSrcDataObj) {
    // The data on the clipboard; the caller releases it.
    OleGetClipboard(&ps->lpSrcDataObj);
  }
  st->data = ps->lpSrcDataObj;
  for (int i = 0; ps->arrPasteEntries && i < ps->cPasteEntries; i++) {
    st->formats.push_back(ps->arrPasteEntries[i].fmtetc);
    st->names.push_back(conv(ps->arrPasteEntries[i].lpstrFormatName));
    st->results.push_back(conv(ps->arrPasteEntries[i].lpstrResultText));
    st->entryFlags.push_back(ps->arrPasteEntries[i].dwFlags);
  }
}

template <class PS>
void FinishPaste(PS* ps, PasteState* st, UINT r) {
  if (r != OLEUI_OK) return;
  ps->dwFlags = st->flags;
  ps->nSelectedIndex = st->selected;
  ps->fLink = st->link;
}

// ---------------------------------------------------------------------
// Links

struct LinkInfo {
  DWORD id;
  std::wstring name, type;
};

struct LinksState : Common {
  std::function<std::vector<LinkInfo>()> list;
  std::function<void(DWORD)> update, open, cancel;
  std::vector<LinkInfo> links;
};

void FillLinks(HWND h, LinksState* st) {
  st->links = st->list();
  HWND list = GetDlgItem(h, kList);
  SendMessageW(list, LB_RESETCONTENT, 0, 0);
  for (size_t i = 0; i < st->links.size(); i++) {
    std::wstring s = st->links[i].name + L"\t" + st->links[i].type;
    int k = (int)SendMessageW(list, LB_ADDSTRING, 0, (LPARAM)s.c_str());
    SendMessageW(list, LB_SETITEMDATA, k, (LPARAM)i);
  }
  SendMessageW(list, LB_SETCURSEL, 0, 0);
  bool any = !st->links.empty();
  for (int id : {kUpdateNow, kOpenSource, kBreakLink}) EnableWindow(GetDlgItem(h, id), any);
}

INT_PTR CALLBACK LinksProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
  LinksState* st = (LinksState*)GetWindowLongPtrW(h, DWLP_USER);
  if (msg == WM_INITDIALOG) {
    st = (LinksState*)lp;
    SetWindowLongPtrW(h, DWLP_USER, (LONG_PTR)st);
    InitCommon(h, st);
    FillLinks(h, st);
    CallHook(st, h, msg, wp, lp);
    return TRUE;
  }
  if (CallHook(st, h, msg, wp, lp)) return TRUE;
  if (msg != WM_COMMAND) return FALSE;
  auto current = [&]() -> DWORD {
    int k = (int)SendMessageW(GetDlgItem(h, kList), LB_GETCURSEL, 0, 0);
    if (k < 0) return 0;
    size_t i = (size_t)SendMessageW(GetDlgItem(h, kList), LB_GETITEMDATA, k, 0);
    return i < st->links.size() ? st->links[i].id : 0;
  };
  switch (LOWORD(wp)) {
    case kUpdateNow:
      if (DWORD id = current()) st->update(id);
      FillLinks(h, st);
      return TRUE;
    case kOpenSource:
      if (DWORD id = current()) st->open(id);
      return TRUE;
    case kBreakLink:
      if (DWORD id = current()) st->cancel(id);
      FillLinks(h, st);
      return TRUE;
    case IDOK:
      EndDialog(h, OLEUI_OK);
      return TRUE;
    case IDCANCEL:
      EndDialog(h, OLEUI_CANCEL);
      return TRUE;
  }
  return FALSE;
}

UINT EditLinks(LinksState* st, HWND owner) {
  Template t(0, 290, 120, L"Links");
  t.Add(Template::kStatic, SS_LEFT, 7, 7, 220, 8, -1, L"&Links:");
  t.Add(Template::kListBox, LBS_NOTIFY | LBS_USETABSTOPS | WS_VSCROLL | WS_BORDER | WS_TABSTOP, 7, 18, 220, 94,
        kList, L"");
  t.Add(Template::kButton, BS_DEFPUSHBUTTON | WS_TABSTOP | WS_GROUP, 233, 7, 50, 14, IDOK, L"Close");
  t.Add(Template::kButton, BS_PUSHBUTTON | WS_TABSTOP, 233, 26, 50, 14, kUpdateNow, L"&Update Now");
  t.Add(Template::kButton, BS_PUSHBUTTON | WS_TABSTOP, 233, 44, 50, 14, kOpenSource, L"&Open Source");
  t.Add(Template::kButton, BS_PUSHBUTTON | WS_TABSTOP, 233, 62, 50, 14, kBreakLink, L"&Break Link");
  t.Add(Template::kButton, BS_PUSHBUTTON | WS_TABSTOP, 233, 80, 50, 14, IDCANCEL, L"Cancel");
  return Run(t, owner, LinksProc, st);
}

template <class C, class Ch>
void LinkCallbacks(C* c, LinksState* st, std::wstring (*conv)(Ch*)) {
  st->list = [c, conv]() {
    std::vector<LinkInfo> v;
    for (DWORD id = c->GetNextLink(0); id; id = c->GetNextLink(id)) {
      Ch *name = NULL, *full = NULL, *shortType = NULL;
      ULONG len = 0;
      BOOL avail = FALSE, sel = FALSE;
      LinkInfo li;
      li.id = id;
      if (SUCCEEDED(c->GetLinkSource(id, &name, &len, &full, &shortType, &avail, &sel))) {
        li.name = conv(name);
        li.type = conv(shortType ? shortType : full);
      }
      CoTaskMemFree(name);
      CoTaskMemFree(full);
      CoTaskMemFree(shortType);
      v.push_back(li);
      if (v.size() > 4096) break;
    }
    return v;
  };
  st->update = [c](DWORD id) { c->UpdateLink(id, TRUE, FALSE); };
  st->open = [c](DWORD id) { c->OpenLinkSource(id); };
  st->cancel = [c](DWORD id) { c->CancelLink(id); };
}

std::wstring FromW(wchar_t* s) { return s ? std::wstring(s) : std::wstring(); }
std::wstring FromA(char* s) { return s ? Widen(s) : std::wstring(); }

}  // namespace

extern "C" {

UINT WINAPI OleUIInsertObjectW(LPOLEUIINSERTOBJECTW io) {
  if (!io) return OLEUI_ERR_STRUCTURENULL;
  if (io->cbStruct != sizeof *io) return OLEUI_ERR_CBSTRUCTINCORRECT;
  InsertState st;
  ReadInsert(io, &st);
  if (io->lpszCaption) st.caption = io->lpszCaption;
  if (io->lpszFile) st.file = io->lpszFile;
  UINT r = InsertObject(&st, io->hWndOwner);
  if (r == OLEUI_OK && io->lpszFile && io->cchFile) {
    wcsncpy(io->lpszFile, st.file.c_str(), io->cchFile - 1);
    io->lpszFile[io->cchFile - 1] = 0;
  }
  FinishInsert(io, &st, r);
  return r;
}

UINT WINAPI OleUIInsertObjectA(LPOLEUIINSERTOBJECTA io) {
  if (!io) return OLEUI_ERR_STRUCTURENULL;
  if (io->cbStruct != sizeof *io) return OLEUI_ERR_CBSTRUCTINCORRECT;
  InsertState st;
  ReadInsert(io, &st);
  st.caption = Utf(io->lpszCaption);
  st.file = Utf(io->lpszFile);
  UINT r = InsertObject(&st, io->hWndOwner);
  if (r == OLEUI_OK && io->lpszFile && io->cchFile) {
    std::string f = Narrow(st.file.c_str());
    strncpy(io->lpszFile, f.c_str(), io->cchFile - 1);
    io->lpszFile[io->cchFile - 1] = 0;
  }
  FinishInsert(io, &st, r);
  return r;
}

UINT WINAPI OleUIPasteSpecialW(LPOLEUIPASTESPECIALW ps) {
  if (!ps) return OLEUI_ERR_STRUCTURENULL;
  if (ps->cbStruct != sizeof *ps) return OLEUI_ERR_CBSTRUCTINCORRECT;
  PasteState st;
  ReadPaste(ps, &st, [](LPCWSTR s) { return s ? std::wstring(s) : std::wstring(); });
  if (ps->lpszCaption) st.caption = ps->lpszCaption;
  UINT r = PasteSpecial(&st, ps->hWndOwner);
  FinishPaste(ps, &st, r);
  return r;
}

UINT WINAPI OleUIPasteSpecialA(LPOLEUIPASTESPECIALA ps) {
  if (!ps) return OLEUI_ERR_STRUCTURENULL;
  if (ps->cbStruct != sizeof *ps) return OLEUI_ERR_CBSTRUCTINCORRECT;
  PasteState st;
  ReadPaste(ps, &st, [](LPCSTR s) { return Utf(s); });
  st.caption = Utf(ps->lpszCaption);
  UINT r = PasteSpecial(&st, ps->hWndOwner);
  FinishPaste(ps, &st, r);
  return r;
}

UINT WINAPI OleUIEditLinksW(LPOLEUIEDITLINKSW el) {
  if (!el) return OLEUI_ERR_STRUCTURENULL;
  if (el->cbStruct != sizeof *el) return OLEUI_ERR_CBSTRUCTINCORRECT;
  if (!el->lpOleUILinkContainer) return OLEUI_ERR_STRUCTUREINVALID;
  LinksState st;
  st.hook = el->lpfnHook;
  if (el->lpszCaption) st.caption = el->lpszCaption;
  LinkCallbacks(el->lpOleUILinkContainer, &st, FromW);
  return EditLinks(&st, el->hWndOwner);
}

UINT WINAPI OleUIEditLinksA(LPOLEUIEDITLINKSA el) {
  if (!el) return OLEUI_ERR_STRUCTURENULL;
  if (el->cbStruct != sizeof *el) return OLEUI_ERR_CBSTRUCTINCORRECT;
  if (!el->lpOleUILinkContainer) return OLEUI_ERR_STRUCTUREINVALID;
  LinksState st;
  st.hook = el->lpfnHook;
  st.caption = Utf(el->lpszCaption);
  LinkCallbacks(el->lpOleUILinkContainer, &st, FromA);
  return EditLinks(&st, el->hWndOwner);
}

}  // extern "C"
