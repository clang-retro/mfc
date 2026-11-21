// shell_filedlg.cpp - shell items and the Vista-style file dialog.
//
// IFileOpenDialog and IFileSaveDialog (CLSID_FileOpenDialog and
// CLSID_FileSaveDialog) are the layer's own file dialog (commdlg.cpp)
// driven through an Explorer-style hook: the hook passes the dialog's
// notifications on as IFileDialogEvents, and holds the controls that
// IFileDialogCustomize adds, in a panel below the standard ones. Picking
// folders, OK takes the folder the dialog is in, or the one typed.
//
// A shell item (IShellItem) is a path in the file system.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include <windows.h>
#include <commdlg.h>
#include <dlgs.h>
#include <shlobj.h>
#include <shobjidl.h>

#include "ole.h"
#include "shell.h"
#include "winport.h"

#include <string.h>

#include <algorithm>
#include <string>
#include <vector>

using namespace winport;

namespace {

LPWSTR TaskString(const std::wstring& s) {
  LPWSTR p = (LPWSTR)CoTaskMemAlloc((s.size() + 1) * sizeof(WCHAR));
  if (p) memcpy(p, s.c_str(), (s.size() + 1) * sizeof(WCHAR));
  return p;
}

std::wstring Leaf(const std::wstring& path) {
  if (path == L"/") return path;
  size_t s = path.find_last_of(L'/');
  return s == std::wstring::npos ? path : path.substr(s + 1);
}

std::wstring ParentOf(const std::wstring& path) {
  size_t s = path.find_last_of(L'/');
  if (s == std::wstring::npos || s == 0) return L"/";
  return path.substr(0, s);
}

bool IsFolder(const std::wstring& path) { return IsDir(Narrow(path.c_str())); }

bool Exists(const std::wstring& path) { return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES; }

// A path as the layer writes it: absolute, with '/'.
std::wstring Absolute(const std::wstring& name) { return Widen(AbsolutePath(name).c_str()); }

// ---------------------------------------------------------------------
// Shell items

class ShellItem : public IShellItem {
 public:
  explicit ShellItem(const std::wstring& path) : path_(path) {}
  const std::wstring& Path() const { return path_; }

  STDMETHOD(QueryInterface)(REFIID riid, void** out) override {
    if (!out) return E_POINTER;
    if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_IShellItem)) {
      *out = static_cast<IShellItem*>(this);
      AddRef();
      return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
  }
  STDMETHOD_(ULONG, AddRef)() override { return (ULONG)InterlockedIncrement(&refs_); }
  STDMETHOD_(ULONG, Release)() override {
    LONG n = InterlockedDecrement(&refs_);
    if (n == 0) delete this;
    return (ULONG)n;
  }

  STDMETHOD(BindToHandler)(IBindCtx*, REFGUID, REFIID, void** out) override {
    if (out) *out = NULL;
    return E_NOTIMPL;
  }
  STDMETHOD(GetParent)(IShellItem** out) override {
    if (!out) return E_POINTER;
    *out = NULL;
    if (path_ == L"/") return MK_E_NOOBJECT;
    *out = new ShellItem(ParentOf(path_));
    return S_OK;
  }
  STDMETHOD(GetDisplayName)(SIGDN sigdn, LPWSTR* out) override {
    if (!out) return E_POINTER;
    std::wstring s;
    switch (sigdn) {
      case SIGDN_FILESYSPATH:
      case SIGDN_DESKTOPABSOLUTEPARSING:
      case SIGDN_DESKTOPABSOLUTEEDITING:
        s = path_;
        break;
      case SIGDN_URL:
        s = L"file://" + path_;
        break;
      default:
        s = Leaf(path_);
        break;
    }
    *out = TaskString(s);
    return *out ? S_OK : E_OUTOFMEMORY;
  }
  STDMETHOD(GetAttributes)(SFGAOF mask, SFGAOF* out) override {
    SFGAOF a = SFGAO_FILESYSTEM;
    if (IsFolder(path_))
      a |= SFGAO_FOLDER | SFGAO_FILESYSANCESTOR | SFGAO_BROWSABLE | SFGAO_STORAGE;
    else
      a |= SFGAO_STREAM | SFGAO_CANCOPY | SFGAO_CANMOVE | SFGAO_CANRENAME | SFGAO_CANDELETE;
    if (GetFileAttributesW(path_.c_str()) & FILE_ATTRIBUTE_READONLY) a |= SFGAO_READONLY;
    if (!Leaf(path_).empty() && Leaf(path_)[0] == L'.') a |= SFGAO_HIDDEN;
    a &= mask;
    if (out) *out = a;
    return a == mask ? S_OK : S_FALSE;
  }
  STDMETHOD(Compare)(IShellItem* other, SICHINTF, int* order) override {
    if (!other || !order) return E_INVALIDARG;
    LPWSTR p = NULL;
    if (FAILED(other->GetDisplayName(SIGDN_FILESYSPATH, &p))) return E_FAIL;
    int c = wcscmp(path_.c_str(), p);
    CoTaskMemFree(p);
    *order = c < 0 ? -1 : c > 0 ? 1 : 0;
    return c == 0 ? S_OK : S_FALSE;
  }

 private:
  virtual ~ShellItem() {}
  LONG refs_ = 1;
  std::wstring path_;
};

std::wstring PathOf(IShellItem* item) {
  if (!item) return std::wstring();
  LPWSTR p = NULL;
  std::wstring s;
  if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &p)) && p) {
    s = Absolute(p);
    CoTaskMemFree(p);
  }
  return s;
}

class ShellItemArray : public IShellItemArray {
 public:
  explicit ShellItemArray(const std::vector<std::wstring>& paths) : paths_(paths) {}

