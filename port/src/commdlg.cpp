// commdlg.cpp - the common dialogs for files, colors and fonts, and the
// modeless Find and Replace dialogs. They are the layer's own dialogs,
// built from templates in memory, with the control identifiers of
// <dlgs.h> so that hooks and custom templates find what they expect.
//
// The file dialogs show the folders and files of the file system with
// POSIX paths, and return POSIX paths.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include "dlgtemplate.h"

#include <commdlg.h>
#include <dlgs.h>
#include <dirent.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <wchar.h>
#include <wctype.h>

#include <algorithm>
#include <string>
#include <vector>

using namespace winport;
using winport::Template;

namespace {

const wchar_t kStateProp[] = L"WinportCommonDialog";

// Identifiers the dialogs use for their own controls, outside <dlgs.h>'s.
const int kUp = 0x0500;
const int kNoId = -1;
// The color dialog's controls, by the numbers its hooks know.
const int kColorHue = 703, kColorSat = 704, kColorLum = 705;
const int kColorRed = 706, kColorGreen = 707, kColorBlue = 708;
const int kColorAdd = 712, kColorMix = 719;

void Label(Template& t, int x, int y, int cx, int id, const wchar_t* text) {
  t.Add(Template::kStatic, SS_LEFT, x, y, cx, 8, id, text);
}
void Push(Template& t, int x, int y, int cx, int id, const wchar_t* text, bool def = false) {
  t.Add(Template::kButton, (def ? BS_DEFPUSHBUTTON : BS_PUSHBUTTON) | WS_TABSTOP, x, y, cx, 14, id, text);
}

std::wstring Text(HWND h, int id) {
  HWND c = GetDlgItem(h, id);
  if (!c) return std::wstring();
  int n = GetWindowTextLengthW(c);
  std::wstring s(n + 1, 0);
  GetWindowTextW(c, &s[0], n + 1);
  s.resize(wcslen(s.c_str()));
  return s;
}

std::wstring Trim(const std::wstring& s) {
  size_t a = s.find_first_not_of(L" \t"), b = s.find_last_not_of(L" \t");
  return a == std::wstring::npos ? std::wstring() : s.substr(a, b - a + 1);
}

// A string, or a NUL-separated list, in the caller's character set.
std::string NarrowAll(const std::wstring& s) { return Narrow(s.data(), (int)s.size()); }

// A list of strings that ends with an empty one, widened.
std::wstring WidenList(const char* s) {
  std::wstring out;
  if (!s) return out;
  while (*s) {
    size_t n = strlen(s);
    out += Widen(s, (int)n);
    out.push_back(0);
    s += n + 1;
  }
  out.push_back(0);
  return out;
}

// A resource name, widened unless it is a number.
struct WideName {
  explicit WideName(const char* s) {
    if (s == NULL || IS_INTRESOURCE(s)) {
      p = (LPCWSTR)s;
    } else {
      w = Widen(s);
      p = w.c_str();
    }
  }
  std::wstring w;
  LPCWSTR p;
};

// Copies s and its NUL into a buffer of n characters of either set,
// cut short when it does not fit. Returns the characters s needs with
// its NUL.
size_t PutString(const std::wstring& s, void* buf, size_t n, bool ansi) {
  if (ansi) {
    std::string a = Narrow(s.c_str());
    if (buf && n > 0) {
      size_t k = std::min(a.size(), n - 1);
      memcpy(buf, a.data(), k);
      ((char*)buf)[k] = 0;
    }
    return a.size() + 1;
  }
  if (buf && n > 0) {
    size_t k = std::min(s.size(), n - 1);
    memcpy(buf, s.data(), k * sizeof(wchar_t));
    ((wchar_t*)buf)[k] = 0;
  }
  return s.size() + 1;
}

void SetText(HWND h, int id, const void* text, bool ansi) {
  if (ansi)
    SetDlgItemTextW(h, id, Widen((const char*)text).c_str());
  else
    SetDlgItemTextW(h, id, (const wchar_t*)text);
}

// The dialog template a hook names, from a module or in memory.
LPCDLGTEMPLATEW CustomTemplate(HINSTANCE inst, LPCWSTR name, bool handle) {
  if (handle) return (LPCDLGTEMPLATEW)inst;
  HRSRC r = FindResourceW(inst, name, (LPCWSTR)RT_DIALOG);
  if (!r) return NULL;
  return (LPCDLGTEMPLATEW)LockResource(LoadResource(inst, r));
}

// ---------------------------------------------------------------------
// Paths

std::wstring Join(const std::wstring& dir, const std::wstring& name) {
  if (dir.empty() || dir == L"/") return L"/" + name;
  return dir + L"/" + name;
}

// An absolute path without ".", ".." or doubled separators.
std::wstring Normalize(const std::wstring& p) {
  std::vector<std::wstring> parts;
  size_t i = 0;
  while (i <= p.size()) {
    size_t j = p.find(L'/', i);
    if (j == std::wstring::npos) j = p.size();
    std::wstring part = p.substr(i, j - i);
    i = j + 1;
    if (part.empty() || part == L".") continue;
    if (part == L"..") {
      if (!parts.empty()) parts.pop_back();
      continue;
    }
    parts.push_back(part);
  }
  std::wstring out;
  for (const std::wstring& part : parts) out += L"/" + part;
  return out.empty() ? L"/" : out;
}

std::wstring Parent(const std::wstring& p) {
  size_t i = p.rfind(L'/');
  if (i == std::wstring::npos || i == 0) return L"/";
  return p.substr(0, i);
}

std::wstring Leaf(const std::wstring& p) {
  size_t i = p.rfind(L'/');
  return i == std::wstring::npos ? p : p.substr(i + 1);
}

// 0 when nothing is there, 1 for a file, 2 for a folder.
int Kind(const std::wstring& p) {
  struct stat s;
  if (stat(Narrow(p.c_str()).c_str(), &s) != 0) return 0;
  return S_ISDIR(s.st_mode) ? 2 : 1;
}

std::wstring CurrentDir() {
  char buf[4096];
  if (getcwd(buf, sizeof buf) == NULL) return L"/";
  return Widen(buf);
}

// A name typed in a folder, as an absolute path: absolute itself, from
// the home folder (~/), with the program's own separators, or relative.
std::wstring Resolve(const std::wstring& dir, std::wstring text) {
  if (text.size() >= 2 && text[1] == L':') return Normalize(Widen(PosixPath(text.c_str()).c_str()));
  std::replace(text.begin(), text.end(), L'\\', L'/');
  if (!text.empty() && text[0] == L'/') return Normalize(text);
  if (text.compare(0, 2, L"~/") == 0 || text == L"~") {
    const char* home = getenv("HOME");
    return Normalize(Widen(home ? home : "/") + L"/" + text.substr(1));
  }
  return Normalize(Join(dir, text));
}

// Whether a name matches a pattern with * and ?, ignoring case the way
// the patterns of filters do.
bool Match(const wchar_t* p, const wchar_t* s) {
  for (; *p; p++, s++) {
    if (*p == L'*') {
      while (p[1] == L'*') p++;
      if (!p[1]) return true;
      for (; *s; s++)
        if (Match(p + 1, s)) return true;
      return false;
    }
    if (!*s) return false;
    if (*p != L'?' && towlower(*p) != towlower(*s)) return false;
  }
  return !*s;
}

// A filter's patterns, separated by semicolons. "*.*" is every name,
// with an extension or without.
bool MatchSpec(const std::wstring& spec, const std::wstring& name) {
  size_t i = 0;
  while (i <= spec.size()) {
    size_t j = spec.find(L';', i);
    if (j == std::wstring::npos) j = spec.size();
    std::wstring p = Trim(spec.substr(i, j - i));
    i = j + 1;
    if (p.empty()) continue;
    if (p == L"*.*" || Match(p.c_str(), name.c_str())) return true;
  }
  return false;
}

bool HasWildcard(const std::wstring& s) { return s.find_first_of(L"*?") != std::wstring::npos; }

bool LessNoCase(const std::wstring& a, const std::wstring& b) {
  size_t n = std::min(a.size(), b.size());
  for (size_t i = 0; i < n; i++) {
    wint_t x = towlower(a[i]), y = towlower(b[i]);
    if (x != y) return x < y;
  }
  return a.size() < b.size();
}

// ---------------------------------------------------------------------
// Open and Save As

struct FileDlg {
  OPENFILENAMEW* ofn;     // the W form; an A call's copy
  OPENFILENAMEA* ansi;    // an A call's own, or NULL
  bool save;
  std::wstring dir;       // the folder shown
  std::wstring name;      // the name to start with
  std::vector<std::wstring> filterNames, filterSpecs;
  std::wstring spec;      // the patterns the list shows now
  std::wstring defExt;
  HWND child = NULL;      // the hook's or template's dialog (Explorer style)
  UINT fileOkMsg = 0;
};

LPOFNHOOKPROC FileHook(FileDlg* st) {
  return (st->ofn->Flags & OFN_ENABLEHOOK) ? st->ofn->lpfnHook : NULL;
}
LPARAM FileParam(FileDlg* st) { return st->ansi ? (LPARAM)st->ansi : (LPARAM)st->ofn; }
bool Explorer(FileDlg* st) { return (st->ofn->Flags & OFN_EXPLORER) != 0; }

// What an A caller sees of the structure while the dialog runs.
void SyncAnsi(FileDlg* st) {
  if (!st->ansi) return;
  st->ansi->Flags = st->ofn->Flags;
  st->ansi->nFilterIndex = st->ofn->nFilterIndex;
}

// Tells the Explorer-style hook what happened; returns its answer.
LRESULT Notify(FileDlg* st, UINT code, const std::wstring& file = std::wstring()) {
  if (!st->child) return 0;
  HWND parent = GetParent(st->child);
  if (st->ansi) {
    SyncAnsi(st);
    std::string f = Narrow(file.c_str());
    OFNOTIFYA n;
    n.hdr.hwndFrom = parent;
    n.hdr.idFrom = 0;
    n.hdr.code = code;
    n.lpOFN = st->ansi;
    n.pszFile = file.empty() ? NULL : &f[0];
    return SendMessageW(st->child, WM_NOTIFY, 0, (LPARAM)&n);
  }
  std::wstring f = file;
  OFNOTIFYW n;
  n.hdr.hwndFrom = parent;
  n.hdr.idFrom = 0;
  n.hdr.code = code;
  n.lpOFN = st->ofn;
  n.pszFile = file.empty() ? NULL : &f[0];
  return SendMessageW(st->child, WM_NOTIFY, 0, (LPARAM)&n);
}

// The list's item data tells folders from files.
enum { kFile = 0, kFolder = 1 };

void FillFiles(HWND h, FileDlg* st) {
  HWND lb = GetDlgItem(h, lst1);
  SendMessageW(lb, LB_RESETCONTENT, 0, 0);
  std::vector<std::wstring> dirs, files;
  if (DIR* d = opendir(Narrow(st->dir.c_str()).c_str())) {
    bool hidden = (st->ofn->Flags & OFN_FORCESHOWHIDDEN) != 0;
    while (struct dirent* e = readdir(d)) {
      if (e->d_name[0] == '.' && (!hidden || !e->d_name[1] || (e->d_name[1] == '.' && !e->d_name[2])))
        continue;
      std::wstring name = Widen(e->d_name);
      if (Kind(Join(st->dir, name)) == 2)
        dirs.push_back(name);
      else if (MatchSpec(st->spec, name))
        files.push_back(name);
    }
    closedir(d);
  }
  std::sort(dirs.begin(), dirs.end(), LessNoCase);
  std::sort(files.begin(), files.end(), LessNoCase);
  auto add = [&](const std::wstring& text, LPARAM kind) {
    LRESULT i = SendMessageW(lb, LB_ADDSTRING, 0, (LPARAM)text.c_str());
    if (i >= 0) SendMessageW(lb, LB_SETITEMDATA, (WPARAM)i, kind);
  };
  if (st->dir != L"/") add(L"../", kFolder);
  for (const std::wstring& d : dirs) add(d + L"/", kFolder);
  for (const std::wstring& f : files) add(f, kFile);

  // The folder and the folders it is in, the outermost first.
  HWND cb = GetDlgItem(h, cmb2);
  SendMessageW(cb, CB_RESETCONTENT, 0, 0);
  std::vector<std::wstring> chain;
  for (std::wstring p = st->dir;; p = Parent(p)) {
    chain.push_back(p);
    if (p == L"/") break;
  }
  for (size_t i = chain.size(); i-- > 0;) SendMessageW(cb, CB_ADDSTRING, 0, (LPARAM)chain[i].c_str());
  SendMessageW(cb, CB_SETCURSEL, chain.size() - 1, 0);
  EnableWindow(GetDlgItem(h, kUp), st->dir != L"/");
}

void Navigate(HWND h, FileDlg* st, const std::wstring& dir) {
  st->dir = dir;
  FillFiles(h, st);
  Notify(st, CDN_FOLDERCHANGE);
}

std::wstring ItemText(HWND lb, int i) {
  LRESULT n = SendMessageW(lb, LB_GETTEXTLEN, i, 0);
  if (n < 0) return std::wstring();
  std::wstring s(n + 1, 0);
  SendMessageW(lb, LB_GETTEXT, i, (LPARAM)&s[0]);
  s.resize(wcslen(s.c_str()));
  if (!s.empty() && s.back() == L'/') s.pop_back();
  return s;
}

std::vector<int> Selection(HWND lb, bool multi) {
  std::vector<int> out;
  if (multi) {
    LRESULT n = SendMessageW(lb, LB_GETSELCOUNT, 0, 0);
    if (n > 0) {
      out.resize(n);
      n = SendMessageW(lb, LB_GETSELITEMS, n, (LPARAM)out.data());
      out.resize(n > 0 ? n : 0);
    }
  } else {
    LRESULT i = SendMessageW(lb, LB_GETCURSEL, 0, 0);
    if (i >= 0) out.push_back((int)i);
  }
  return out;
}

bool Multi(FileDlg* st) { return (st->ofn->Flags & OFN_ALLOWMULTISELECT) != 0; }

// The files selected in the list go in the name box, quoted when more
// than one.
void OnSelChange(HWND h, FileDlg* st) {
  HWND lb = GetDlgItem(h, lst1);
  std::vector<std::wstring> names;
  for (int i : Selection(lb, Multi(st)))
    if (SendMessageW(lb, LB_GETITEMDATA, i, 0) == kFile) names.push_back(ItemText(lb, i));
  if (names.empty()) return;
  std::wstring text;
  if (names.size() == 1) {
    text = names[0];
  } else {
    for (const std::wstring& n : names) text += (text.empty() ? L"\"" : L" \"") + n + L"\"";
  }
  SetDlgItemTextW(h, edt1, text.c_str());
  Notify(st, CDN_SELCHANGE);
}

// The extension a name without one gets: the filter's own, or the
// program's default.
std::wstring WithDefExt(FileDlg* st, const std::wstring& path) {
  if (st->defExt.empty() && !(st->ofn->lpstrDefExt)) return path;
  std::wstring leaf = Leaf(path);
  if (leaf.find(L'.') != std::wstring::npos) return path;
  std::wstring ext = st->defExt;
  const std::wstring& s = st->spec;
  if (s.compare(0, 2, L"*.") == 0 && s.find(L';') == std::wstring::npos && !HasWildcard(s.substr(2)))
    ext = s.substr(2);
  if (ext.empty()) return path;
  if (st->save || Kind(path) == 0) return path + L"." + ext;
  return path;
}

int Ask(HWND h, FileDlg* st, const std::wstring& text, UINT type) {
  const wchar_t* title = st->save ? L"Save As" : L"Open";
  if (st->ofn->lpstrTitle) title = st->ofn->lpstrTitle;
  return MessageBoxW(h, text.c_str(), title, type);
}

// The checks the flags ask for, before a path is the answer.
bool CheckPath(HWND h, FileDlg* st, const std::wstring& path) {
  DWORD f = st->ofn->Flags;
  int kind = Kind(path);
  if ((f & (OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST)) && Kind(Parent(path)) != 2) {
    Ask(h, st, path + L"\nThe folder does not exist. Check the path and try again.", MB_OK | MB_ICONEXCLAMATION);
    return false;
  }
  if (!st->save && (f & OFN_FILEMUSTEXIST) && kind != 1) {
    Ask(h, st, path + L"\nFile not found. Check the file name and try again.", MB_OK | MB_ICONEXCLAMATION);
    return false;
  }
  if (st->save && (f & OFN_OVERWRITEPROMPT) && kind == 1 &&
      Ask(h, st, path + L" already exists.\nDo you want to replace it?", MB_YESNO | MB_ICONEXCLAMATION) != IDYES)
    return false;
  if ((f & OFN_CREATEPROMPT) && kind == 0 &&
      Ask(h, st, path + L"\nThis file does not exist. Do you want to create it?", MB_YESNO | MB_ICONQUESTION) != IDYES)
    return false;
  return true;
}

// Puts the answer in the caller's buffers: the path, or for several
// files the folder and then each name. Fails when lpstrFile is short,
// with the size it needs in its first two bytes.
bool WriteResult(FileDlg* st, const std::wstring& dir, const std::vector<std::wstring>& names) {
  std::wstring all;
  size_t offset, ext = 0;
  if (names.size() == 1) {
    all = Join(dir, names[0]);
    offset = all.size() - names[0].size();
    size_t dot = names[0].rfind(L'.');
    if (dot != std::wstring::npos) ext = offset + dot + 1;
  } else {
    all = dir;
    all.push_back(0);
    offset = all.size();
    for (const std::wstring& n : names) {
      all += n;
      all.push_back(0);
    }
  }
  OPENFILENAMEW* w = st->ofn;
  if (st->ansi) {
    OPENFILENAMEA* a = st->ansi;
    std::string s = NarrowAll(all);
    size_t need = s.size() + 1;
    if (need > a->nMaxFile) {
      if (a->nMaxFile >= 2) *(WORD*)a->lpstrFile = (WORD)std::min<size_t>(need, 0xFFFF);
      return false;
    }
    memcpy(a->lpstrFile, s.data(), s.size());
    a->lpstrFile[s.size()] = 0;
    a->nFileOffset = (WORD)NarrowAll(all.substr(0, offset)).size();
    a->nFileExtension = ext ? (WORD)NarrowAll(all.substr(0, ext)).size() : 0;
    if (a->lpstrFileTitle && a->nMaxFileTitle && names.size() == 1)
      PutString(names[0], a->lpstrFileTitle, a->nMaxFileTitle, true);
  } else {
    size_t need = all.size() + 1;
    if (need > w->nMaxFile) {
      if (w->nMaxFile >= 1) *(WORD*)w->lpstrFile = (WORD)std::min<size_t>(need, 0xFFFF);
      return false;
    }
    memcpy(w->lpstrFile, all.data(), all.size() * sizeof(wchar_t));
    w->lpstrFile[all.size()] = 0;
    w->nFileOffset = (WORD)offset;
    w->nFileExtension = (WORD)ext;
    if (w->lpstrFileTitle && w->nMaxFileTitle && names.size() == 1)
      PutString(names[0], w->lpstrFileTitle, w->nMaxFileTitle, false);
  }
  return true;
}

void Finish(HWND h, FileDlg* st, const std::wstring& dir, const std::vector<std::wstring>& names) {
  OPENFILENAMEW* ofn = st->ofn;
  if (!WriteResult(st, dir, names)) {
    tlsDlgError = FNERR_BUFFERTOOSMALL;
    EndDialog(h, IDCANCEL);
    return;
  }
  ofn->Flags &= ~(OFN_READONLY | OFN_EXTENSIONDIFFERENT);
  if (IsDlgButtonChecked(h, chx1) == BST_CHECKED) ofn->Flags |= OFN_READONLY;
  if (ofn->lpstrDefExt && names.size() == 1) {
    size_t dot = names[0].rfind(L'.');
    std::wstring ext = dot == std::wstring::npos ? std::wstring() : names[0].substr(dot + 1);
    if (wcscasecmp(ext.c_str(), ofn->lpstrDefExt) != 0) ofn->Flags |= OFN_EXTENSIONDIFFERENT;
  }
  SyncAnsi(st);
  // The hook may turn the answer down.
  if (st->child) {
    if (Notify(st, CDN_FILEOK)) return;
  } else if (LPOFNHOOKPROC hook = FileHook(st)) {
    if (hook(h, st->fileOkMsg, 0, FileParam(st))) return;
  }
  if (!(ofn->Flags & OFN_NOCHANGEDIR)) {
    if (chdir(Narrow(dir.c_str()).c_str()) != 0) {
      // The folder stays as it was.
    }
  }
  EndDialog(h, IDOK);
}

void OnFileOk(HWND h, FileDlg* st) {
  HWND lb = GetDlgItem(h, lst1);
  std::wstring text = Trim(Text(h, edt1));
  if (text.empty()) {
    // A folder chosen in the list opens.
    std::vector<int> sel = Selection(lb, Multi(st));
    if (sel.size() == 1 && SendMessageW(lb, LB_GETITEMDATA, sel[0], 0) == kFolder)
      Navigate(h, st, Normalize(Join(st->dir, ItemText(lb, sel[0]))));
    return;
  }
  std::vector<std::wstring> names;
  if (Multi(st) && text[0] == L'"') {
    size_t i = 0;
    while ((i = text.find(L'"', i)) != std::wstring::npos) {
      size_t j = text.find(L'"', i + 1);
      if (j == std::wstring::npos) j = text.size();
      std::wstring n = text.substr(i + 1, j - i - 1);
      if (!n.empty()) names.push_back(n);
      i = j + 1;
    }
  } else {
    names.push_back(text);
  }
  if (names.empty()) return;
  if (names.size() == 1) {
    std::wstring t = names[0];
    std::wstring path = Resolve(st->dir, t);
    if (HasWildcard(t)) {
      // A pattern filters the list, in the folder it names.
      std::wstring folder = Parent(path);
      st->spec = Leaf(path);
      if (folder != st->dir && Kind(folder) == 2) st->dir = folder;
      Navigate(h, st, st->dir);
      SetDlgItemTextW(h, edt1, st->spec.c_str());
      return;
    }
    if (Kind(path) == 2) {
      Navigate(h, st, path);
      SetDlgItemTextW(h, edt1, L"");
      return;
    }
    path = WithDefExt(st, path);
    if (!CheckPath(h, st, path)) return;
    Finish(h, st, Parent(path), std::vector<std::wstring>(1, Leaf(path)));
    return;
  }
  std::vector<std::wstring> leaves;
  for (const std::wstring& n : names) {
    std::wstring path = WithDefExt(st, Resolve(st->dir, n));
    if (!CheckPath(h, st, path)) return;
    leaves.push_back(Leaf(path));
  }
  Finish(h, st, st->dir, leaves);
}

// The path the dialog would answer now.
std::wstring CurrentPath(HWND h, FileDlg* st) {
  std::wstring text = Trim(Text(h, edt1));
  if (text.empty() || text[0] == L'"') return st->dir;
  return WithDefExt(st, Resolve(st->dir, text));
}

INT_PTR CALLBACK FileChildProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
  FileDlg* st;
  if (msg == WM_INITDIALOG) {
    st = (FileDlg*)lp;
    SetPropW(h, kStateProp, (HANDLE)st);
    st->child = h;
    if (LPOFNHOOKPROC hook = FileHook(st)) return (INT_PTR)hook(h, msg, wp, FileParam(st));
    return TRUE;
  }
  st = (FileDlg*)GetPropW(h, kStateProp);
  if (!st) return FALSE;
  if (msg == WM_DESTROY) RemovePropW(h, kStateProp);
  if (LPOFNHOOKPROC hook = FileHook(st)) return (INT_PTR)hook(h, msg, wp, lp);
  return FALSE;
}

