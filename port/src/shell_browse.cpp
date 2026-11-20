// shell_browse.cpp - the folder browser, SHBrowseForFolder: a dialog with
// the folders (and, with BIF_BROWSEINCLUDEFILES, files) as a tree below
// the root, read as they are opened. It answers the BFFM_ messages and
// calls the program's callback as Windows does: BFFM_INITIALIZED once it
// is up, BFFM_SELCHANGED as the selection moves, BFFM_VALIDATEFAILED for
// a name typed in its edit box (BIF_EDITBOX) that is not there.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include <windows.h>
#include <commctrl.h>
#include <shlobj.h>

#include "dlgtemplate.h"
#include "shell.h"
#include "winport.h"

#include <errno.h>
#include <string.h>
#include <sys/stat.h>

#include <memory>
#include <string>
#include <vector>

using namespace winport;

namespace {

enum {
  kTree = 14145,
  kTitle = 14146,
  kStatus = 14147,
  kEdit = 14148,
  kEditLabel = 14149,
  kNewFolder = 14150,
};

const WCHAR kStateProp[] = L"winport.browse";

struct Node {
  std::string path;
  bool loaded = false;
};

struct Browse {
  BROWSEINFOW* bi = NULL;
  bool ansi = false;  // SHBrowseForFolderA: the callback takes ANSI text
  std::string root;
  std::vector<std::unique_ptr<Node>> nodes;
  HWND tree = NULL;
  std::string chosen;
};

std::string Leaf(const std::string& path) {
  std::string n = path.substr(path.rfind('/') + 1);
  return n.empty() ? "/" : n;
}

int Callback(HWND h, Browse* b, UINT msg, LPARAM lp) {
  if (!b->bi->lpfn) return 0;
  return b->bi->lpfn(h, msg, lp, b->bi->lParam);
}

Node* NodeOf(Browse* b, HTREEITEM item) {
  if (!item) return NULL;
  TVITEMW it = {};
  it.mask = TVIF_PARAM;
  it.hItem = item;
  if (!SendMessageW(b->tree, TVM_GETITEMW, 0, (LPARAM)&it)) return NULL;
  return (Node*)it.lParam;
}

HTREEITEM Insert(Browse* b, HTREEITEM parent, const std::string& path) {
  b->nodes.push_back(std::unique_ptr<Node>(new Node{path}));
  std::wstring text = Widen(Leaf(path).c_str());
  TVINSERTSTRUCTW ins = {};
  ins.hParent = parent;
  ins.hInsertAfter = TVI_LAST;
  ins.item.mask = TVIF_TEXT | TVIF_PARAM | TVIF_CHILDREN;
  ins.item.pszText = &text[0];
  ins.item.lParam = (LPARAM)b->nodes.back().get();
  ins.item.cChildren = IsDir(path) ? 1 : 0;
  return (HTREEITEM)SendMessageW(b->tree, TVM_INSERTITEMW, 0, (LPARAM)&ins);
}

// Reads a folder's entries into the tree the first time it is opened.
void Load(Browse* b, HTREEITEM item) {
  Node* n = NodeOf(b, item);
  if (!n || n->loaded) return;
  n->loaded = true;
  bool files = (b->bi->ulFlags & BIF_BROWSEINCLUDEFILES) != 0;
  std::vector<std::string> names = ListDir(n->path, true, files, false);
  for (const std::string& name : names) Insert(b, item, n->path == "/" ? "/" + name : n->path + "/" + name);
  if (names.empty()) {
    TVITEMW it = {};
    it.mask = TVIF_CHILDREN;
    it.hItem = item;
    it.cChildren = 0;
    SendMessageW(b->tree, TVM_SETITEMW, 0, (LPARAM)&it);
  }
}

HTREEITEM RootItem(Browse* b) { return (HTREEITEM)SendMessageW(b->tree, TVM_GETNEXTITEM, TVGN_ROOT, 0); }

// The item for a path under the root, reading folders on the way.
HTREEITEM Find(Browse* b, const std::string& path) {
  std::vector<std::string> want = SplitPath(path), root = SplitPath(b->root);
  if (want.size() < root.size() || !std::equal(root.begin(), root.end(), want.begin())) return NULL;
  HTREEITEM cur = RootItem(b);
  for (size_t i = root.size(); cur && i < want.size(); i++) {
    Load(b, cur);
    HTREEITEM c = (HTREEITEM)SendMessageW(b->tree, TVM_GETNEXTITEM, TVGN_CHILD, (LPARAM)cur);
    for (; c; c = (HTREEITEM)SendMessageW(b->tree, TVM_GETNEXTITEM, TVGN_NEXT, (LPARAM)c)) {
      Node* n = NodeOf(b, c);
      if (n && Leaf(n->path) == want[i]) break;
    }
    cur = c;
  }
  return cur;
}

bool Select(Browse* b, const std::string& path, bool expand) {
  HTREEITEM item = Find(b, path);
  if (!item) return false;
  SendMessageW(b->tree, TVM_SELECTITEM, TVGN_CARET, (LPARAM)item);
  SendMessageW(b->tree, TVM_ENSUREVISIBLE, 0, (LPARAM)item);
  if (expand) {
    Load(b, item);
    SendMessageW(b->tree, TVM_EXPAND, TVE_EXPAND, (LPARAM)item);
  }
  return true;
}

// A path from a BFFM_ message: text (ANSI or wide) or an ID list.
std::string PathFrom(WPARAM isText, LPARAM lp, bool wide) {
  if (!lp) return std::string();
  if (!isText) return JoinPath(PidlItems((LPCITEMIDLIST)lp));
  return wide ? AbsolutePath((LPCWSTR)lp) : AbsolutePath(Widen((LPCSTR)lp));
}

std::string Selected(Browse* b) {
  Node* n = NodeOf(b, (HTREEITEM)SendMessageW(b->tree, TVM_GETNEXTITEM, TVGN_CARET, 0));
  return n ? n->path : b->root;
}

void SelChanged(HWND h, Browse* b, Node* n) {
  if (!n) return;
  if (b->bi->ulFlags & BIF_EDITBOX) SetDlgItemTextW(h, kEdit, Widen(Leaf(n->path).c_str()).c_str());
  LPITEMIDLIST pidl = MakePidl(SplitPath(n->path));
  Callback(h, b, BFFM_SELCHANGED, (LPARAM)pidl);
  CoTaskMemFree(pidl);
}

// A new folder in the selected one, selected, with its name to edit.
void NewFolder(HWND h, Browse* b) {
  HTREEITEM parent = (HTREEITEM)SendMessageW(b->tree, TVM_GETNEXTITEM, TVGN_CARET, 0);
  Node* n = NodeOf(b, parent);
  if (!n || !IsDir(n->path)) {
    MessageBeep(MB_ICONEXCLAMATION);
    return;
  }
  Load(b, parent);
  std::string base = n->path == "/" ? "/New Folder" : n->path + "/New Folder", path = base;
  for (int i = 2; mkdir(path.c_str(), 0777) != 0; i++) {
    if (errno != EEXIST || i > 999) {
      MessageBeep(MB_ICONEXCLAMATION);
      return;
    }
    path = base + " (" + std::to_string(i) + ")";
  }
  TVITEMW it = {};
  it.mask = TVIF_CHILDREN;
  it.hItem = parent;
  it.cChildren = 1;
  SendMessageW(b->tree, TVM_SETITEMW, 0, (LPARAM)&it);
  HTREEITEM item = Insert(b, parent, path);
  NodeOf(b, item)->loaded = true;
  SendMessageW(b->tree, TVM_EXPAND, TVE_EXPAND, (LPARAM)parent);
  SendMessageW(b->tree, TVM_SELECTITEM, TVGN_CARET, (LPARAM)item);
  SetFocus(b->tree);
  SendMessageW(b->tree, TVM_EDITLABELW, 0, (LPARAM)item);
}

// A label edited in the tree renames the file or folder.
BOOL EndLabelEdit(Browse* b, HTREEITEM item, const std::wstring& text) {
  Node* n = NodeOf(b, item);
  if (!n || n->path == b->root || text.empty() || text.find_first_of(L"/\\") != std::wstring::npos) return FALSE;
  std::string to = n->path.substr(0, n->path.rfind('/') + 1) + Narrow(text.c_str());
  if (to == n->path) return TRUE;
  if (rename(n->path.c_str(), to.c_str()) != 0) {
    MessageBeep(MB_ICONEXCLAMATION);
    return FALSE;
  }
  // Paths below it change too.
  for (auto& other : b->nodes) {
    if (other->path == n->path || other->path.compare(0, n->path.size() + 1, n->path + "/") == 0)
      other->path = to + other->path.substr(n->path.size());
  }
  return TRUE;
}

// OK: the selection, or the name typed in the edit box, from the selected
// folder. A name that is not there goes to the callback to judge.
void Ok(HWND h, Browse* b) {
  std::string sel = Selected(b);
  if (b->bi->ulFlags & BIF_EDITBOX) {
    HWND e = GetDlgItem(h, kEdit);
    int len = GetWindowTextLengthW(e);
    std::wstring text((size_t)len + 1, L'\0');
    GetWindowTextW(e, &text[0], len + 1);
    text.resize((size_t)len);
    if (!text.empty() && Narrow(text.c_str()) != Leaf(sel)) {
      std::string p = PosixPath(text.c_str());
      std::string dir = IsDir(sel) ? sel : sel.substr(0, sel.rfind('/'));
      std::string full = !p.empty() && p[0] == '/' ? JoinPath(SplitPath(p)) : AbsolutePath(Widen((dir + "/" + p).c_str()));
      struct stat st;
      if (stat(full.c_str(), &st) != 0) {
        if (!(b->bi->ulFlags & BIF_VALIDATE)) {
          MessageBeep(MB_ICONEXCLAMATION);
          return;
        }
        int keep;
        if (b->ansi) {
          std::string a = Narrow(text.c_str());
          keep = Callback(h, b, BFFM_VALIDATEFAILEDA, (LPARAM)a.c_str());
        } else {
          keep = Callback(h, b, BFFM_VALIDATEFAILEDW, (LPARAM)text.c_str());
        }
        if (!keep) EndDialog(h, IDCANCEL);
        return;
      }
      sel = full;
    }
  }
  b->chosen = sel;
  EndDialog(h, IDOK);
}

INT_PTR Answer(HWND h, LRESULT r) {
  SetWindowLongPtrW(h, DWLP_MSGRESULT, r);
  return TRUE;
}

INT_PTR CALLBACK BrowseProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
  Browse* b;
  if (msg == WM_INITDIALOG) {
    b = (Browse*)lp;
    SetPropW(h, kStateProp, (HANDLE)b);
    b->tree = GetDlgItem(h, kTree);
    if (b->bi->lpszTitle) SetDlgItemTextW(h, kTitle, b->bi->lpszTitle);
    HTREEITEM root = Insert(b, TVI_ROOT, b->root);
    Load(b, root);
    SendMessageW(b->tree, TVM_EXPAND, TVE_EXPAND, (LPARAM)root);
    SendMessageW(b->tree, TVM_SELECTITEM, TVGN_CARET, (LPARAM)root);
    Callback(h, b, BFFM_INITIALIZED, 0);
    SetFocus(b->tree);
    return FALSE;
  }
  b = (Browse*)GetPropW(h, kStateProp);
  if (!b) return FALSE;
  switch (msg) {
    case WM_DESTROY:
      RemovePropW(h, kStateProp);
      return FALSE;
    case WM_COMMAND:
      switch (LOWORD(wp)) {
        case IDOK: Ok(h, b); return TRUE;
        case IDCANCEL: EndDialog(h, IDCANCEL); return TRUE;
        case kNewFolder: NewFolder(h, b); return TRUE;
      }
      return FALSE;
    case WM_NOTIFY: {
      NMHDR* nm = (NMHDR*)lp;
      if (nm->idFrom != kTree) return FALSE;
      NMTREEVIEWW* tv = (NMTREEVIEWW*)lp;
      switch (nm->code) {
        case TVN_ITEMEXPANDINGW:
        case TVN_ITEMEXPANDINGA:
          if (tv->action & TVE_EXPAND) Load(b, tv->itemNew.hItem);
          return Answer(h, FALSE);
        case TVN_SELCHANGEDW:
        case TVN_SELCHANGEDA:
          SelChanged(h, b, (Node*)tv->itemNew.lParam);
          return TRUE;
        case TVN_BEGINLABELEDITW:
        case TVN_BEGINLABELEDITA: {
          NMTVDISPINFOW* di = (NMTVDISPINFOW*)lp;
          Node* n = NodeOf(b, di->item.hItem);
          return Answer(h, !n || n->path == b->root);  // TRUE cancels
        }
        case TVN_ENDLABELEDITW: {
          NMTVDISPINFOW* di = (NMTVDISPINFOW*)lp;
          if (!di->item.pszText) return Answer(h, FALSE);
          return Answer(h, EndLabelEdit(b, di->item.hItem, di->item.pszText));
        }
        case TVN_ENDLABELEDITA: {
          NMTVDISPINFOA* di = (NMTVDISPINFOA*)lp;
          if (!di->item.pszText) return Answer(h, FALSE);
          return Answer(h, EndLabelEdit(b, di->item.hItem, Widen(di->item.pszText)));
        }
      }
      return FALSE;
    }
    // From the callback.
    case BFFM_SETSELECTIONA:
    case BFFM_SETSELECTIONW:
      return Answer(h, Select(b, PathFrom(wp, lp, msg == BFFM_SETSELECTIONW), false));
    case BFFM_SETEXPANDED:
      return Answer(h, Select(b, PathFrom(wp, lp, true), true));
    case BFFM_ENABLEOK:
      EnableWindow(GetDlgItem(h, IDOK), lp != 0);
      return Answer(h, 0);
    case BFFM_SETSTATUSTEXTA:
      SetDlgItemTextW(h, kStatus, lp ? Widen((LPCSTR)lp).c_str() : L"");
      return Answer(h, 0);
    case BFFM_SETSTATUSTEXTW:
      SetDlgItemTextW(h, kStatus, lp ? (LPCWSTR)lp : L"");
      return Answer(h, 0);
    case BFFM_SETOKTEXT:
      SetDlgItemTextW(h, IDOK, lp ? (LPCWSTR)lp : L"OK");
      return Answer(h, 0);
  }
  return FALSE;
}