  STDMETHOD(QueryInterface)(REFIID riid, void** out) override {
    if (!out) return E_POINTER;
    if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_IShellItemArray)) {
      *out = static_cast<IShellItemArray*>(this);
      AddRef();
      return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
  }
  STDMETHOD_(ULONG, AddRef)() override { return (ULONG)InterlockedIncrement(&refs_); }
  STDMETHOD_(ULONG, Release)() override {
    LONG n = InterlockedDecrement(&refs_);
    if (n == 0) delete this;
    return (ULONG)n;
  }
  STDMETHOD(BindToHandler)(IBindCtx*, REFGUID, REFIID, void** out) override {
    if (out) *out = NULL;
    return E_NOTIMPL;
  }
  STDMETHOD(GetPropertyStore)(GETPROPERTYSTOREFLAGS, REFIID, void** out) override {
    if (out) *out = NULL;
    return E_NOTIMPL;
  }
  STDMETHOD(GetPropertyDescriptionList)(REFPROPERTYKEY, REFIID, void** out) override {
    if (out) *out = NULL;
    return E_NOTIMPL;
  }
  STDMETHOD(GetAttributes)(SIATTRIBFLAGS flags, SFGAOF mask, SFGAOF* out) override {
    bool any = (flags & SIATTRIBFLAGS_MASK) == SIATTRIBFLAGS_OR;
    SFGAOF all = any ? 0 : mask;
    for (const std::wstring& p : paths_) {
      ShellItem* item = new ShellItem(p);
      SFGAOF a = 0;
      item->GetAttributes(mask, &a);
      item->Release();
      all = any ? (all | a) : (all & a);
    }
    if (out) *out = all;
    return all == mask ? S_OK : S_FALSE;
  }
  STDMETHOD(GetCount)(DWORD* out) override {
    if (!out) return E_POINTER;
    *out = (DWORD)paths_.size();
    return S_OK;
  }
  STDMETHOD(GetItemAt)(DWORD i, IShellItem** out) override {
    if (!out) return E_POINTER;
    *out = NULL;
    if (i >= paths_.size()) return E_INVALIDARG;
    *out = new ShellItem(paths_[i]);
    return S_OK;
  }
  STDMETHOD(EnumItems)(IEnumShellItems** out) override {
    if (out) *out = NULL;
    return E_NOTIMPL;
  }

 private:
  virtual ~ShellItemArray() {}
  LONG refs_ = 1;
  std::vector<std::wstring> paths_;
};

// ---------------------------------------------------------------------
// The file dialog

// A control a program adds: its kind, what it holds, and its windows
// while the dialog is up.
struct Control {
  enum Kind { kCheck, kEdit, kText, kPush, kCombo, kRadio, kMenu, kSeparator, kGroup };
  struct Item {
    DWORD id;
    std::wstring label;
    CDCONTROLSTATEF state;
    HWND hwnd;
  };
  Kind kind;
  DWORD id;
  std::wstring label, text;
  BOOL checked = FALSE;
  CDCONTROLSTATEF state = CDCS_ENABLEDVISIBLE;
  std::vector<Item> items;
  DWORD selected = (DWORD)-1;
  DWORD group = (DWORD)-1;  // the visual group it is in
  HWND hwnd = NULL, caption = NULL;
};

const wchar_t kSelfProp[] = L"winport.filedialog";
const wchar_t kOldProcProp[] = L"winport.filedialog.proc";
const int kFirstControlId = 0x5000;

class FileDialog : public IFileOpenDialog, public IFileSaveDialog, public IFileDialogCustomize, public IOleWindow {
 public:
  explicit FileDialog(bool save) : save_(save) {}