// The Explorer-style hook or template gets a dialog of its own inside
// this one, below the standard controls.
void CreateChild(HWND h, FileDlg* st) {
  OPENFILENAMEW* ofn = st->ofn;
  LPCDLGTEMPLATEW tmpl = NULL;
  if (ofn->Flags & (OFN_ENABLETEMPLATE | OFN_ENABLETEMPLATEHANDLE))
    tmpl = CustomTemplate(ofn->hInstance, ofn->lpTemplateName, (ofn->Flags & OFN_ENABLETEMPLATEHANDLE) != 0);
  static const WORD kEmpty[] = {
      (WORD)(WS_CHILD | DS_CONTROL), (WORD)((WS_CHILD | DS_CONTROL) >> 16), 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
  if (!tmpl) {
    if (!FileHook(st)) return;
    tmpl = (LPCDLGTEMPLATEW)kEmpty;
  }
  HWND child = CreateDialogIndirectParamW(ofn->hInstance, tmpl, h, FileChildProc, (LPARAM)st);
  if (!child) return;
  RECT pc, cr;
  GetClientRect(h, &pc);
  GetWindowRect(child, &cr);
  int cw = cr.right - cr.left, ch = cr.bottom - cr.top;
  int wider = std::max(0, cw - (int)pc.right);
  SetWindowPos(child, NULL, 0, pc.bottom, std::max(cw, (int)pc.right), ch, SWP_NOZORDER | SWP_NOACTIVATE);
  if (ch > 0 || wider > 0) {
    RECT wr;
    GetWindowRect(h, &wr);
    SetWindowPos(h, NULL, 0, 0, wr.right - wr.left + wider, wr.bottom - wr.top + ch,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
  }
  ShowWindow(child, SW_SHOWNA);
}

INT_PTR CALLBACK FileProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
  FileDlg* st;
  if (msg == WM_INITDIALOG) {
    st = (FileDlg*)lp;
    SetPropW(h, kStateProp, (HANDLE)st);
    OPENFILENAMEW* ofn = st->ofn;
    HWND cb = GetDlgItem(h, cmb1);
    for (const std::wstring& n : st->filterNames) SendMessageW(cb, CB_ADDSTRING, 0, (LPARAM)n.c_str());
    if (!st->filterNames.empty()) {
      SendMessageW(cb, CB_SETCURSEL, ofn->nFilterIndex - 1, 0);
    } else {
      EnableWindow(cb, FALSE);
    }
    SetDlgItemTextW(h, edt1, st->name.c_str());
    if (st->save || (ofn->Flags & OFN_HIDEREADONLY)) ShowWindow(GetDlgItem(h, chx1), SW_HIDE);
    CheckDlgButton(h, chx1, (ofn->Flags & OFN_READONLY) ? BST_CHECKED : BST_UNCHECKED);
    if (!(ofn->Flags & OFN_SHOWHELP)) ShowWindow(GetDlgItem(h, pshHelp), SW_HIDE);
    FillFiles(h, st);
    if (Explorer(st)) {
      CreateChild(h, st);
    } else if (LPOFNHOOKPROC hook = FileHook(st)) {
      if (!hook(h, msg, wp, FileParam(st))) return FALSE;
    }
    Notify(st, CDN_INITDONE);
    Notify(st, CDN_FOLDERCHANGE);
    return TRUE;
  }
  st = (FileDlg*)GetPropW(h, kStateProp);
  if (!st) return FALSE;
  if (!Explorer(st))
    if (LPOFNHOOKPROC hook = FileHook(st))
      if (hook(h, msg, wp, lp)) return TRUE;
  switch (msg) {
    case WM_DESTROY:
      RemovePropW(h, kStateProp);
      return FALSE;
    case CDM_GETSPEC:
    case CDM_GETFILEPATH:
    case CDM_GETFOLDERPATH: {
      std::wstring s = msg == CDM_GETSPEC ? Trim(Text(h, edt1)) : msg == CDM_GETFOLDERPATH ? st->dir : CurrentPath(h, st);
      SetWindowLongPtrW(h, DWLP_MSGRESULT, (LONG_PTR)PutString(s, (void*)lp, (size_t)wp, st->ansi != NULL));
      return TRUE;
    }
    case CDM_SETCONTROLTEXT: {
      int id = (int)wp == cmb13 ? edt1 : (int)wp;
      SetText(h, id, (const void*)lp, st->ansi != NULL);
      SetWindowLongPtrW(h, DWLP_MSGRESULT, 0);
      return TRUE;
    }
    case CDM_HIDECONTROL:
      ShowWindow(GetDlgItem(h, (int)wp == cmb13 ? edt1 : (int)wp), SW_HIDE);
      SetWindowLongPtrW(h, DWLP_MSGRESULT, 0);
      return TRUE;
    case CDM_SETDEFEXT:
      st->defExt = !lp ? std::wstring() : st->ansi ? Widen((const char*)lp) : std::wstring((const wchar_t*)lp);
      SetWindowLongPtrW(h, DWLP_MSGRESULT, 0);
      return TRUE;
    case WM_COMMAND: {
      int id = LOWORD(wp), code = HIWORD(wp);
      switch (id) {
        case IDOK:
          OnFileOk(h, st);
          return TRUE;
        case IDCANCEL:
          EndDialog(h, IDCANCEL);
          return TRUE;
        case kUp:
          Navigate(h, st, Parent(st->dir));
          return TRUE;
        case pshHelp:
          if (HWND owner = st->ofn->hwndOwner)
            SendMessageW(owner, RegisterWindowMessageW(HELPMSGSTRINGW), (WPARAM)h, FileParam(st));
          return TRUE;
        case cmb1:
          if (code == CBN_SELCHANGE) {
            LRESULT i = SendDlgItemMessageW(h, cmb1, CB_GETCURSEL, 0, 0);
            if (i >= 0 && (size_t)i < st->filterSpecs.size()) {
              st->ofn->nFilterIndex = (DWORD)i + 1;
              st->spec = st->filterSpecs[i];
              FillFiles(h, st);
              Notify(st, CDN_TYPECHANGE);
            }
          }
          return TRUE;
        case cmb2:
          if (code == CBN_SELCHANGE) {
            LRESULT i = SendDlgItemMessageW(h, cmb2, CB_GETCURSEL, 0, 0);
            if (i >= 0) {
              LRESULT n = SendDlgItemMessageW(h, cmb2, CB_GETLBTEXTLEN, i, 0);
              std::wstring s(n > 0 ? n + 1 : 1, 0);
              SendDlgItemMessageW(h, cmb2, CB_GETLBTEXT, i, (LPARAM)&s[0]);
              s.resize(wcslen(s.c_str()));
              if (!s.empty() && s != st->dir) Navigate(h, st, s);
            }
          }
          return TRUE;
        case lst1:
          if (code == LBN_SELCHANGE) {
            OnSelChange(h, st);
          } else if (code == LBN_DBLCLK) {
            HWND lb = GetDlgItem(h, lst1);
            LRESULT i = SendMessageW(lb, LB_GETCARETINDEX, 0, 0);
            if (i < 0) i = SendMessageW(lb, LB_GETCURSEL, 0, 0);
            if (i < 0) return TRUE;
            if (SendMessageW(lb, LB_GETITEMDATA, i, 0) == kFolder) {
              Navigate(h, st, Normalize(Join(st->dir, ItemText(lb, (int)i))));
            } else {
              SetDlgItemTextW(h, edt1, ItemText(lb, (int)i).c_str());
              OnFileOk(h, st);
            }
          }
          return TRUE;
      }
      return FALSE;
    }
  }
  return FALSE;
}

BOOL FileCore(OPENFILENAMEW* ofn, OPENFILENAMEA* ansi, bool save) {
  tlsDlgError = 0;
  if (ofn == NULL || (ofn->lStructSize != sizeof(OPENFILENAMEW) && ofn->lStructSize != OPENFILENAME_SIZE_VERSION_400W)) {
    tlsDlgError = CDERR_STRUCTSIZE;
    return FALSE;
  }
  if (ofn->lpstrFile == NULL || ofn->nMaxFile == 0) {
    tlsDlgError = CDERR_INITIALIZATION;
    return FALSE;
  }
  if ((ofn->Flags & OFN_ENABLEHOOK) && ofn->lpfnHook == NULL) {
    tlsDlgError = CDERR_NOHOOK;
    return FALSE;
  }
  FileDlg st;
  st.ofn = ofn;
  st.ansi = ansi;
  st.save = save;
  st.fileOkMsg = RegisterWindowMessageW(FILEOKSTRINGW);
  if (const wchar_t* f = ofn->lpstrFilter) {
    while (*f) {
      std::wstring name = f;
      f += name.size() + 1;
      std::wstring spec = f;
      f += spec.size() + (*f ? 1 : 0);
      st.filterNames.push_back(name);
      st.filterSpecs.push_back(spec.empty() ? L"*" : spec);
    }
  }
  if (st.filterSpecs.empty()) {
    st.spec = L"*";
  } else {
    if (ofn->nFilterIndex < 1 || ofn->nFilterIndex > st.filterSpecs.size()) ofn->nFilterIndex = 1;
    st.spec = st.filterSpecs[ofn->nFilterIndex - 1];
  }
  if (ofn->lpstrDefExt) st.defExt = ofn->lpstrDefExt;

  // The folder: the one the name gives, the program's, or the current one.
  st.dir = CurrentDir();
  if (ofn->lpstrInitialDir && *ofn->lpstrInitialDir) {
    std::wstring d = Resolve(st.dir, ofn->lpstrInitialDir);
    if (Kind(d) == 2) st.dir = d;
  }
  std::wstring name = ofn->lpstrFile;
  if (name.find_first_of(L"/\\") != std::wstring::npos || (name.size() >= 2 && name[1] == L':')) {
    std::wstring p = Resolve(st.dir, name);
    if (Kind(p) == 2) {
      st.dir = p;
      name.clear();
    } else {
      if (Kind(Parent(p)) == 2) st.dir = Parent(p);
      name = Leaf(p);
    }
  }
  st.name = name;

  const wchar_t* title = ofn->lpstrTitle ? ofn->lpstrTitle : save ? L"Save As" : L"Open";
  Template t(DS_3DLOOK | DS_CONTEXTHELP, 280, 176, title);
  Label(t, 7, 8, 40, stc4, L"Look &in:");
  t.Add(Template::kCombo, CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, 50, 6, 172, 120, cmb2, L"");
  Push(t, 226, 5, 47, kUp, L"&Up");
  DWORD lbStyle = LBS_NOTIFY | LBS_NOINTEGRALHEIGHT | WS_VSCROLL | WS_BORDER | WS_TABSTOP;
  if (ofn->Flags & OFN_ALLOWMULTISELECT) lbStyle |= LBS_EXTENDEDSEL;
  t.Add(Template::kListBox, lbStyle, 7, 24, 266, 98, lst1, L"");
  Label(t, 7, 130, 50, stc3, L"File &name:");
  t.Add(Template::kEdit, ES_AUTOHSCROLL | WS_BORDER | WS_TABSTOP, 60, 128, 150, 12, edt1, L"");
  Label(t, 7, 146, 50, stc2, save ? L"Save as &type:" : L"Files of &type:");
  t.Add(Template::kCombo, CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, 60, 144, 150, 100, cmb1, L"");
  t.Add(Template::kButton, BS_AUTOCHECKBOX | WS_TABSTOP, 60, 160, 100, 10, chx1, L"Open as &read-only");
  Push(t, 216, 127, 57, IDOK, save ? L"&Save" : L"&Open", true);
  Push(t, 216, 143, 57, IDCANCEL, L"Cancel");
  Push(t, 216, 159, 57, pshHelp, L"&Help");
  INT_PTR r = DialogBoxIndirectParamW(NULL, t.Get(), ofn->hwndOwner, FileProc, (LPARAM)&st);
  if (r < 0) tlsDlgError = CDERR_DIALOGFAILURE;
  return r == IDOK;
}

BOOL FileCoreA(OPENFILENAMEA* a, bool save) {
  if (a == NULL || (a->lStructSize != sizeof(OPENFILENAMEA) && a->lStructSize != OPENFILENAME_SIZE_VERSION_400A)) {
    tlsDlgError = CDERR_STRUCTSIZE;
    return FALSE;
  }
  OPENFILENAMEW w;
  memset(&w, 0, sizeof w);
  w.lStructSize = sizeof w;
  w.hwndOwner = a->hwndOwner;
  w.hInstance = a->hInstance;
  std::wstring filter = WidenList(a->lpstrFilter);
  w.lpstrFilter = a->lpstrFilter ? filter.c_str() : NULL;
  w.nFilterIndex = a->nFilterIndex;
  std::wstring file = a->lpstrFile ? Widen(a->lpstrFile) : std::wstring();
  file.resize(std::max<size_t>(file.size() + 1, a->nMaxFile));
  w.lpstrFile = a->lpstrFile ? &file[0] : NULL;
  w.nMaxFile = a->nMaxFile;
  std::wstring dir = a->lpstrInitialDir ? Widen(a->lpstrInitialDir) : std::wstring();
  w.lpstrInitialDir = a->lpstrInitialDir ? dir.c_str() : NULL;
  std::wstring title = a->lpstrTitle ? Widen(a->lpstrTitle) : std::wstring();
  w.lpstrTitle = a->lpstrTitle ? title.c_str() : NULL;
  w.Flags = a->Flags;
  std::wstring ext = a->lpstrDefExt ? Widen(a->lpstrDefExt) : std::wstring();
  w.lpstrDefExt = a->lpstrDefExt ? ext.c_str() : NULL;
  w.lCustData = a->lCustData;
  w.lpfnHook = a->lpfnHook;
  WideName tmpl((a->Flags & OFN_ENABLETEMPLATE) ? a->lpTemplateName : NULL);
  w.lpTemplateName = tmpl.p;
  BOOL r = FileCore(&w, a, save);
  a->Flags = w.Flags;
  a->nFilterIndex = w.nFilterIndex;
  return r;
}

// ---------------------------------------------------------------------
// Color

// Six rows of eight: hues across, from light to dark, then grays.
COLORREF BasicColor(int i) {
  static const COLORREF kBasic[48] = {
      RGB(255, 128, 128), RGB(255, 255, 128), RGB(128, 255, 128), RGB(0, 255, 128),
      RGB(128, 255, 255), RGB(0, 128, 255), RGB(255, 128, 192), RGB(255, 128, 255),
      RGB(255, 0, 0), RGB(255, 255, 0), RGB(128, 255, 0), RGB(0, 255, 64),
      RGB(0, 255, 255), RGB(0, 128, 192), RGB(128, 128, 192), RGB(255, 0, 255),
      RGB(128, 64, 64), RGB(255, 128, 64), RGB(0, 255, 0), RGB(0, 128, 128),
      RGB(0, 64, 128), RGB(128, 128, 255), RGB(128, 0, 64), RGB(255, 0, 128),
      RGB(128, 0, 0), RGB(255, 128, 0), RGB(0, 128, 0), RGB(0, 128, 64),
      RGB(0, 0, 255), RGB(0, 0, 160), RGB(128, 0, 128), RGB(128, 0, 255),
      RGB(64, 0, 0), RGB(128, 64, 0), RGB(0, 64, 0), RGB(0, 64, 64),
      RGB(0, 0, 128), RGB(0, 0, 64), RGB(64, 0, 64), RGB(64, 0, 128),
      RGB(0, 0, 0), RGB(128, 128, 0), RGB(128, 128, 64), RGB(128, 128, 128),
      RGB(64, 128, 128), RGB(192, 192, 192), RGB(224, 224, 224), RGB(255, 255, 255)};
  return kBasic[i];
}

// Hue, saturation and luminosity on the scale of 0 to 240.
void RgbToHls(COLORREF c, int* hue, int* lum, int* sat) {
  int r = GetRValue(c), g = GetGValue(c), b = GetBValue(c);
  int mx = std::max(r, std::max(g, b)), mn = std::min(r, std::min(g, b));
  int sum = mx + mn, d = mx - mn;
  *lum = (sum * 240 + 255) / 510;
  if (d == 0) {
    *sat = 0;
    *hue = 160;
    return;
  }
  int den = sum <= 255 ? sum : 510 - sum;
  *sat = (d * 240 + den / 2) / den;
  double h;
  if (r == mx)
    h = (double)(g - b) / d;
  else if (g == mx)
    h = 2 + (double)(b - r) / d;
  else
    h = 4 + (double)(r - g) / d;
  h *= 40;
  if (h < 0) h += 240;
  *hue = (int)(h + 0.5) % 240;
}

COLORREF HlsToRgb(int hue, int lum, int sat) {
  double l = lum / 240.0, s = sat / 240.0, h = hue / 240.0;
  if (sat == 0) {
    int v = (int)(l * 255 + 0.5);
    return RGB(v, v, v);
  }
  double q = l < 0.5 ? l * (1 + s) : l + s - l * s, p = 2 * l - q;
  auto f = [&](double t) {
    if (t < 0) t += 1;
    if (t > 1) t -= 1;
    if (t < 1 / 6.0) return p + (q - p) * 6 * t;
    if (t < 0.5) return q;
    if (t < 2 / 3.0) return p + (q - p) * (2 / 3.0 - t) * 6;
    return p;
  };
  auto byte = [](double v) { return (int)std::min(255.0, std::max(0.0, v * 255 + 0.5)); };
  return RGB(byte(f(h + 1 / 3.0)), byte(f(h)), byte(f(h - 1 / 3.0)));
}

struct ColorDlg {
  CHOOSECOLORW* cc;
  CHOOSECOLORA* ansi;
  COLORREF color;
  COLORREF custom[16];
  int basicSel = -1, customSel = 0;
  bool full = false, updating = false;
  int fullWidth = 0, shortWidth = 0;
  UINT okMsg = 0, setRgbMsg = 0;
};

LPCCHOOKPROC ColorHook(ColorDlg* st) {
  return (st->cc->Flags & CC_ENABLEHOOK) ? st->cc->lpfnHook : NULL;
}

// The swatches, in dialog units: the basic colors from y 14, the
// custom ones from y 106, eight across, 17 by 13 each.
RECT Swatch(HWND h, bool custom, int i) {
  RECT r;
  r.left = 6 + (i % 8) * 17;
  r.top = (custom ? 106 : 14) + (i / 8) * 13;
  r.right = r.left + 15;
  r.bottom = r.top + 11;
  MapDialogRect(h, &r);
  return r;
}

RECT Preview(HWND h) {
  RECT r = {160, 10, 288, 64};
  MapDialogRect(h, &r);
  return r;
}

void ShowColor(HWND h, ColorDlg* st, int except) {
  st->updating = true;
  int hue, lum, sat;
  RgbToHls(st->color, &hue, &lum, &sat);
  const int ids[6] = {kColorRed, kColorGreen, kColorBlue, kColorHue, kColorSat, kColorLum};
  const int vals[6] = {GetRValue(st->color), GetGValue(st->color), GetBValue(st->color), hue, sat, lum};
  for (int i = 0; i < 6; i++)
    if (ids[i] != except) SetDlgItemInt(h, ids[i], vals[i], FALSE);
  st->updating = false;
  RECT r = Preview(h);
  InvalidateRect(h, &r, FALSE);
}

void PaintColors(HWND h, ColorDlg* st) {
  PAINTSTRUCT ps;
  HDC dc = BeginPaint(h, &ps);
  for (int k = 0; k < 2; k++) {
    bool custom = k == 1;
    for (int i = 0; i < (custom ? 16 : 48); i++) {
      RECT r = Swatch(h, custom, i);
      HBRUSH br = CreateSolidBrush(custom ? st->custom[i] : BasicColor(i));
      FillRect(dc, &r, br);
      DeleteObject(br);
      DrawEdge(dc, &r, EDGE_SUNKEN, BF_RECT);
      bool sel = custom ? i == st->customSel : i == st->basicSel;
      if (sel) {
        RECT o = {r.left - 2, r.top - 2, r.right + 2, r.bottom + 2};
        FrameRect(dc, &o, (HBRUSH)GetStockObject(BLACK_BRUSH));
      }
    }
  }
  if (st->full) {
    RECT r = Preview(h);
    HBRUSH br = CreateSolidBrush(st->color);
    FillRect(dc, &r, br);
    DeleteObject(br);
    DrawEdge(dc, &r, EDGE_SUNKEN, BF_RECT);
  }
  EndPaint(h, &ps);
}

void OpenFull(HWND h, ColorDlg* st) {
  if (st->full) return;
  st->full = true;
  RECT wr;
  GetWindowRect(h, &wr);
  SetWindowPos(h, NULL, 0, 0, st->fullWidth, wr.bottom - wr.top, SWP_NOMOVE | SWP_NOZORDER);
  EnableWindow(GetDlgItem(h, kColorMix), FALSE);
  InvalidateRect(h, NULL, TRUE);
}

INT_PTR CALLBACK ColorProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
  ColorDlg* st;
  if (msg == WM_INITDIALOG) {
    st = (ColorDlg*)lp;
    SetPropW(h, kStateProp, (HANDLE)st);
    for (int id : {kColorRed, kColorGreen, kColorBlue, kColorHue, kColorSat, kColorLum})
      SendDlgItemMessageW(h, id, EM_LIMITTEXT, 3, 0);
    RECT wr, cr;
    GetWindowRect(h, &wr);
    GetClientRect(h, &cr);
    RECT left = {0, 0, 150, 0};
    MapDialogRect(h, &left);
    st->fullWidth = wr.right - wr.left;
    st->shortWidth = st->fullWidth - (cr.right - left.right);
    if (st->cc->Flags & CC_PREVENTFULLOPEN) EnableWindow(GetDlgItem(h, kColorMix), FALSE);
    if (!(st->cc->Flags & CC_SHOWHELP)) ShowWindow(GetDlgItem(h, pshHelp), SW_HIDE);
    ShowColor(h, st, 0);
    if (st->cc->Flags & CC_FULLOPEN) {
      st->full = true;
      EnableWindow(GetDlgItem(h, kColorMix), FALSE);
    } else {
      SetWindowPos(h, NULL, 0, 0, st->shortWidth, wr.bottom - wr.top, SWP_NOMOVE | SWP_NOZORDER);
    }
    if (LPCCHOOKPROC hook = ColorHook(st))
      return (INT_PTR)hook(h, msg, wp, st->ansi ? (LPARAM)st->ansi : (LPARAM)st->cc);
    return TRUE;
  }
  st = (ColorDlg*)GetPropW(h, kStateProp);
  if (!st) return FALSE;
  if (LPCCHOOKPROC hook = ColorHook(st))
    if (hook(h, msg, wp, lp)) return TRUE;
  if (msg == st->setRgbMsg && msg != 0) {
    st->color = (COLORREF)lp & 0xFFFFFF;
    st->basicSel = -1;
    for (int i = 0; i < 48; i++)
      if (BasicColor(i) == st->color) st->basicSel = i;
    ShowColor(h, st, 0);
    InvalidateRect(h, NULL, FALSE);
    return TRUE;
  }
  switch (msg) {
    case WM_DESTROY:
      RemovePropW(h, kStateProp);
      return FALSE;
    case WM_PAINT:
      PaintColors(h, st);
      return TRUE;
    case WM_LBUTTONDOWN: {
      POINT pt = {(short)LOWORD(lp), (short)HIWORD(lp)};
      for (int k = 0; k < 2; k++) {
        bool custom = k == 1;
        for (int i = 0; i < (custom ? 16 : 48); i++) {
          RECT r = Swatch(h, custom, i);
          if (!PtInRect(&r, pt)) continue;
          if (custom) {
            st->customSel = i;
            st->basicSel = -1;
            st->color = st->custom[i];
          } else {
            st->basicSel = i;
            st->color = BasicColor(i);
          }
          ShowColor(h, st, 0);
          InvalidateRect(h, NULL, FALSE);
          return TRUE;
        }
      }
      return FALSE;
    }
    case WM_COMMAND: {
      int id = LOWORD(wp), code = HIWORD(wp);
      switch (id) {
        case IDOK: {
          COLORREF* out = st->cc->lpCustColors;
          for (int i = 0; i < 16 && out; i++) out[i] = st->custom[i];
          st->cc->rgbResult = st->color;
          if (st->ansi) st->ansi->rgbResult = st->color;
          if (LPCCHOOKPROC hook = ColorHook(st))
            if (hook(h, st->okMsg, 0, st->ansi ? (LPARAM)st->ansi : (LPARAM)st->cc)) return TRUE;
          EndDialog(h, IDOK);
          return TRUE;
        }
        case IDCANCEL:
          EndDialog(h, IDCANCEL);
          return TRUE;
        case pshHelp:
          if (HWND owner = st->cc->hwndOwner)
            SendMessageW(owner, RegisterWindowMessageW(HELPMSGSTRINGW), (WPARAM)h,
                         st->ansi ? (LPARAM)st->ansi : (LPARAM)st->cc);
          return TRUE;
        case kColorMix:
          OpenFull(h, st);
          return TRUE;
        case kColorAdd:
          st->custom[st->customSel] = st->color;
          st->customSel = (st->customSel + 1) % 16;
          InvalidateRect(h, NULL, FALSE);
          return TRUE;
        case kColorRed:
        case kColorGreen:
        case kColorBlue:
        case kColorHue:
        case kColorSat:
        case kColorLum: {
          if (code != EN_CHANGE || st->updating) return TRUE;
          BOOL ok;
          UINT v = GetDlgItemInt(h, id, &ok, FALSE);
          if (!ok) return TRUE;
          int r = GetRValue(st->color), g = GetGValue(st->color), b = GetBValue(st->color);
          int hue, lum, sat;
          RgbToHls(st->color, &hue, &lum, &sat);
          int top = id >= kColorRed ? 255 : 240;
          int n = (int)std::min<UINT>(v, top);
          switch (id) {
            case kColorRed: r = n; break;
            case kColorGreen: g = n; break;
            case kColorBlue: b = n; break;
            case kColorHue: hue = std::min(n, 239); break;
            case kColorSat: sat = n; break;
            case kColorLum: lum = n; break;
          }
          st->color = id >= kColorRed ? RGB(r, g, b) : HlsToRgb(hue, lum, sat);
          st->basicSel = -1;
          ShowColor(h, st, id);
          return TRUE;
        }
      }
      return FALSE;
    }
  }
  return FALSE;
}