LPITEMIDLIST BrowseCore(BROWSEINFOW* bi, bool ansi, std::string* chosen) {
  Browse b;
  b.bi = bi;
  b.ansi = ansi;
  b.root = bi->pidlRoot ? JoinPath(PidlItems(bi->pidlRoot)) : "/";
  if (!IsDir(b.root)) {
    SetLastError(ERROR_PATH_NOT_FOUND);
    return NULL;
  }
  DWORD f = bi->ulFlags;
  bool status = (f & BIF_STATUSTEXT) && !(f & BIF_NEWDIALOGSTYLE);
  bool edit = (f & BIF_EDITBOX) != 0;
  bool newFolder = (f & BIF_NEWDIALOGSTYLE) && !(f & BIF_NONEWFOLDERBUTTON);
  const int w = 212;
  int y = 6;
  Template t(DS_3DLOOK, w, 0, L"Browse For Folder");
  t.Add(Template::kStatic, SS_LEFT | SS_NOPREFIX, 8, y, w - 16, 20, kTitle, L"");
  y += 22;
  if (status) {
    t.Add(Template::kStatic, SS_LEFT | SS_NOPREFIX, 8, y, w - 16, 8, kStatus, L"");
    y += 10;
  }
  t.Add(WC_TREEVIEWW,
        TVS_HASBUTTONS | TVS_HASLINES | TVS_LINESATROOT | TVS_SHOWSELALWAYS | WS_BORDER | WS_TABSTOP |
            (newFolder ? TVS_EDITLABELS : 0),
        8, y, w - 16, 130, kTree, L"");
  y += 134;
  if (edit) {
    t.Add(Template::kStatic, SS_LEFT, 8, y + 2, 30, 8, kEditLabel, L"Fold&er:");
    t.Add(Template::kEdit, ES_AUTOHSCROLL | WS_BORDER | WS_TABSTOP, 40, y, w - 48, 12, kEdit, L"");
    y += 16;
  }
  if (newFolder)
    t.Add(Template::kButton, BS_PUSHBUTTON | WS_TABSTOP, 8, y + 2, 70, 14, kNewFolder, L"&Make New Folder");
  t.Add(Template::kButton, BS_DEFPUSHBUTTON | WS_TABSTOP, w - 108, y + 2, 48, 14, IDOK, L"OK");
  t.Add(Template::kButton, BS_PUSHBUTTON | WS_TABSTOP, w - 56, y + 2, 48, 14, IDCANCEL, L"Cancel");
  y += 22;
  t.SetHeight(y);
  INT_PTR r = DialogBoxIndirectParamW(NULL, t.Get(), bi->hwndOwner, BrowseProc, (LPARAM)&b);
  if (r != IDOK) return NULL;
  *chosen = b.chosen;
  bi->iImage = 0;
  return MakePidl(SplitPath(b.chosen));
}

}  // namespace