  // IUnknown
  STDMETHOD(QueryInterface)(REFIID riid, void** out) override {
    if (!out) return E_POINTER;
    *out = NULL;
    if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_IModalWindow) || IsEqualIID(riid, IID_IFileDialog)) {
      *out = save_ ? (void*)static_cast<IFileSaveDialog*>(this) : (void*)static_cast<IFileOpenDialog*>(this);
    } else if (IsEqualIID(riid, IID_IFileOpenDialog) && !save_) {
      *out = static_cast<IFileOpenDialog*>(this);
    } else if (IsEqualIID(riid, IID_IFileSaveDialog) && save_) {
      *out = static_cast<IFileSaveDialog*>(this);
    } else if (IsEqualIID(riid, IID_IFileDialogCustomize)) {
      *out = static_cast<IFileDialogCustomize*>(this);
    } else if (IsEqualIID(riid, IID_IOleWindow)) {
      *out = static_cast<IOleWindow*>(this);
    } else {
      return E_NOINTERFACE;
    }
    AddRef();
    return S_OK;
  }
  STDMETHOD_(ULONG, AddRef)() override { return (ULONG)InterlockedIncrement(&refs_); }
  STDMETHOD_(ULONG, Release)() override {
    LONG n = InterlockedDecrement(&refs_);
    if (n == 0) delete this;
    return (ULONG)n;
  }

  // IModalWindow
  STDMETHOD(Show)(HWND owner) override;

  // IFileDialog
  STDMETHOD(SetFileTypes)(UINT n, const COMDLG_FILTERSPEC* specs) override {
    if (!specs || !n) return E_INVALIDARG;
    if (dialog_) return E_UNEXPECTED;
    types_.clear();
    for (UINT i = 0; i < n; i++)
      types_.push_back({specs[i].pszName ? specs[i].pszName : L"", specs[i].pszSpec ? specs[i].pszSpec : L"*"});
    return S_OK;
  }
  STDMETHOD(SetFileTypeIndex)(UINT i) override {
    typeIndex_ = i ? i : 1;
    return S_OK;
  }
  STDMETHOD(GetFileTypeIndex)(UINT* out) override {
    if (!out) return E_POINTER;
    *out = typeIndex_;
    return S_OK;
  }
  STDMETHOD(Advise)(IFileDialogEvents* sink, DWORD* cookie) override {
    if (!sink || !cookie) return E_INVALIDARG;
    sink->AddRef();
    sinks_.push_back({++lastCookie_, sink});
    *cookie = lastCookie_;
    return S_OK;
  }
  STDMETHOD(Unadvise)(DWORD cookie) override {
    for (auto it = sinks_.begin(); it != sinks_.end(); ++it)
      if (it->first == cookie) {
        it->second->Release();
        sinks_.erase(it);
        return S_OK;
      }
    return E_INVALIDARG;
  }
  STDMETHOD(SetOptions)(FILEOPENDIALOGOPTIONS fos) override {
    options_ = fos;
    return S_OK;
  }
  STDMETHOD(GetOptions)(FILEOPENDIALOGOPTIONS* out) override {
    if (!out) return E_POINTER;
    *out = options_;
    return S_OK;
  }
  STDMETHOD(SetDefaultFolder)(IShellItem* item) override {
    if (!item) return E_INVALIDARG;
    defaultFolder_ = PathOf(item);
    return S_OK;
  }
  STDMETHOD(SetFolder)(IShellItem* item) override {
    if (!item) return E_INVALIDARG;
    folder_ = PathOf(item);
    return S_OK;
  }
  STDMETHOD(GetFolder)(IShellItem** out) override {
    if (!out) return E_POINTER;
    std::wstring f = CurrentFolder();
    *out = f.empty() ? NULL : new ShellItem(f);
    return *out ? S_OK : E_FAIL;
  }
  STDMETHOD(GetCurrentSelection)(IShellItem** out) override {
    if (!out) return E_POINTER;
    *out = NULL;
    std::wstring name = CurrentName();
    if (name.empty() || name[0] == L'"') return E_FAIL;
    *out = new ShellItem(Resolve(CurrentFolder(), name));
    return S_OK;
  }
  STDMETHOD(SetFileName)(LPCWSTR name) override {
    name_ = name ? name : L"";
    if (dialog_) SetDlgItemTextW(dialog_, edt1, name_.c_str());
    return S_OK;
  }
  STDMETHOD(GetFileName)(LPWSTR* out) override {
    if (!out) return E_POINTER;
    *out = TaskString(CurrentName());
    return *out ? S_OK : E_OUTOFMEMORY;
  }
  STDMETHOD(SetTitle)(LPCWSTR title) override {
    title_ = title ? title : L"";
    if (dialog_) SetWindowTextW(dialog_, title_.c_str());
    return S_OK;
  }
  STDMETHOD(SetOkButtonLabel)(LPCWSTR text) override {
    okLabel_ = text ? text : L"";
    if (dialog_) SetDlgItemTextW(dialog_, IDOK, okLabel_.c_str());
    return S_OK;
  }
  STDMETHOD(SetFileNameLabel)(LPCWSTR text) override {
    nameLabel_ = text ? text : L"";
    if (dialog_) SetDlgItemTextW(dialog_, stc3, nameLabel_.c_str());
    return S_OK;
  }
  STDMETHOD(GetResult)(IShellItem** out) override {
    if (!out) return E_POINTER;
    *out = NULL;
    if (results_.empty()) return E_UNEXPECTED;
    *out = new ShellItem(results_[0]);
    return S_OK;
  }
  STDMETHOD(AddPlace)(IShellItem*, FDAP) override { return S_OK; }
  STDMETHOD(SetDefaultExtension)(LPCWSTR ext) override {
    defExt_ = ext ? ext : L"";
    if (!defExt_.empty() && defExt_[0] == L'.') defExt_.erase(0, 1);
    return S_OK;
  }
  STDMETHOD(Close)(HRESULT hr) override {
    if (!dialog_) return E_UNEXPECTED;
    closeResult_ = hr;
    EndDialog(dialog_, IDCANCEL);
    return S_OK;
  }
  STDMETHOD(SetClientGuid)(REFGUID) override { return S_OK; }
  STDMETHOD(ClearClientData)() override { return S_OK; }
  STDMETHOD(SetFilter)(IShellItemFilter*) override { return E_NOTIMPL; }

  // IFileOpenDialog
  STDMETHOD(GetResults)(IShellItemArray** out) override {
    if (!out) return E_POINTER;
    *out = NULL;
    if (results_.empty()) return E_UNEXPECTED;
    *out = new ShellItemArray(results_);
    return S_OK;
  }
  STDMETHOD(GetSelectedItems)(IShellItemArray** out) override {
    if (!out) return E_POINTER;
    *out = NULL;
    std::vector<std::wstring> paths = Selected();
    if (paths.empty()) return E_FAIL;
    *out = new ShellItemArray(paths);
    return S_OK;
  }

  // IFileSaveDialog: the item it starts on; no properties here.
  STDMETHOD(SetSaveAsItem)(IShellItem* item) override {
    std::wstring p = PathOf(item);
    if (p.empty()) return E_INVALIDARG;
    folder_ = ParentOf(p);
    name_ = Leaf(p);
    return S_OK;
  }
  STDMETHOD(SetProperties)(IPropertyStore*) override { return E_NOTIMPL; }
  STDMETHOD(SetCollectedProperties)(IPropertyDescriptionList*, BOOL) override { return E_NOTIMPL; }
  STDMETHOD(GetProperties)(IPropertyStore** out) override {
    if (out) *out = NULL;
    return E_NOTIMPL;
  }
  STDMETHOD(ApplyProperties)(IShellItem*, IPropertyStore*, HWND, IFileOperationProgressSink*) override {
    return E_NOTIMPL;
  }

  // IOleWindow: the dialog's window while it shows.
  STDMETHOD(GetWindow)(HWND* out) override {
    if (!out) return E_POINTER;
    *out = dialog_;
    return dialog_ ? S_OK : E_FAIL;
  }
  STDMETHOD(ContextSensitiveHelp)(BOOL) override { return E_NOTIMPL; }

  // IFileDialogCustomize
  STDMETHOD(EnableOpenDropDown)(DWORD id) override { return Add(Control::kMenu, id, L"", L""); }
  STDMETHOD(AddMenu)(DWORD id, LPCWSTR label) override { return Add(Control::kMenu, id, label, L""); }
  STDMETHOD(AddPushButton)(DWORD id, LPCWSTR label) override { return Add(Control::kPush, id, label, L""); }
  STDMETHOD(AddComboBox)(DWORD id) override { return Add(Control::kCombo, id, L"", L""); }
  STDMETHOD(AddRadioButtonList)(DWORD id) override { return Add(Control::kRadio, id, L"", L""); }
  STDMETHOD(AddCheckButton)(DWORD id, LPCWSTR label, BOOL checked) override {
    HRESULT hr = Add(Control::kCheck, id, label, L"");
    if (SUCCEEDED(hr)) controls_.back().checked = checked;
    return hr;
  }
  STDMETHOD(AddEditBox)(DWORD id, LPCWSTR text) override { return Add(Control::kEdit, id, L"", text); }
  STDMETHOD(AddSeparator)(DWORD id) override { return Add(Control::kSeparator, id, L"", L""); }
  STDMETHOD(AddText)(DWORD id, LPCWSTR text) override { return Add(Control::kText, id, text, L""); }
  STDMETHOD(SetControlLabel)(DWORD id, LPCWSTR label) override {
    Control* c = Find(id);
    if (!c) return E_INVALIDARG;
    c->label = label ? label : L"";
    if (c->kind == Control::kCheck || c->kind == Control::kPush || c->kind == Control::kText ||
        c->kind == Control::kMenu) {
      if (c->hwnd) SetWindowTextW(c->hwnd, c->label.c_str());
    } else if (c->caption) {
      SetWindowTextW(c->caption, c->label.c_str());
    }
    return S_OK;
  }
  STDMETHOD(GetControlState)(DWORD id, CDCONTROLSTATEF* out) override {
    Control* c = Find(id);
    if (!c || !out) return E_INVALIDARG;
    *out = c->state;
    return S_OK;
  }
  STDMETHOD(SetControlState)(DWORD id, CDCONTROLSTATEF state) override {
    Control* c = Find(id);
    if (!c) return E_INVALIDARG;
    c->state = state;
    ApplyState(*c);
    return S_OK;
  }
  STDMETHOD(GetEditBoxText)(DWORD id, WCHAR** out) override {
    Control* c = Find(id);
    if (!c || c->kind != Control::kEdit || !out) return E_INVALIDARG;
    Read(*c);
    *out = TaskString(c->text);
    return *out ? S_OK : E_OUTOFMEMORY;
  }
  STDMETHOD(SetEditBoxText)(DWORD id, LPCWSTR text) override {
    Control* c = Find(id);
    if (!c || c->kind != Control::kEdit) return E_INVALIDARG;
    c->text = text ? text : L"";
    if (c->hwnd) SetWindowTextW(c->hwnd, c->text.c_str());
    return S_OK;
  }
  STDMETHOD(GetCheckButtonState)(DWORD id, BOOL* out) override {
    Control* c = Find(id);
    if (!c || c->kind != Control::kCheck || !out) return E_INVALIDARG;
    Read(*c);
    *out = c->checked;
    return S_OK;
  }
  STDMETHOD(SetCheckButtonState)(DWORD id, BOOL checked) override {
    Control* c = Find(id);
    if (!c || c->kind != Control::kCheck) return E_INVALIDARG;
    c->checked = checked;
    if (c->hwnd) SendMessageW(c->hwnd, BM_SETCHECK, checked ? BST_CHECKED : BST_UNCHECKED, 0);
    return S_OK;
  }
  STDMETHOD(AddControlItem)(DWORD id, DWORD item, LPCWSTR label) override {
    Control* c = Find(id);
    if (!c || (c->kind != Control::kCombo && c->kind != Control::kRadio && c->kind != Control::kMenu)) return E_INVALIDARG;
    for (const Control::Item& i : c->items)
      if (i.id == item) return E_INVALIDARG;
    c->items.push_back({item, label ? label : L"", CDCS_ENABLEDVISIBLE, NULL});
    if (c->kind == Control::kCombo && c->hwnd) SendMessageW(c->hwnd, CB_ADDSTRING, 0, (LPARAM)c->items.back().label.c_str());
    return S_OK;
  }
  STDMETHOD(RemoveControlItem)(DWORD id, DWORD item) override {
    Control* c = Find(id);
    if (!c) return E_INVALIDARG;
    for (size_t i = 0; i < c->items.size(); i++)
      if (c->items[i].id == item) {
        if (c->kind == Control::kCombo && c->hwnd) SendMessageW(c->hwnd, CB_DELETESTRING, i, 0);
        if (c->items[i].hwnd) DestroyWindow(c->items[i].hwnd);
        c->items.erase(c->items.begin() + i);
        if (c->selected == item) c->selected = (DWORD)-1;
        return S_OK;
      }
    return E_INVALIDARG;
  }
  STDMETHOD(RemoveAllControlItems)(DWORD id) override {
    Control* c = Find(id);
    if (!c) return E_INVALIDARG;
    while (!c->items.empty()) RemoveControlItem(id, c->items.back().id);
    return S_OK;
  }
  STDMETHOD(GetControlItemState)(DWORD id, DWORD item, CDCONTROLSTATEF* out) override {
    Control::Item* i = FindItem(id, item);
    if (!i || !out) return E_INVALIDARG;
    *out = i->state;
    return S_OK;
  }
  STDMETHOD(SetControlItemState)(DWORD id, DWORD item, CDCONTROLSTATEF state) override {
    Control::Item* i = FindItem(id, item);
    if (!i) return E_INVALIDARG;
    i->state = state;
    if (i->hwnd) {
      EnableWindow(i->hwnd, (state & CDCS_ENABLED) != 0);
      ShowWindow(i->hwnd, (state & CDCS_VISIBLE) ? SW_SHOWNA : SW_HIDE);
    }
    return S_OK;
  }
  STDMETHOD(GetSelectedControlItem)(DWORD id, DWORD* out) override {
    Control* c = Find(id);
    if (!c || !out) return E_INVALIDARG;
    Read(*c);
    if (c->selected == (DWORD)-1) return E_FAIL;
    *out = c->selected;
    return S_OK;
  }
  STDMETHOD(SetSelectedControlItem)(DWORD id, DWORD item) override {
    Control* c = Find(id);
    if (!c || !FindItem(id, item)) return E_INVALIDARG;
    c->selected = item;
    Write(*c);
    return S_OK;
  }
  STDMETHOD(StartVisualGroup)(DWORD id, LPCWSTR label) override {
    if (group_ != (DWORD)-1) return E_UNEXPECTED;
    HRESULT hr = Add(Control::kGroup, id, label, L"");
    if (SUCCEEDED(hr)) group_ = id;
    return hr;
  }
  STDMETHOD(EndVisualGroup)() override {
    if (group_ == (DWORD)-1) return E_UNEXPECTED;
    group_ = (DWORD)-1;
    return S_OK;
  }
  STDMETHOD(MakeProminent)(DWORD id) override { return Find(id) ? S_OK : E_INVALIDARG; }
  STDMETHOD(SetControlItemText)(DWORD id, DWORD item, LPCWSTR label) override {
    Control* c = Find(id);
    Control::Item* i = FindItem(id, item);
    if (!c || !i) return E_INVALIDARG;
    i->label = label ? label : L"";
    if (i->hwnd) SetWindowTextW(i->hwnd, i->label.c_str());
    if (c->kind == Control::kCombo && c->hwnd) {
      int at = (int)(i - &c->items[0]);
      LRESULT sel = SendMessageW(c->hwnd, CB_GETCURSEL, 0, 0);
      SendMessageW(c->hwnd, CB_DELETESTRING, at, 0);
      SendMessageW(c->hwnd, CB_INSERTSTRING, at, (LPARAM)i->label.c_str());
      SendMessageW(c->hwnd, CB_SETCURSEL, sel, 0);
    }
    return S_OK;
  }

  // The hook on the dialog's panel, and the window procedure that takes
  // OK when picking folders.
  static UINT_PTR CALLBACK Hook(HWND h, UINT msg, WPARAM wp, LPARAM lp);
  static LRESULT CALLBACK PickProc(HWND h, UINT msg, WPARAM wp, LPARAM lp);

 private:
  virtual ~FileDialog() {
    for (auto& s : sinks_) s.second->Release();
  }

  HRESULT Add(Control::Kind kind, DWORD id, LPCWSTR label, LPCWSTR text) {
    if (Find(id)) return E_INVALIDARG;
    if (dialog_) return E_UNEXPECTED;
    Control c;
    c.kind = kind;
    c.id = id;
    c.label = label ? label : L"";
    c.text = text ? text : L"";
    c.group = kind == Control::kGroup ? (DWORD)-1 : group_;
    controls_.push_back(c);
    return S_OK;
  }
  Control* Find(DWORD id) {
    for (Control& c : controls_)
      if (c.id == id) return &c;
    return NULL;
  }
  Control::Item* FindItem(DWORD id, DWORD item) {
    Control* c = Find(id);
    if (!c) return NULL;
    for (Control::Item& i : c->items)
      if (i.id == item) return &i;
    return NULL;
  }

  std::wstring Resolve(const std::wstring& folder, const std::wstring& name) const {
    if (name.empty()) return folder;
    std::wstring n = Widen(PosixPath(name.c_str()).c_str());
    if (!n.empty() && n[0] == L'/') return Absolute(n);
    return Absolute((folder == L"/" ? L"" : folder) + L"/" + n);
  }
  std::wstring CurrentFolder() {
    if (dialog_) {
      WCHAR buf[MAX_PATH * 4];
      LRESULT n = SendMessageW(dialog_, CDM_GETFOLDERPATH, MAX_PATH * 4, (LPARAM)buf);
      if (n > 0) return buf;
    }
    if (!lastFolder_.empty()) return lastFolder_;
    if (!folder_.empty()) return folder_;
    if (!defaultFolder_.empty()) return defaultFolder_;
    return Absolute(L".");
  }
  std::wstring CurrentName() {
    if (dialog_) {
      int n = GetWindowTextLengthW(GetDlgItem(dialog_, edt1));
      std::wstring s(n + 1, 0);
      GetDlgItemTextW(dialog_, edt1, &s[0], n + 1);
      s.resize(wcslen(s.c_str()));
      return s;
    }
    return name_;
  }
  // The names in the name box, quoted when there are several.
  std::vector<std::wstring> Selected() {
    std::vector<std::wstring> out;
    std::wstring text = CurrentName(), folder = CurrentFolder();
    if (!text.empty() && text[0] == L'"') {
      size_t i = 0;
      while ((i = text.find(L'"', i)) != std::wstring::npos) {
        size_t j = text.find(L'"', i + 1);
        if (j == std::wstring::npos) j = text.size();
        if (j > i + 1) out.push_back(Resolve(folder, text.substr(i + 1, j - i - 1)));
        i = j + 1;
      }
    } else if (!text.empty()) {
      out.push_back(Resolve(folder, text));
    }
    return out;
  }

  // Each sink, told of an event; true when one turns it down.
  template <class F>
  bool Fire(F f) {
    bool refused = false;
    std::vector<IFileDialogEvents*> sinks;
    for (auto& s : sinks_) {
      s.second->AddRef();
      sinks.push_back(s.second);
    }
    for (IFileDialogEvents* s : sinks) {
      if (f(s) == S_FALSE) refused = true;
      s->Release();
    }
    return refused;
  }
  template <class F>
  void FireControl(F f) {
    Fire([&](IFileDialogEvents* s) {
      IFileDialogControlEvents* c = NULL;
      if (SUCCEEDED(s->QueryInterface(IID_IFileDialogControlEvents, (void**)&c)) && c) {
        f(c);
        c->Release();
      }
      return S_OK;
    });
  }

  // The controls' windows, in a column in the panel.
  void CreateControls(HWND panel);
  void ApplyState(Control& c) {
    HWND hs[] = {c.hwnd, c.caption};
    for (HWND w : hs) {
      if (!w) continue;
      EnableWindow(w, (c.state & CDCS_ENABLED) != 0);
      ShowWindow(w, (c.state & CDCS_VISIBLE) ? SW_SHOWNA : SW_HIDE);
    }
    for (Control::Item& i : c.items)
      if (i.hwnd) ShowWindow(i.hwnd, (c.state & CDCS_VISIBLE) && (i.state & CDCS_VISIBLE) ? SW_SHOWNA : SW_HIDE);
  }
  // A control's window into its value, and back.
  void Read(Control& c) {
    switch (c.kind) {
      case Control::kCheck:
        if (c.hwnd) c.checked = SendMessageW(c.hwnd, BM_GETCHECK, 0, 0) == BST_CHECKED;
        break;
      case Control::kEdit:
        if (c.hwnd) {
          int n = GetWindowTextLengthW(c.hwnd);
          std::wstring s(n + 1, 0);
          GetWindowTextW(c.hwnd, &s[0], n + 1);
          s.resize(wcslen(s.c_str()));
          c.text = s;
        }
        break;
      case Control::kCombo:
        if (c.hwnd) {
          LRESULT i = SendMessageW(c.hwnd, CB_GETCURSEL, 0, 0);
          c.selected = i >= 0 && (size_t)i < c.items.size() ? c.items[i].id : (DWORD)-1;
        }
        break;
      case Control::kRadio:
        for (Control::Item& i : c.items)
          if (i.hwnd && SendMessageW(i.hwnd, BM_GETCHECK, 0, 0) == BST_CHECKED) c.selected = i.id;
        break;
      default:
        break;
    }
  }
  void Write(Control& c) {
    if (c.kind == Control::kCombo && c.hwnd) {
      for (size_t i = 0; i < c.items.size(); i++)
        if (c.items[i].id == c.selected) SendMessageW(c.hwnd, CB_SETCURSEL, i, 0);
    } else if (c.kind == Control::kRadio) {
      for (Control::Item& i : c.items)
        if (i.hwnd) SendMessageW(i.hwnd, BM_SETCHECK, i.id == c.selected ? BST_CHECKED : BST_UNCHECKED, 0);
    }
  }
  void OnCommand(HWND panel, int id, int code, HWND from);
  bool FileOk(const std::vector<std::wstring>& results) {
    results_ = results;
    for (Control& c : controls_) Read(c);
    return !Fire([&](IFileDialogEvents* s) { return s->OnFileOk(Self()); });
  }
  IFileDialog* Self() {
    return save_ ? (IFileDialog*)static_cast<IFileSaveDialog*>(this) : (IFileDialog*)static_cast<IFileOpenDialog*>(this);
  }

  LONG refs_ = 1;
  bool save_;
  FILEOPENDIALOGOPTIONS options_ = 0;
  std::vector<std::pair<std::wstring, std::wstring>> types_;
  UINT typeIndex_ = 1;
  std::wstring folder_, defaultFolder_, lastFolder_, name_, title_, okLabel_, nameLabel_, defExt_;
  std::vector<std::pair<DWORD, IFileDialogEvents*>> sinks_;
  DWORD lastCookie_ = 0;
  std::vector<Control> controls_;
  DWORD group_ = (DWORD)-1;
  std::vector<std::wstring> results_;
  HWND dialog_ = NULL, panel_ = NULL;
  HRESULT closeResult_ = S_OK;
};