BOOL ColorCore(CHOOSECOLORW* cc, CHOOSECOLORA* ansi) {
  tlsDlgError = 0;
  if (cc == NULL || cc->lStructSize != sizeof(CHOOSECOLORW)) {
    tlsDlgError = CDERR_STRUCTSIZE;
    return FALSE;
  }
  if ((cc->Flags & CC_ENABLEHOOK) && cc->lpfnHook == NULL) {
    tlsDlgError = CDERR_NOHOOK;
    return FALSE;
  }
  ColorDlg st;
  st.cc = cc;
  st.ansi = ansi;
  st.color = (cc->Flags & CC_RGBINIT) ? cc->rgbResult & 0xFFFFFF : RGB(0, 0, 0);
  for (int i = 0; i < 16; i++) st.custom[i] = cc->lpCustColors ? cc->lpCustColors[i] : RGB(255, 255, 255);
  for (int i = 0; i < 48; i++)
    if (BasicColor(i) == st.color) st.basicSel = i;
  st.okMsg = RegisterWindowMessageW(COLOROKSTRINGW);
  st.setRgbMsg = RegisterWindowMessageW(SETRGBSTRINGW);

  Template t(DS_3DLOOK, 300, 178, L"Color");
  Label(t, 6, 4, 140, kNoId, L"&Basic colors:");
  Label(t, 6, 96, 140, kNoId, L"&Custom colors:");
  Push(t, 6, 138, 138, kColorMix, L"&Define Custom Colors >>");
  Push(t, 6, 158, 44, IDOK, L"OK", true);
  Push(t, 54, 158, 44, IDCANCEL, L"Cancel");
  Push(t, 102, 158, 44, pshHelp, L"&Help");
  struct Field {
    int x, y, id;
    const wchar_t* label;
  };
  const Field fields[] = {{160, 72, kColorHue, L"Hu&e:"},  {160, 88, kColorSat, L"&Sat:"},
                          {160, 104, kColorLum, L"&Lum:"}, {226, 72, kColorRed, L"&Red:"},
                          {226, 88, kColorGreen, L"&Green:"}, {226, 104, kColorBlue, L"Bl&ue:"}};
  for (const Field& f : fields) {
    Label(t, f.x, f.y + 2, 26, kNoId, f.label);
    t.Add(Template::kEdit, ES_NUMBER | WS_BORDER | WS_TABSTOP, f.x + 28, f.y, 24, 12, f.id, L"");
  }
  Push(t, 160, 138, 128, kColorAdd, L"&Add to Custom Colors");
  INT_PTR r = DialogBoxIndirectParamW(NULL, t.Get(), cc->hwndOwner, ColorProc, (LPARAM)&st);
  if (r < 0) tlsDlgError = CDERR_DIALOGFAILURE;
  return r == IDOK;
}