extern "C" {

PIDLIST_ABSOLUTE WINAPI SHBrowseForFolderW(LPBROWSEINFOW bi) {
  if (!bi) return NULL;
  std::string chosen;
  LPITEMIDLIST pidl = BrowseCore(bi, false, &chosen);
  if (pidl && bi->pszDisplayName) {
    std::wstring n = Widen(Leaf(chosen).c_str());
    lstrcpynW(bi->pszDisplayName, n.c_str(), MAX_PATH);
  }
  return pidl;
}

PIDLIST_ABSOLUTE WINAPI SHBrowseForFolderA(LPBROWSEINFOA a) {
  if (!a) return NULL;
  BROWSEINFOW w;
  memcpy(&w, a, sizeof w);  // the same layout, with wide text
  std::wstring title = a->lpszTitle ? Widen(a->lpszTitle) : std::wstring();
  w.lpszTitle = a->lpszTitle ? title.c_str() : NULL;
  w.pszDisplayName = NULL;
  std::string chosen;
  LPITEMIDLIST pidl = BrowseCore(&w, true, &chosen);
  a->iImage = w.iImage;
  if (pidl && a->pszDisplayName) lstrcpynA(a->pszDisplayName, Leaf(chosen).c_str(), MAX_PATH);
  return pidl;
}

}  // extern "C"