void FileDialog::CreateControls(HWND panel) {
  HFONT font = (HFONT)SendMessageW(GetParent(panel), WM_GETFONT, 0, 0);
  RECT pr;
  GetClientRect(GetParent(panel), &pr);
  const int margin = 10, row = 24;
  int width = pr.right - 2 * margin, y = 6, next = kFirstControlId;
  auto make = [&](LPCWSTR cls, LPCWSTR text, DWORD style, int x, int w, int h, Control* c, int item) {
    HWND win = CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style, x, y, w, h, panel, (HMENU)(INT_PTR)next++,
                               NULL, NULL);
    if (font) SendMessageW(win, WM_SETFONT, (WPARAM)font, FALSE);
    SetPropW(win, L"winport.filedialog.control", (HANDLE)(INT_PTR)(c - &controls_[0] + 1));
    SetPropW(win, L"winport.filedialog.item", (HANDLE)(INT_PTR)(item + 1));
    return win;
  };
  for (Control& c : controls_) {
    int x = margin + (c.group != (DWORD)-1 ? 12 : 0), w = width - (x - margin);
    // A control with a label of its own takes the label's room at the left.
    int labelled = 0;
    if ((c.kind == Control::kEdit || c.kind == Control::kCombo) && !c.label.empty()) {
      c.caption = make(L"Static", c.label.c_str(), SS_LEFT, x, 90, 16, &c, -1);
      labelled = 94;
    }
    switch (c.kind) {
      case Control::kCheck:
        c.hwnd = make(L"Button", c.label.c_str(), BS_AUTOCHECKBOX | WS_TABSTOP, x, w, 18, &c, -1);
        SendMessageW(c.hwnd, BM_SETCHECK, c.checked ? BST_CHECKED : BST_UNCHECKED, 0);
        break;
      case Control::kEdit:
        c.hwnd = make(L"Edit", c.text.c_str(), WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP, x + labelled, w - labelled, 20,
                      &c, -1);
        break;
      case Control::kText:
        c.hwnd = make(L"Static", c.label.c_str(), SS_LEFT, x, w, 18, &c, -1);
        break;
      case Control::kGroup:
        c.hwnd = make(L"Static", c.label.c_str(), SS_LEFT, x, w, 18, &c, -1);
        break;
      case Control::kPush:
      case Control::kMenu:
        c.hwnd = make(L"Button", c.label.empty() ? L"..." : c.label.c_str(), BS_PUSHBUTTON | WS_TABSTOP, x, 110, 22,
                      &c, -1);
        break;
      case Control::kSeparator:
        c.hwnd = make(L"Static", L"", SS_ETCHEDHORZ, x, w, 2, &c, -1);
        y -= row - 8;
        break;
      case Control::kCombo:
        c.hwnd = make(L"ComboBox", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, x + labelled, w - labelled, 160, &c,
                      -1);
        for (const Control::Item& i : c.items) SendMessageW(c.hwnd, CB_ADDSTRING, 0, (LPARAM)i.label.c_str());
        Write(c);
        break;
      case Control::kRadio:
        for (size_t i = 0; i < c.items.size(); i++) {
          if (i) y += row - 4;
          c.items[i].hwnd = make(L"Button", c.items[i].label.c_str(),
                                 BS_AUTORADIOBUTTON | WS_TABSTOP | (i == 0 ? WS_GROUP : 0), x, w, 18, &c, (int)i);
        }
        Write(c);
        break;
    }
    ApplyState(c);
    y += row;
  }
  RECT r;
  GetWindowRect(panel, &r);
  SetWindowPos(panel, NULL, 0, 0, pr.right, controls_.empty() ? 0 : y + 4, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}