// ---------------------------------------------------------------------
// Font

struct FaceInfo {
  std::wstring name;
  BYTE charset, pitchAndFamily;
  DWORD type;
};

struct FontDlg {
  CHOOSEFONTW* cf;
  CHOOSEFONTA* ansi;
  LOGFONTW lf;              // the caller's, widened for an A call
  std::vector<FaceInfo> faces;
  HFONT sample = NULL;
  COLORREF color;
  int dpi = 96;
};

const wchar_t* const kStyles[] = {L"Regular", L"Italic", L"Bold", L"Bold Italic"};
const int kSizes[] = {8, 9, 10, 11, 12, 14, 16, 18, 20, 22, 24, 26, 28, 36, 48, 72};
struct NamedColor {
  COLORREF c;
  const wchar_t* name;
};
const NamedColor kColors[] = {
    {RGB(0, 0, 0), L"Black"},        {RGB(128, 0, 0), L"Maroon"},    {RGB(0, 128, 0), L"Green"},
    {RGB(128, 128, 0), L"Olive"},    {RGB(0, 0, 128), L"Navy"},      {RGB(128, 0, 128), L"Purple"},
    {RGB(0, 128, 128), L"Teal"},     {RGB(128, 128, 128), L"Gray"},  {RGB(192, 192, 192), L"Silver"},
    {RGB(255, 0, 0), L"Red"},        {RGB(0, 255, 0), L"Lime"},      {RGB(255, 255, 0), L"Yellow"},
    {RGB(0, 0, 255), L"Blue"},       {RGB(255, 0, 255), L"Fuchsia"}, {RGB(0, 255, 255), L"Aqua"},
    {RGB(255, 255, 255), L"White"}};

LPCFHOOKPROC FontHook(FontDlg* st) { return (st->cf->Flags & CF_ENABLEHOOK) ? st->cf->lpfnHook : NULL; }

int CALLBACK CollectFace(const LOGFONTW* lf, const TEXTMETRICW*, DWORD type, LPARAM p) {
  FontDlg* st = (FontDlg*)p;
  DWORD f = st->cf->Flags;
  if ((f & CF_FIXEDPITCHONLY) && (lf->lfPitchAndFamily & 3) != FIXED_PITCH) return 1;
  if ((f & (CF_TTONLY | CF_SCALABLEONLY)) && !(type & (TRUETYPE_FONTTYPE | DEVICE_FONTTYPE))) return 1;
  if (lf->lfFaceName[0] == L'@') return 1;
  for (const FaceInfo& fi : st->faces)
    if (wcscasecmp(fi.name.c_str(), lf->lfFaceName) == 0) return 1;
  st->faces.push_back({lf->lfFaceName, lf->lfCharSet, lf->lfPitchAndFamily, type});
  return 1;
}

const FaceInfo* FindFace(FontDlg* st, const std::wstring& name) {
  for (const FaceInfo& fi : st->faces)
    if (wcscasecmp(fi.name.c_str(), name.c_str()) == 0) return &fi;
  return NULL;
}