void FileDialog::OnCommand(HWND panel, int, int code, HWND from) {
  INT_PTR ci = (INT_PTR)GetPropW(from, L"winport.filedialog.control");
  INT_PTR ii = (INT_PTR)GetPropW(from, L"winport.filedialog.item");
  if (ci <= 0 || (size_t)ci > controls_.size()) return;
  Control& c = controls_[ci - 1];
  IFileDialogCustomize* self = this;
  switch (c.kind) {
    case Control::kCheck:
      if (code == BN_CLICKED) {
        Read(c);
        FireControl([&](IFileDialogControlEvents* e) { e->OnCheckButtonToggled(self, c.id, c.checked); });
      }
      break;
    case Control::kPush:
      if (code == BN_CLICKED) FireControl([&](IFileDialogControlEvents* e) { e->OnButtonClicked(self, c.id); });
      break;
    case Control::kMenu:
      if (code == BN_CLICKED) {
        FireControl([&](IFileDialogControlEvents* e) { e->OnControlActivating(self, c.id); });
        HMENU menu = CreatePopupMenu();
        for (size_t i = 0; i < c.items.size(); i++)
          if (c.items[i].state & CDCS_VISIBLE)
            AppendMenuW(menu, MF_STRING | ((c.items[i].state & CDCS_ENABLED) ? 0 : MF_GRAYED), i + 1,
                        c.items[i].label.c_str());
        RECT r;
        GetWindowRect(from, &r);
        UINT pick = (UINT)TrackPopupMenu(menu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN, r.left, r.bottom, 0, panel,
                                         NULL);
        DestroyMenu(menu);
        if (pick >= 1 && pick <= c.items.size()) {
          DWORD item = c.items[pick - 1].id;
          FireControl([&](IFileDialogControlEvents* e) { e->OnItemSelected(self, c.id, item); });
        }
      }
      break;
    case Control::kCombo:
      if (code == CBN_SELCHANGE) {
        Read(c);
        DWORD item = c.selected;
        FireControl([&](IFileDialogControlEvents* e) { e->OnItemSelected(self, c.id, item); });
      } else if (code == CBN_DROPDOWN) {
        FireControl([&](IFileDialogControlEvents* e) { e->OnControlActivating(self, c.id); });
      }
      break;
    case Control::kRadio:
      if (code == BN_CLICKED && ii > 0 && (size_t)ii <= c.items.size()) {
        c.selected = c.items[ii - 1].id;
        DWORD item = c.selected;
        FireControl([&](IFileDialogControlEvents* e) { e->OnItemSelected(self, c.id, item); });
      }
      break;
    case Control::kEdit:
      if (code == EN_CHANGE) Read(c);
      break;
    default:
      break;
  }
}