// The font the controls describe. Fails without a size.
bool ReadFont(HWND h, FontDlg* st, LOGFONTW* lf, int* pt10) {
  *lf = st->lf;
  std::wstring face = Trim(Text(h, cmb1));
  std::wstring size = Trim(Text(h, cmb3));
  wchar_t* end = NULL;
  double pt = wcstod(size.c_str(), &end);
  if (size.empty() || (end && *end) || pt <= 0) return false;
  *pt10 = (int)(pt * 10 + 0.5);
  lf->lfHeight = -MulDiv(*pt10, st->dpi, 720);
  lf->lfWidth = 0;
  LRESULT style = SendDlgItemMessageW(h, cmb2, CB_GETCURSEL, 0, 0);
  if (style >= 0) {
    lf->lfWeight = style >= 2 ? FW_BOLD : FW_NORMAL;
    lf->lfItalic = (BYTE)(style == 1 || style == 3);
  }
  if (st->cf->Flags & CF_EFFECTS) {
    lf->lfStrikeOut = (BYTE)(IsDlgButtonChecked(h, chx1) == BST_CHECKED);
    lf->lfUnderline = (BYTE)(IsDlgButtonChecked(h, chx2) == BST_CHECKED);
  }
  if (!face.empty()) {
    wcsncpy(lf->lfFaceName, face.c_str(), LF_FACESIZE - 1);
    lf->lfFaceName[LF_FACESIZE - 1] = 0;
    if (const FaceInfo* fi = FindFace(st, face)) {
      lf->lfCharSet = fi->charset;
      lf->lfPitchAndFamily = fi->pitchAndFamily;
    }
  }
  return true;
}

void UpdateSample(HWND h, FontDlg* st) {
  LOGFONTW lf;
  int pt10;
  if (!ReadFont(h, st, &lf, &pt10)) return;
  HDC dc = GetDC(h);
  lf.lfHeight = -MulDiv(pt10, GetDeviceCaps(dc, LOGPIXELSY), 720);
  ReleaseDC(h, dc);
  HFONT f = CreateFontIndirectW(&lf);
  SendDlgItemMessageW(h, stc5, WM_SETFONT, (WPARAM)f, TRUE);
  if (st->sample) DeleteObject(st->sample);
  st->sample = f;
  LRESULT c = SendDlgItemMessageW(h, cmb4, CB_GETCURSEL, 0, 0);
  if (c >= 0) st->color = (COLORREF)SendDlgItemMessageW(h, cmb4, CB_GETITEMDATA, c, 0);
  InvalidateRect(GetDlgItem(h, stc5), NULL, TRUE);
}

void LogFontToA(const LOGFONTW& w, LOGFONTA* a) {
  memcpy(a, &w, offsetof(LOGFONTW, lfFaceName));
  std::string n = Narrow(w.lfFaceName);
  strncpy(a->lfFaceName, n.c_str(), LF_FACESIZE - 1);
  a->lfFaceName[LF_FACESIZE - 1] = 0;
}

void LogFontToW(const LOGFONTA& a, LOGFONTW* w) {
  memcpy(w, &a, offsetof(LOGFONTA, lfFaceName));
  std::wstring n = Widen(a.lfFaceName);
  wcsncpy(w->lfFaceName, n.c_str(), LF_FACESIZE - 1);
  w->lfFaceName[LF_FACESIZE - 1] = 0;
}

bool FontOk(HWND h, FontDlg* st) {
  CHOOSEFONTW* cf = st->cf;
  LOGFONTW lf;
  int pt10;
  if (!ReadFont(h, st, &lf, &pt10)) {
    MessageBoxW(h, L"Enter a number for the size.", L"Font", MB_OK | MB_ICONEXCLAMATION);
    return false;
  }
  if ((cf->Flags & CF_LIMITSIZE) && (pt10 < cf->nSizeMin * 10 || pt10 > cf->nSizeMax * 10)) {
    wchar_t msg[96];
    swprintf(msg, 96, L"The size must be between %d and %d points.", cf->nSizeMin, cf->nSizeMax);
    MessageBoxW(h, msg, L"Font", MB_OK | MB_ICONEXCLAMATION);
    return false;
  }
  if ((cf->Flags & CF_FORCEFONTEXIST) && !FindFace(st, lf.lfFaceName)) {
    MessageBoxW(h, L"There is no font with that name. Choose a font from the list.", L"Font",
                MB_OK | MB_ICONEXCLAMATION);
    return false;
  }
  WORD type = (WORD)((cf->Flags & CF_PRINTERFONTS) && cf->hDC ? PRINTER_FONTTYPE : SCREEN_FONTTYPE);
  type |= lf.lfWeight >= FW_BOLD ? BOLD_FONTTYPE : REGULAR_FONTTYPE;
  if (lf.lfItalic) type |= ITALIC_FONTTYPE;
  int style = (lf.lfWeight >= FW_BOLD ? 2 : 0) + (lf.lfItalic ? 1 : 0);
  st->lf = lf;
  cf->iPointSize = pt10;
  cf->rgbColors = st->color;
  cf->nFontType = type;
  if (cf->lpLogFont) *cf->lpLogFont = lf;
  if ((cf->Flags & CF_USESTYLE) && cf->lpszStyle) PutString(kStyles[style], cf->lpszStyle, LF_FACESIZE, false);
  if (CHOOSEFONTA* a = st->ansi) {
    a->iPointSize = pt10;
    a->rgbColors = st->color;
    a->nFontType = type;
    if (a->lpLogFont) LogFontToA(lf, a->lpLogFont);
    if ((a->Flags & CF_USESTYLE) && a->lpszStyle) PutString(kStyles[style], a->lpszStyle, LF_FACESIZE, true);
  }
  return true;
}

INT_PTR CALLBACK FontProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
  FontDlg* st;
  if (msg == WM_INITDIALOG) {
    st = (FontDlg*)lp;
    SetPropW(h, kStateProp, (HANDLE)st);
    CHOOSEFONTW* cf = st->cf;
    for (const FaceInfo& fi : st->faces) SendDlgItemMessageW(h, cmb1, CB_ADDSTRING, 0, (LPARAM)fi.name.c_str());
    for (const wchar_t* s : kStyles) SendDlgItemMessageW(h, cmb2, CB_ADDSTRING, 0, (LPARAM)s);
    for (int s : kSizes) {
      if ((cf->Flags & CF_LIMITSIZE) && (s < cf->nSizeMin || s > cf->nSizeMax)) continue;
      wchar_t buf[8];
      swprintf(buf, 8, L"%d", s);
      SendDlgItemMessageW(h, cmb3, CB_ADDSTRING, 0, (LPARAM)buf);
    }
    bool known = false;
    for (const NamedColor& c : kColors) {
      LRESULT i = SendDlgItemMessageW(h, cmb4, CB_ADDSTRING, 0, (LPARAM)c.name);
      SendDlgItemMessageW(h, cmb4, CB_SETITEMDATA, i, (LPARAM)c.c);
      if (c.c == st->color) {
        SendDlgItemMessageW(h, cmb4, CB_SETCURSEL, i, 0);
        known = true;
      }
    }
    if (!known) {
      LRESULT i = SendDlgItemMessageW(h, cmb4, CB_ADDSTRING, 0, (LPARAM)L"Custom");
      SendDlgItemMessageW(h, cmb4, CB_SETITEMDATA, i, (LPARAM)st->color);
      SendDlgItemMessageW(h, cmb4, CB_SETCURSEL, i, 0);
    }
    const LOGFONTW& lf = st->lf;
    if (cf->Flags & CF_INITTOLOGFONTSTRUCT) {
      if (!(cf->Flags & CF_NOFACESEL)) {
        LRESULT i = SendDlgItemMessageW(h, cmb1, CB_FINDSTRINGEXACT, (WPARAM)-1, (LPARAM)lf.lfFaceName);
        if (i >= 0)
          SendDlgItemMessageW(h, cmb1, CB_SETCURSEL, i, 0);
        else
          SetDlgItemTextW(h, cmb1, lf.lfFaceName);
      }
      if (!(cf->Flags & CF_NOSTYLESEL))
        SendDlgItemMessageW(h, cmb2, CB_SETCURSEL, (lf.lfWeight >= FW_BOLD ? 2 : 0) + (lf.lfItalic ? 1 : 0), 0);
      if (!(cf->Flags & CF_NOSIZESEL) && lf.lfHeight != 0) {
        int pt10 = MulDiv(lf.lfHeight < 0 ? -lf.lfHeight : lf.lfHeight, 720, st->dpi);
        wchar_t buf[16];
        if (pt10 % 10)
          swprintf(buf, 16, L"%d.%d", pt10 / 10, pt10 % 10);
        else
          swprintf(buf, 16, L"%d", pt10 / 10);
        SetDlgItemTextW(h, cmb3, buf);
      }
      CheckDlgButton(h, chx1, lf.lfStrikeOut ? BST_CHECKED : BST_UNCHECKED);
      CheckDlgButton(h, chx2, lf.lfUnderline ? BST_CHECKED : BST_UNCHECKED);
    }
    if ((cf->Flags & CF_USESTYLE) && cf->lpszStyle && *cf->lpszStyle) {
      LRESULT i = SendDlgItemMessageW(h, cmb2, CB_FINDSTRINGEXACT, (WPARAM)-1, (LPARAM)cf->lpszStyle);
      if (i >= 0) SendDlgItemMessageW(h, cmb2, CB_SETCURSEL, i, 0);
    }
    if (SendDlgItemMessageW(h, cmb2, CB_GETCURSEL, 0, 0) < 0 && !(cf->Flags & CF_NOSTYLESEL))
      SendDlgItemMessageW(h, cmb2, CB_SETCURSEL, 0, 0);
    if (!(cf->Flags & CF_EFFECTS))
      for (int id : {grp1, chx1, chx2, stc4, cmb4}) ShowWindow(GetDlgItem(h, id), SW_HIDE);
    if (!(cf->Flags & CF_APPLY)) ShowWindow(GetDlgItem(h, psh3), SW_HIDE);
    if (!(cf->Flags & CF_SHOWHELP)) ShowWindow(GetDlgItem(h, pshHelp), SW_HIDE);
    UpdateSample(h, st);
    if (LPCFHOOKPROC hook = FontHook(st))
      return (INT_PTR)hook(h, msg, wp, st->ansi ? (LPARAM)st->ansi : (LPARAM)cf);
    return TRUE;
  }
  st = (FontDlg*)GetPropW(h, kStateProp);
  if (!st) return FALSE;
  if (LPCFHOOKPROC hook = FontHook(st))
    if (hook(h, msg, wp, lp)) return TRUE;
  switch (msg) {
    case WM_DESTROY:
      RemovePropW(h, kStateProp);
      if (st->sample) DeleteObject(st->sample);
      st->sample = NULL;
      return FALSE;
    case WM_CHOOSEFONT_GETLOGFONT: {
      LOGFONTW lf;
      int pt10;
      if (!ReadFont(h, st, &lf, &pt10)) lf = st->lf;
      if (st->ansi)
        LogFontToA(lf, (LOGFONTA*)lp);
      else
        *(LOGFONTW*)lp = lf;
      return TRUE;
    }
    case WM_CTLCOLORSTATIC:
      if ((HWND)lp == GetDlgItem(h, stc5)) {
        SetTextColor((HDC)wp, st->color);
        SetBkMode((HDC)wp, TRANSPARENT);
        return (INT_PTR)GetSysColorBrush(COLOR_BTNFACE);
      }
      return FALSE;
    case WM_COMMAND: {
      int id = LOWORD(wp), code = HIWORD(wp);
      switch (id) {
        case IDOK:
          if (FontOk(h, st)) EndDialog(h, IDOK);
          return TRUE;
        case IDCANCEL:
          EndDialog(h, IDCANCEL);
          return TRUE;
        case pshHelp:
          if (HWND owner = st->cf->hwndOwner)
            SendMessageW(owner, RegisterWindowMessageW(HELPMSGSTRINGW), (WPARAM)h,
                         st->ansi ? (LPARAM)st->ansi : (LPARAM)st->cf);
          return TRUE;
        case cmb1:
        case cmb2:
        case cmb3:
        case cmb4:
          if (code == CBN_SELCHANGE || code == CBN_EDITCHANGE) {
            if (code == CBN_SELCHANGE && id != cmb2 && id != cmb4) {
              // The edit shows the new choice only after this message.
              LRESULT i = SendDlgItemMessageW(h, id, CB_GETCURSEL, 0, 0);
              if (i >= 0) {
                LRESULT n = SendDlgItemMessageW(h, id, CB_GETLBTEXTLEN, i, 0);
                std::wstring s(n > 0 ? n + 1 : 1, 0);
                SendDlgItemMessageW(h, id, CB_GETLBTEXT, i, (LPARAM)&s[0]);
                SetDlgItemTextW(h, id, s.c_str());
              }
            }
            UpdateSample(h, st);
          }
          return TRUE;
        case chx1:
        case chx2:
          UpdateSample(h, st);
          return TRUE;
      }
      return FALSE;
    }
  }
  return FALSE;
}