UINT_PTR CALLBACK FileDialog::Hook(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
  FileDialog* d;
  if (msg == WM_INITDIALOG) {
    d = (FileDialog*)((OPENFILENAMEW*)lp)->lCustData;
    SetPropW(h, kSelfProp, (HANDLE)d);
    d->panel_ = h;
    d->dialog_ = GetParent(h);
    SetPropW(d->dialog_, kSelfProp, (HANDLE)d);
    if (!d->okLabel_.empty()) SetDlgItemTextW(d->dialog_, IDOK, d->okLabel_.c_str());
    if (!d->nameLabel_.empty()) SetDlgItemTextW(d->dialog_, stc3, d->nameLabel_.c_str());
    if (d->options_ & FOS_PICKFOLDERS) {
      SetDlgItemTextW(d->dialog_, stc3, d->nameLabel_.empty() ? L"Fol&der:" : d->nameLabel_.c_str());
      if (d->okLabel_.empty()) SetDlgItemTextW(d->dialog_, IDOK, L"Select Folder");
      LONG_PTR old = SetWindowLongPtrW(d->dialog_, GWLP_WNDPROC, (LONG_PTR)PickProc);
      SetPropW(d->dialog_, kOldProcProp, (HANDLE)old);
    }
    d->CreateControls(h);
    return TRUE;
  }
  d = (FileDialog*)GetPropW(h, kSelfProp);
  if (!d) return FALSE;
  switch (msg) {
    case WM_COMMAND:
      if (lp) d->OnCommand(h, LOWORD(wp), HIWORD(wp), (HWND)lp);
      return FALSE;
    case WM_NOTIFY: {
      OFNOTIFYW* n = (OFNOTIFYW*)lp;
      IFileDialog* self = d->Self();
      switch (n->hdr.code) {
        case CDN_FOLDERCHANGE:
          d->lastFolder_ = d->CurrentFolder();
          d->Fire([&](IFileDialogEvents* s) { return s->OnFolderChange(self); });
          break;
        case CDN_SELCHANGE:
          d->Fire([&](IFileDialogEvents* s) { return s->OnSelectionChange(self); });
          break;
        case CDN_TYPECHANGE:
          d->typeIndex_ = n->lpOFN->nFilterIndex;
          d->Fire([&](IFileDialogEvents* s) { return s->OnTypeChange(self); });
          break;
        case CDN_FILEOK: {
          // The answer as the dialog has written it: one path, or the
          // folder and then each name.
          std::vector<std::wstring> results;
          const wchar_t* f = n->lpOFN->lpstrFile;
          std::wstring first = f;
          const wchar_t* next = f + first.size() + 1;
          if ((d->options_ & FOS_ALLOWMULTISELECT) && *next) {
            for (; *next; next += wcslen(next) + 1) results.push_back(d->Resolve(first, next));
          } else {
            results.push_back(Absolute(first));
          }
          d->typeIndex_ = n->lpOFN->nFilterIndex;
          bool ok = !(d->options_ & FOS_PICKFOLDERS) || IsFolder(results[0]);
          if (ok) ok = d->FileOk(results);
          SetWindowLongPtrW(h, DWLP_MSGRESULT, ok ? 0 : 1);
          return TRUE;
        }
      }
      return FALSE;
    }
    case WM_DESTROY:
      for (Control& c : d->controls_) {
        d->Read(c);
        c.hwnd = c.caption = NULL;
        for (Control::Item& i : c.items) i.hwnd = NULL;
      }
      d->lastFolder_ = d->CurrentFolder();
      d->name_ = d->CurrentName();
      RemovePropW(h, kSelfProp);
      return FALSE;
  }
  return FALSE;
}