BOOL FontCore(CHOOSEFONTW* cf, CHOOSEFONTA* ansi) {
  tlsDlgError = 0;
  if (cf == NULL || cf->lStructSize != sizeof(CHOOSEFONTW)) {
    tlsDlgError = CDERR_STRUCTSIZE;
    return FALSE;
  }
  if ((cf->Flags & CF_ENABLEHOOK) && cf->lpfnHook == NULL) {
    tlsDlgError = CDERR_NOHOOK;
    return FALSE;
  }
  if ((cf->Flags & CF_LIMITSIZE) && cf->nSizeMax < cf->nSizeMin) {
    tlsDlgError = CFERR_MAXLESSTHANMIN;
    return FALSE;
  }
  FontDlg st;
  st.cf = cf;
  st.ansi = ansi;
  memset(&st.lf, 0, sizeof st.lf);
  if (cf->lpLogFont) st.lf = *cf->lpLogFont;
  if (!st.lf.lfCharSet) st.lf.lfCharSet = DEFAULT_CHARSET;
  st.color = (cf->Flags & CF_EFFECTS) ? cf->rgbColors & 0xFFFFFF : RGB(0, 0, 0);
  HDC dc = (cf->Flags & CF_PRINTERFONTS) && cf->hDC ? cf->hDC : GetDC(NULL);
  st.dpi = GetDeviceCaps(dc, LOGPIXELSY);
  if (st.dpi <= 0) st.dpi = 96;
  LOGFONTW all;
  memset(&all, 0, sizeof all);
  all.lfCharSet = DEFAULT_CHARSET;
  EnumFontFamiliesExW(dc, &all, CollectFace, (LPARAM)&st, 0);
  if (dc != cf->hDC) ReleaseDC(NULL, dc);
  if (st.faces.empty()) {
    tlsDlgError = CFERR_NOFONTS;
    return FALSE;
  }

  Template t(DS_3DLOOK, 286, 112, L"Font");
  Label(t, 7, 7, 98, stc1, L"&Font:");
  t.Add(Template::kCombo, CBS_DROPDOWN | CBS_SORT | CBS_AUTOHSCROLL | WS_VSCROLL | WS_TABSTOP, 7, 17, 98, 100, cmb1,
        L"");
  Label(t, 110, 7, 74, stc2, L"Font st&yle:");
  t.Add(Template::kCombo, CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, 110, 17, 74, 80, cmb2, L"");
  Label(t, 189, 7, 36, stc3, L"&Size:");
  t.Add(Template::kCombo, CBS_DROPDOWN | CBS_AUTOHSCROLL | WS_VSCROLL | WS_TABSTOP, 189, 17, 36, 100, cmb3, L"");
  Push(t, 230, 17, 50, IDOK, L"OK", true);
  Push(t, 230, 34, 50, IDCANCEL, L"Cancel");
  Push(t, 230, 51, 50, psh3, L"&Apply");
  Push(t, 230, 68, 50, pshHelp, L"&Help");
  t.Add(Template::kButton, BS_GROUPBOX, 7, 40, 98, 64, grp1, L"Effects");
  t.Add(Template::kButton, BS_AUTOCHECKBOX | WS_TABSTOP, 13, 52, 80, 10, chx1, L"Stri&keout");
  t.Add(Template::kButton, BS_AUTOCHECKBOX | WS_TABSTOP, 13, 64, 80, 10, chx2, L"&Underline");
  Label(t, 13, 78, 80, stc4, L"&Color:");
  t.Add(Template::kCombo, CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, 13, 88, 85, 100, cmb4, L"");
  t.Add(Template::kButton, BS_GROUPBOX, 110, 40, 115, 64, grp2, L"Sample");
  t.Add(Template::kStatic, SS_CENTER, 116, 52, 103, 46, stc5, L"AaBbYyZz");
  INT_PTR r = DialogBoxIndirectParamW(NULL, t.Get(), cf->hwndOwner, FontProc, (LPARAM)&st);
  if (r < 0) tlsDlgError = CDERR_DIALOGFAILURE;
  return r == IDOK;
}

// ---------------------------------------------------------------------
// Find and Replace

struct FindDlg {
  FINDREPLACEW* fr;   // a W call's own
  FINDREPLACEA* ansi; // an A call's own
  bool replace;
  UINT msg;
};

DWORD& FindFlags(FindDlg* st) { return st->ansi ? st->ansi->Flags : st->fr->Flags; }
LPARAM FindParam(FindDlg* st) { return st->ansi ? (LPARAM)st->ansi : (LPARAM)st->fr; }
HWND FindOwner(FindDlg* st) { return st->ansi ? st->ansi->hwndOwner : st->fr->hwndOwner; }
LPFRHOOKPROC FindHook(FindDlg* st) {
  return (FindFlags(st) & FR_ENABLEHOOK) ? (st->ansi ? st->ansi->lpfnHook : st->fr->lpfnHook) : NULL;
}

// Tells the owner what the user asked for.
void SendFind(HWND h, FindDlg* st, DWORD action) {
  DWORD& f = FindFlags(st);
  f &= ~(FR_DOWN | FR_WHOLEWORD | FR_MATCHCASE | FR_FINDNEXT | FR_REPLACE | FR_REPLACEALL | FR_DIALOGTERM);
  f |= action;
  if (action != FR_DIALOGTERM) {
    if (IsDlgButtonChecked(h, chx1) == BST_CHECKED) f |= FR_WHOLEWORD;
    if (IsDlgButtonChecked(h, chx2) == BST_CHECKED) f |= FR_MATCHCASE;
    if (st->replace || IsDlgButtonChecked(h, rad2) == BST_CHECKED) f |= FR_DOWN;
    bool a = st->ansi != NULL;
    if (a) {
      PutString(Text(h, edt1), st->ansi->lpstrFindWhat, st->ansi->wFindWhatLen, true);
      if (st->replace) PutString(Text(h, edt2), st->ansi->lpstrReplaceWith, st->ansi->wReplaceWithLen, true);
    } else {
      PutString(Text(h, edt1), st->fr->lpstrFindWhat, st->fr->wFindWhatLen, false);
      if (st->replace) PutString(Text(h, edt2), st->fr->lpstrReplaceWith, st->fr->wReplaceWithLen, false);
    }
  }
  SendMessageW(FindOwner(st), st->msg, 0, FindParam(st));
}

void EnableFind(HWND h) {
  bool any = GetWindowTextLengthW(GetDlgItem(h, edt1)) > 0;
  for (int id : {IDOK, psh1, psh2}) EnableWindow(GetDlgItem(h, id), any);
}