// Picking folders: OK answers with the folder typed, or the one the
// dialog is in when nothing is.
LRESULT CALLBACK FileDialog::PickProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
  WNDPROC old = (WNDPROC)GetPropW(h, kOldProcProp);
  FileDialog* d = (FileDialog*)GetPropW(h, kSelfProp);
  if (msg == WM_COMMAND && LOWORD(wp) == IDOK && d) {
    std::wstring folder = d->CurrentFolder(), text = d->CurrentName();
    while (!text.empty() && text.back() == L' ') text.pop_back();
    std::wstring path = d->Resolve(folder, text);
    if (IsFolder(path)) {
      if (d->FileOk(std::vector<std::wstring>(1, path))) EndDialog(h, IDOK);
      return 0;
    }
  }
  if (msg == WM_NCDESTROY) {
    SetWindowLongPtrW(h, GWLP_WNDPROC, (LONG_PTR)old);
    RemovePropW(h, kOldProcProp);
    RemovePropW(h, kSelfProp);
  }
  return CallWindowProcW(old, h, msg, wp, lp);
}

HRESULT FileDialog::Show(HWND owner) {
  if (dialog_) return E_UNEXPECTED;
  results_.clear();
  closeResult_ = S_OK;

  OPENFILENAMEW ofn;
  memset(&ofn, 0, sizeof ofn);
  ofn.lStructSize = sizeof ofn;
  ofn.hwndOwner = owner;
  std::vector<WCHAR> file(32768, 0);
  std::wstring start = options_ & FOS_PICKFOLDERS ? std::wstring() : name_;
  wcsncpy(&file[0], start.c_str(), file.size() - 1);
  ofn.lpstrFile = &file[0];
  ofn.nMaxFile = (DWORD)file.size();
  std::wstring filter;
  if (options_ & FOS_PICKFOLDERS) {
    // Only folders show.
    filter = std::wstring(L"Folders") + L'\0' + L"\x01" + L'\0';
  } else {
    for (auto& t : types_) filter += t.first + L'\0' + t.second + L'\0';
  }
  if (!filter.empty()) {
    filter += L'\0';
    ofn.lpstrFilter = filter.c_str();
    ofn.nFilterIndex = options_ & FOS_PICKFOLDERS ? 1 : std::min<UINT>(typeIndex_, (UINT)types_.size());
  }
  std::wstring folder = !folder_.empty() ? folder_ : !lastFolder_.empty() ? lastFolder_ : defaultFolder_;
  if (!folder.empty()) ofn.lpstrInitialDir = folder.c_str();
  if (!title_.empty()) ofn.lpstrTitle = title_.c_str();
  if (!defExt_.empty()) ofn.lpstrDefExt = defExt_.c_str();
  static const DWORD map[][2] = {
      {FOS_ALLOWMULTISELECT, OFN_ALLOWMULTISELECT}, {FOS_FILEMUSTEXIST, OFN_FILEMUSTEXIST},
      {FOS_PATHMUSTEXIST, OFN_PATHMUSTEXIST},       {FOS_OVERWRITEPROMPT, OFN_OVERWRITEPROMPT},
      {FOS_CREATEPROMPT, OFN_CREATEPROMPT},         {FOS_NOCHANGEDIR, OFN_NOCHANGEDIR},
      {FOS_NOVALIDATE, OFN_NOVALIDATE},             {FOS_NOREADONLYRETURN, OFN_NOREADONLYRETURN},
      {FOS_NOTESTFILECREATE, OFN_NOTESTFILECREATE}, {FOS_SHAREAWARE, OFN_SHAREAWARE},
      {FOS_FORCESHOWHIDDEN, OFN_FORCESHOWHIDDEN},   {FOS_NODEREFERENCELINKS, OFN_NODEREFERENCELINKS},
  };
  ofn.Flags = OFN_EXPLORER | OFN_ENABLEHOOK | OFN_HIDEREADONLY | OFN_ENABLESIZING;
  for (auto& m : map)
    if (options_ & m[0]) ofn.Flags |= m[1];
  if (options_ & FOS_PICKFOLDERS) ofn.Flags &= ~(OFN_FILEMUSTEXIST | OFN_ALLOWMULTISELECT);
  ofn.lpfnHook = (LPOFNHOOKPROC)Hook;
  ofn.lCustData = (LPARAM)this;

  AddRef();
  BOOL ok = save_ ? GetSaveFileNameW(&ofn) : GetOpenFileNameW(&ofn);
  dialog_ = panel_ = NULL;
  if (ofn.nFilterIndex && !(options_ & FOS_PICKFOLDERS)) typeIndex_ = ofn.nFilterIndex;
  HRESULT hr = ok && !results_.empty() ? S_OK : FAILED(closeResult_) ? closeResult_ : HRESULT_FROM_WIN32(ERROR_CANCELLED);
  if (FAILED(hr)) results_.clear();
  Release();
  return hr;
}

class Factory : public IClassFactory {
 public:
  explicit Factory(bool save) : save_(save) {}
  STDMETHOD(QueryInterface)(REFIID riid, void** out) override {
    if (!out) return E_POINTER;
    if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_IClassFactory)) {
      *out = static_cast<IClassFactory*>(this);
      return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
  }
  // Static, so not counted.
  STDMETHOD_(ULONG, AddRef)() override { return 2; }
  STDMETHOD_(ULONG, Release)() override { return 1; }
  STDMETHOD(CreateInstance)(IUnknown* outer, REFIID riid, void** out) override {
    if (!out) return E_POINTER;
    *out = NULL;
    if (outer) return CLASS_E_NOAGGREGATION;
    FileDialog* d = new FileDialog(save_);
    HRESULT hr = d->QueryInterface(riid, out);
    d->Release();
    return hr;
  }
  STDMETHOD(LockServer)(BOOL) override { return S_OK; }

 private:
  bool save_;
};

}  // namespace

namespace winport {
namespace ole {

HRESULT GetShellClassObject(REFCLSID clsid, REFIID riid, void** out) {
  static Factory open(false), save(true);
  *out = NULL;
  if (IsEqualCLSID(clsid, CLSID_FileOpenDialog)) return open.QueryInterface(riid, out);
  if (IsEqualCLSID(clsid, CLSID_FileSaveDialog)) return save.QueryInterface(riid, out);
  return REGDB_E_CLASSNOTREG;
}

}  // namespace ole
}  // namespace winport

extern "C" HRESULT WINAPI SHCreateItemFromParsingName(PCWSTR path, IBindCtx*, REFIID riid, void** out) {
  if (!out) return E_POINTER;
  *out = NULL;
  if (!path || !*path) return E_INVALIDARG;
  std::wstring p = Absolute(path);
  if (!Exists(p)) return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
  ShellItem* item = new ShellItem(p);
  HRESULT hr = item->QueryInterface(riid, out);
  item->Release();
  return hr;
}