INT_PTR CALLBACK FindProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
  FindDlg* st;
  if (msg == WM_INITDIALOG) {
    st = (FindDlg*)lp;
    SetPropW(h, kStateProp, (HANDLE)st);
    bool a = st->ansi != NULL;
    DWORD f = FindFlags(st);
    SetText(h, edt1, a ? (const void*)st->ansi->lpstrFindWhat : st->fr->lpstrFindWhat, a);
    if (st->replace) SetText(h, edt2, a ? (const void*)st->ansi->lpstrReplaceWith : st->fr->lpstrReplaceWith, a);
    SendDlgItemMessageW(h, edt1, EM_LIMITTEXT, (a ? st->ansi->wFindWhatLen : st->fr->wFindWhatLen) - 1, 0);
    CheckDlgButton(h, chx1, (f & FR_WHOLEWORD) ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(h, chx2, (f & FR_MATCHCASE) ? BST_CHECKED : BST_UNCHECKED);
    if (!st->replace) CheckRadioButton(h, rad1, rad2, (f & FR_DOWN) ? rad2 : rad1);
    if (f & FR_NOWHOLEWORD) EnableWindow(GetDlgItem(h, chx1), FALSE);
    if (f & FR_HIDEWHOLEWORD) ShowWindow(GetDlgItem(h, chx1), SW_HIDE);
    if (f & FR_NOMATCHCASE) EnableWindow(GetDlgItem(h, chx2), FALSE);
    if (f & FR_HIDEMATCHCASE) ShowWindow(GetDlgItem(h, chx2), SW_HIDE);
    if (!st->replace) {
      for (int id : {grp1, rad1, rad2}) {
        if (f & FR_NOUPDOWN) EnableWindow(GetDlgItem(h, id), FALSE);
        if (f & FR_HIDEUPDOWN) ShowWindow(GetDlgItem(h, id), SW_HIDE);
      }
    }
    if (!(f & FR_SHOWHELP)) ShowWindow(GetDlgItem(h, pshHelp), SW_HIDE);
    EnableFind(h);
    SendDlgItemMessageW(h, edt1, EM_SETSEL, 0, -1);
    if (LPFRHOOKPROC hook = FindHook(st)) return (INT_PTR)hook(h, msg, wp, FindParam(st));
    return TRUE;
  }
  st = (FindDlg*)GetPropW(h, kStateProp);
  if (!st) return FALSE;
  if (msg == WM_DESTROY) {
    RemovePropW(h, kStateProp);
    if (LPFRHOOKPROC hook = FindHook(st)) hook(h, msg, wp, lp);
    delete st;
    return FALSE;
  }
  if (LPFRHOOKPROC hook = FindHook(st))
    if (hook(h, msg, wp, lp)) return TRUE;
  if (msg == WM_CLOSE) {
    SendMessageW(h, WM_COMMAND, IDCANCEL, 0);
    return TRUE;
  }
  if (msg != WM_COMMAND) return FALSE;
  int id = LOWORD(wp), code = HIWORD(wp);
  switch (id) {
    case edt1:
      if (code == EN_CHANGE) EnableFind(h);
      return TRUE;
    case IDOK:
      SendFind(h, st, FR_FINDNEXT);
      return TRUE;
    case psh1:
      if (st->replace) SendFind(h, st, FR_REPLACE);
      return TRUE;
    case psh2:
      if (st->replace) SendFind(h, st, FR_REPLACEALL);
      return TRUE;
    case pshHelp:
      if (HWND owner = FindOwner(st))
        SendMessageW(owner, RegisterWindowMessageW(HELPMSGSTRINGW), (WPARAM)h, FindParam(st));
      return TRUE;
    case IDCANCEL:
      SendFind(h, st, FR_DIALOGTERM);
      DestroyWindow(h);
      return TRUE;
  }
  return FALSE;
}

HWND FindCore(FINDREPLACEW* fr, FINDREPLACEA* ansi, bool replace) {
  tlsDlgError = 0;
  DWORD size = ansi ? ansi->lStructSize : fr ? fr->lStructSize : 0;
  if (size != (ansi ? sizeof(FINDREPLACEA) : sizeof(FINDREPLACEW))) {
    tlsDlgError = CDERR_STRUCTSIZE;
    return NULL;
  }
  HWND owner = ansi ? ansi->hwndOwner : fr->hwndOwner;
  DWORD flags = ansi ? ansi->Flags : fr->Flags;
  WORD findLen = ansi ? ansi->wFindWhatLen : fr->wFindWhatLen;
  WORD replaceLen = ansi ? ansi->wReplaceWithLen : fr->wReplaceWithLen;
  bool haveFind = ansi ? ansi->lpstrFindWhat != NULL : fr->lpstrFindWhat != NULL;
  bool haveReplace = ansi ? ansi->lpstrReplaceWith != NULL : fr->lpstrReplaceWith != NULL;
  if (!IsWindow(owner)) {
    tlsDlgError = CDERR_DIALOGFAILURE;
    return NULL;
  }
  if (findLen == 0 || (replace && replaceLen == 0)) {
    tlsDlgError = FRERR_BUFFERLENGTHZERO;
    return NULL;
  }
  if (!haveFind || (replace && !haveReplace)) {
    tlsDlgError = CDERR_INITIALIZATION;
    return NULL;
  }
  if ((flags & FR_ENABLEHOOK) && !(ansi ? (void*)ansi->lpfnHook : (void*)fr->lpfnHook)) {
    tlsDlgError = CDERR_NOHOOK;
    return NULL;
  }
  FindDlg* st = new FindDlg;
  st->fr = fr;
  st->ansi = ansi;
  st->replace = replace;
  st->msg = RegisterWindowMessageW(FINDMSGSTRINGW);

  Template t(DS_3DLOOK | DS_CONTEXTHELP, 236, replace ? 94 : 62, replace ? L"Replace" : L"Find");
  Label(t, 5, 7, 48, stc1, L"Fi&nd what:");
  t.Add(Template::kEdit, ES_AUTOHSCROLL | WS_BORDER | WS_TABSTOP | WS_GROUP, 54, 5, 114, 12, edt1, L"");
  if (replace) {
    Label(t, 5, 23, 48, stc2, L"Re&place with:");
    t.Add(Template::kEdit, ES_AUTOHSCROLL | WS_BORDER | WS_TABSTOP | WS_GROUP, 54, 21, 114, 12, edt2, L"");
    t.Add(Template::kButton, BS_AUTOCHECKBOX | WS_TABSTOP | WS_GROUP, 5, 46, 104, 12, chx1, L"Match &whole word only");
    t.Add(Template::kButton, BS_AUTOCHECKBOX | WS_TABSTOP, 5, 62, 104, 12, chx2, L"Match &case");
    Push(t, 174, 4, 56, IDOK, L"&Find Next", true);
    Push(t, 174, 21, 56, psh1, L"&Replace");
    Push(t, 174, 38, 56, psh2, L"Replace &All");
    Push(t, 174, 55, 56, IDCANCEL, L"Cancel");
    Push(t, 174, 75, 56, pshHelp, L"&Help");
  } else {
    t.Add(Template::kButton, BS_AUTOCHECKBOX | WS_TABSTOP | WS_GROUP, 5, 26, 100, 12, chx1, L"Match &whole word only");
    t.Add(Template::kButton, BS_AUTOCHECKBOX | WS_TABSTOP, 5, 42, 64, 12, chx2, L"Match &case");
    t.Add(Template::kButton, BS_GROUPBOX, 107, 26, 60, 28, grp1, L"Direction");
    t.Add(Template::kButton, BS_AUTORADIOBUTTON | WS_GROUP | WS_TABSTOP, 111, 38, 25, 12, rad1, L"&Up");
    t.Add(Template::kButton, BS_AUTORADIOBUTTON, 138, 38, 28, 12, rad2, L"&Down");
    Push(t, 174, 5, 56, IDOK, L"&Find Next", true);
    Push(t, 174, 23, 56, IDCANCEL, L"Cancel");
    Push(t, 174, 45, 56, pshHelp, L"&Help");
  }
  // The dialog is not modal: it lives on, without a frame that waits.
  DLGTEMPLATE* d = (DLGTEMPLATE*)t.Get();
  d->style &= ~DS_MODALFRAME;
  HWND h = CreateDialogIndirectParamW(NULL, t.Get(), owner, FindProc, (LPARAM)st);
  if (!h) {
    delete st;
    tlsDlgError = CDERR_DIALOGFAILURE;
    return NULL;
  }
  ShowWindow(h, SW_SHOW);
  return h;
}

}  // namespace

extern "C" {

BOOL WINAPI GetOpenFileNameW(LPOPENFILENAMEW ofn) { return FileCore(ofn, NULL, false); }
BOOL WINAPI GetSaveFileNameW(LPOPENFILENAMEW ofn) { return FileCore(ofn, NULL, true); }
BOOL WINAPI GetOpenFileNameA(LPOPENFILENAMEA ofn) { return FileCoreA(ofn, false); }
BOOL WINAPI GetSaveFileNameA(LPOPENFILENAMEA ofn) { return FileCoreA(ofn, true); }

BOOL WINAPI ChooseColorW(LPCHOOSECOLORW cc) { return ColorCore(cc, NULL); }

BOOL WINAPI ChooseColorA(LPCHOOSECOLORA a) {
  if (a == NULL || a->lStructSize != sizeof(CHOOSECOLORA)) {
    tlsDlgError = CDERR_STRUCTSIZE;
    return FALSE;
  }
  CHOOSECOLORW w;
  memcpy(&w, a, sizeof w);
  w.lStructSize = sizeof w;
  WideName tmpl((a->Flags & CC_ENABLETEMPLATE) ? a->lpTemplateName : NULL);
  w.lpTemplateName = tmpl.p;
  BOOL r = ColorCore(&w, a);
  a->rgbResult = w.rgbResult;
  a->Flags = w.Flags;
  return r;
}

BOOL WINAPI ChooseFontW(LPCHOOSEFONTW cf) { return FontCore(cf, NULL); }

BOOL WINAPI ChooseFontA(LPCHOOSEFONTA a) {
  if (a == NULL || a->lStructSize != sizeof(CHOOSEFONTA)) {
    tlsDlgError = CDERR_STRUCTSIZE;
    return FALSE;
  }
  CHOOSEFONTW w;
  memcpy(&w, a, sizeof w);
  w.lStructSize = sizeof w;
  LOGFONTW lf;
  memset(&lf, 0, sizeof lf);
  if (a->lpLogFont) LogFontToW(*a->lpLogFont, &lf);
  w.lpLogFont = a->lpLogFont ? &lf : NULL;
  std::wstring style = a->lpszStyle ? Widen(a->lpszStyle) : std::wstring();
  style.resize(LF_FACESIZE);
  w.lpszStyle = a->lpszStyle ? &style[0] : NULL;
  WideName tmpl((a->Flags & CF_ENABLETEMPLATE) ? a->lpTemplateName : NULL);
  w.lpTemplateName = tmpl.p;
  return FontCore(&w, a);
}

HWND WINAPI FindTextW(LPFINDREPLACEW fr) { return FindCore(fr, NULL, false); }
HWND WINAPI ReplaceTextW(LPFINDREPLACEW fr) { return FindCore(fr, NULL, true); }
HWND WINAPI FindTextA(LPFINDREPLACEA fr) {
  if (!fr) {
    tlsDlgError = CDERR_STRUCTSIZE;
    return NULL;
  }
  return FindCore(NULL, fr, false);
}
HWND WINAPI ReplaceTextA(LPFINDREPLACEA fr) {
  if (!fr) {
    tlsDlgError = CDERR_STRUCTSIZE;
    return NULL;
  }
  return FindCore(NULL, fr, true);
}

}  // extern "C"
