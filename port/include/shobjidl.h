// shobjidl.h - the shell's items (IShellItem, IShellItemArray) and the
// Vista-style file dialog: IFileOpenDialog, IFileSaveDialog, the events
// they send and the controls a program adds to them.
//
// Here the dialog is the layer's own file dialog, made to look and answer
// as Windows' does; a shell item is a path in the file system.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#ifndef _WINPORT_SHOBJIDL_H
#define _WINPORT_SHOBJIDL_H

#include <windows.h>
#include <objidl.h>
#include <oleidl.h>
#include <shtypes.h>

#ifdef __cplusplus
interface IShellItem;
#else
typedef struct IShellItem IShellItem;
#endif
#ifdef __cplusplus
interface IShellItemArray;
#else
typedef struct IShellItemArray IShellItemArray;
#endif
#ifdef __cplusplus
interface IEnumShellItems;
#else
typedef struct IEnumShellItems IEnumShellItems;
#endif
#ifdef __cplusplus
interface IShellItemFilter;
#else
typedef struct IShellItemFilter IShellItemFilter;
#endif
#ifdef __cplusplus
interface IPropertyStore;
#else
typedef struct IPropertyStore IPropertyStore;
#endif
#ifdef __cplusplus
interface IPropertyDescriptionList;
#else
typedef struct IPropertyDescriptionList IPropertyDescriptionList;
#endif
#ifdef __cplusplus
interface IFileOperationProgressSink;
#else
typedef struct IFileOperationProgressSink IFileOperationProgressSink;
#endif
#ifdef __cplusplus
interface IFileDialog;
#else
typedef struct IFileDialog IFileDialog;
#endif
#ifdef __cplusplus
interface IFileDialogEvents;
#else
typedef struct IFileDialogEvents IFileDialogEvents;
#endif
#ifdef __cplusplus
interface IFileDialogCustomize;
#else
typedef struct IFileDialogCustomize IFileDialogCustomize;
#endif
#ifdef __cplusplus
interface IFileDialogControlEvents;
#else
typedef struct IFileDialogControlEvents IFileDialogControlEvents;
#endif

#ifdef __cplusplus
extern "C" {
#endif

extern const IID IID_IShellItem, IID_IShellItemArray, IID_IModalWindow, IID_IFileDialog, IID_IFileOpenDialog,
    IID_IFileSaveDialog, IID_IFileDialogEvents, IID_IFileDialogCustomize, IID_IFileDialogControlEvents;
extern const CLSID CLSID_FileOpenDialog, CLSID_FileSaveDialog;

#ifndef __WINPORT_SFGAOF_DEFINED
#define __WINPORT_SFGAOF_DEFINED
typedef ULONG SFGAOF;
#endif

typedef enum _SIGDN {
  SIGDN_NORMALDISPLAY = 0x00000000,
  SIGDN_PARENTRELATIVEPARSING = (int)0x80018001,
  SIGDN_DESKTOPABSOLUTEPARSING = (int)0x80028000,
  SIGDN_PARENTRELATIVEEDITING = (int)0x80031001,
  SIGDN_DESKTOPABSOLUTEEDITING = (int)0x8004c000,
  SIGDN_FILESYSPATH = (int)0x80058000,
  SIGDN_URL = (int)0x80068000,
  SIGDN_PARENTRELATIVEFORADDRESSBAR = (int)0x8007c001,
  SIGDN_PARENTRELATIVE = (int)0x80080001,
  SIGDN_PARENTRELATIVEFORUI = (int)0x80094001
} SIGDN;

enum _SICHINTF {
  SICHINT_DISPLAY = 0,
  SICHINT_ALLFIELDS = (int)0x80000000,
  SICHINT_CANONICAL = 0x10000000,
  SICHINT_TEST_FILESYSPATH_IF_NOT_EQUAL = 0x20000000
};
typedef DWORD SICHINTF;

typedef enum SIATTRIBFLAGS {
  SIATTRIBFLAGS_AND = 0x1,
  SIATTRIBFLAGS_OR = 0x2,
  SIATTRIBFLAGS_APPCOMPAT = 0x3,
  SIATTRIBFLAGS_MASK = 0x3,
  SIATTRIBFLAGS_ALLITEMS = 0x4000
} SIATTRIBFLAGS;

typedef enum GETPROPERTYSTOREFLAGS { GPS_DEFAULT = 0 } GETPROPERTYSTOREFLAGS;

#ifndef __WINPORT_PROPERTYKEY_DEFINED
#define __WINPORT_PROPERTYKEY_DEFINED
typedef struct _tagpropertykey {
  GUID fmtid;
  DWORD pid;
} PROPERTYKEY;
#define REFPROPERTYKEY const PROPERTYKEY&
#endif

typedef struct _COMDLG_FILTERSPEC {
  LPCWSTR pszName;
  LPCWSTR pszSpec;
} COMDLG_FILTERSPEC;

enum _FILEOPENDIALOGOPTIONS {
  FOS_OVERWRITEPROMPT = 0x2,
  FOS_STRICTFILETYPES = 0x4,
  FOS_NOCHANGEDIR = 0x8,
  FOS_PICKFOLDERS = 0x20,
  FOS_FORCEFILESYSTEM = 0x40,
  FOS_ALLNONSTORAGEITEMS = 0x80,
  FOS_NOVALIDATE = 0x100,
  FOS_ALLOWMULTISELECT = 0x200,
  FOS_PATHMUSTEXIST = 0x800,
  FOS_FILEMUSTEXIST = 0x1000,
  FOS_CREATEPROMPT = 0x2000,
  FOS_SHAREAWARE = 0x4000,
  FOS_NOREADONLYRETURN = 0x8000,
  FOS_NOTESTFILECREATE = 0x10000,
  FOS_HIDEMRUPLACES = 0x20000,
  FOS_HIDEPINNEDPLACES = 0x40000,
  FOS_NODEREFERENCELINKS = 0x100000,
  FOS_OKBUTTONNEEDSINTERACTION = 0x200000,
  FOS_DONTADDTORECENT = 0x2000000,
  FOS_FORCESHOWHIDDEN = 0x10000000,
  FOS_DEFAULTNOMINIMODE = 0x20000000,
  FOS_FORCEPREVIEWPANEON = 0x40000000,
  FOS_SUPPORTSTREAMABLEITEMS = (int)0x80000000
};
typedef DWORD FILEOPENDIALOGOPTIONS;

typedef enum FDAP { FDAP_BOTTOM = 0, FDAP_TOP = 1 } FDAP;
typedef enum FDE_SHAREVIOLATION_RESPONSE {
  FDESVR_DEFAULT = 0,
  FDESVR_ACCEPT = 1,
  FDESVR_REFUSE = 2
} FDE_SHAREVIOLATION_RESPONSE;
typedef enum FDE_OVERWRITE_RESPONSE { FDEOR_DEFAULT = 0, FDEOR_ACCEPT = 1, FDEOR_REFUSE = 2 } FDE_OVERWRITE_RESPONSE;

#ifndef __WINPORT_CDCONTROLSTATEF_DEFINED
#define __WINPORT_CDCONTROLSTATEF_DEFINED
typedef enum CDCONTROLSTATEF {
  CDCS_INACTIVE = 0x0,
  CDCS_ENABLED = 0x1,
  CDCS_VISIBLE = 0x2,
  CDCS_ENABLEDVISIBLE = 0x3
} CDCONTROLSTATEF;
#endif

// A shell item for a path, a file or a folder.
HRESULT WINAPI SHCreateItemFromParsingName(PCWSTR pszPath, IBindCtx* pbc, REFIID riid, void** ppv);

#ifdef __cplusplus
}  // extern "C"
#endif

#ifdef __cplusplus
interface IShellItem : public IUnknown {
  STDMETHOD(BindToHandler)(IBindCtx * pbc, REFGUID bhid, REFIID riid, void** ppv) = 0;
  STDMETHOD(GetParent)(IShellItem * *ppsi) = 0;
  STDMETHOD(GetDisplayName)(SIGDN sigdnName, LPWSTR * ppszName) = 0;
  STDMETHOD(GetAttributes)(SFGAOF sfgaoMask, SFGAOF * psfgaoAttribs) = 0;
  STDMETHOD(Compare)(IShellItem * psi, SICHINTF hint, int* piOrder) = 0;
};
#else
typedef struct IShellItem IShellItem;
#endif

#ifdef __cplusplus
interface IShellItemArray : public IUnknown {
  STDMETHOD(BindToHandler)(IBindCtx * pbc, REFGUID bhid, REFIID riid, void** ppvOut) = 0;
  STDMETHOD(GetPropertyStore)(GETPROPERTYSTOREFLAGS flags, REFIID riid, void** ppv) = 0;
  STDMETHOD(GetPropertyDescriptionList)(REFPROPERTYKEY keyType, REFIID riid, void** ppv) = 0;
  STDMETHOD(GetAttributes)(SIATTRIBFLAGS AttribFlags, SFGAOF sfgaoMask, SFGAOF * psfgaoAttribs) = 0;
  STDMETHOD(GetCount)(DWORD * pdwNumItems) = 0;
  STDMETHOD(GetItemAt)(DWORD dwIndex, IShellItem * *ppsi) = 0;
  STDMETHOD(EnumItems)(IEnumShellItems * *ppenumShellItems) = 0;
};
#else
typedef struct IShellItemArray IShellItemArray;
#endif

#ifdef __cplusplus
interface IModalWindow : public IUnknown {
  STDMETHOD(Show)(HWND hwndOwner) = 0;
};
#else
typedef struct IModalWindow IModalWindow;
#endif

#ifdef __cplusplus
interface IFileDialog : public IModalWindow {
  STDMETHOD(SetFileTypes)(UINT cFileTypes, const COMDLG_FILTERSPEC* rgFilterSpec) = 0;
  STDMETHOD(SetFileTypeIndex)(UINT iFileType) = 0;
  STDMETHOD(GetFileTypeIndex)(UINT * piFileType) = 0;
  STDMETHOD(Advise)(IFileDialogEvents * pfde, DWORD * pdwCookie) = 0;
  STDMETHOD(Unadvise)(DWORD dwCookie) = 0;
  STDMETHOD(SetOptions)(FILEOPENDIALOGOPTIONS fos) = 0;
  STDMETHOD(GetOptions)(FILEOPENDIALOGOPTIONS * pfos) = 0;
  STDMETHOD(SetDefaultFolder)(IShellItem * psi) = 0;
  STDMETHOD(SetFolder)(IShellItem * psi) = 0;
  STDMETHOD(GetFolder)(IShellItem * *ppsi) = 0;
  STDMETHOD(GetCurrentSelection)(IShellItem * *ppsi) = 0;
  STDMETHOD(SetFileName)(LPCWSTR pszName) = 0;
  STDMETHOD(GetFileName)(LPWSTR * pszName) = 0;
  STDMETHOD(SetTitle)(LPCWSTR pszTitle) = 0;
  STDMETHOD(SetOkButtonLabel)(LPCWSTR pszText) = 0;
  STDMETHOD(SetFileNameLabel)(LPCWSTR pszLabel) = 0;
  STDMETHOD(GetResult)(IShellItem * *ppsi) = 0;
  STDMETHOD(AddPlace)(IShellItem * psi, FDAP fdap) = 0;
  STDMETHOD(SetDefaultExtension)(LPCWSTR pszDefaultExtension) = 0;
  STDMETHOD(Close)(HRESULT hr) = 0;
  STDMETHOD(SetClientGuid)(REFGUID guid) = 0;
  STDMETHOD(ClearClientData)() = 0;
  STDMETHOD(SetFilter)(IShellItemFilter * pFilter) = 0;
};
#else
typedef struct IFileDialog IFileDialog;
#endif

#ifdef __cplusplus
interface IFileOpenDialog : public IFileDialog {
  STDMETHOD(GetResults)(IShellItemArray * *ppenum) = 0;
  STDMETHOD(GetSelectedItems)(IShellItemArray * *ppsai) = 0;
};
#else
typedef struct IFileOpenDialog IFileOpenDialog;
#endif

#ifdef __cplusplus
interface IFileSaveDialog : public IFileDialog {
  STDMETHOD(SetSaveAsItem)(IShellItem * psi) = 0;
  STDMETHOD(SetProperties)(IPropertyStore * pStore) = 0;
  STDMETHOD(SetCollectedProperties)(IPropertyDescriptionList * pList, BOOL fAppendDefault) = 0;
  STDMETHOD(GetProperties)(IPropertyStore * *ppStore) = 0;
  STDMETHOD(ApplyProperties)(IShellItem * psi, IPropertyStore * pStore, HWND hwnd,
                             IFileOperationProgressSink * pSink) = 0;
};
#else
typedef struct IFileSaveDialog IFileSaveDialog;
#endif

#ifdef __cplusplus
interface IFileDialogEvents : public IUnknown {
  STDMETHOD(OnFileOk)(IFileDialog * pfd) = 0;
  STDMETHOD(OnFolderChanging)(IFileDialog * pfd, IShellItem * psiFolder) = 0;
  STDMETHOD(OnFolderChange)(IFileDialog * pfd) = 0;
  STDMETHOD(OnSelectionChange)(IFileDialog * pfd) = 0;
  STDMETHOD(OnShareViolation)(IFileDialog * pfd, IShellItem * psi, FDE_SHAREVIOLATION_RESPONSE * pResponse) = 0;
  STDMETHOD(OnTypeChange)(IFileDialog * pfd) = 0;
  STDMETHOD(OnOverwrite)(IFileDialog * pfd, IShellItem * psi, FDE_OVERWRITE_RESPONSE * pResponse) = 0;
};
#else
typedef struct IFileDialogEvents IFileDialogEvents;
#endif

#ifdef __cplusplus
interface IFileDialogCustomize : public IUnknown {
  STDMETHOD(EnableOpenDropDown)(DWORD dwIDCtl) = 0;
  STDMETHOD(AddMenu)(DWORD dwIDCtl, LPCWSTR pszLabel) = 0;
  STDMETHOD(AddPushButton)(DWORD dwIDCtl, LPCWSTR pszLabel) = 0;
  STDMETHOD(AddComboBox)(DWORD dwIDCtl) = 0;
  STDMETHOD(AddRadioButtonList)(DWORD dwIDCtl) = 0;
  STDMETHOD(AddCheckButton)(DWORD dwIDCtl, LPCWSTR pszLabel, BOOL bChecked) = 0;
  STDMETHOD(AddEditBox)(DWORD dwIDCtl, LPCWSTR pszText) = 0;
  STDMETHOD(AddSeparator)(DWORD dwIDCtl) = 0;
  STDMETHOD(AddText)(DWORD dwIDCtl, LPCWSTR pszText) = 0;
  STDMETHOD(SetControlLabel)(DWORD dwIDCtl, LPCWSTR pszLabel) = 0;
  STDMETHOD(GetControlState)(DWORD dwIDCtl, CDCONTROLSTATEF * pdwState) = 0;
  STDMETHOD(SetControlState)(DWORD dwIDCtl, CDCONTROLSTATEF dwState) = 0;
  STDMETHOD(GetEditBoxText)(DWORD dwIDCtl, WCHAR * *ppszText) = 0;
  STDMETHOD(SetEditBoxText)(DWORD dwIDCtl, LPCWSTR pszText) = 0;
  STDMETHOD(GetCheckButtonState)(DWORD dwIDCtl, BOOL * pbChecked) = 0;
  STDMETHOD(SetCheckButtonState)(DWORD dwIDCtl, BOOL bChecked) = 0;
  STDMETHOD(AddControlItem)(DWORD dwIDCtl, DWORD dwIDItem, LPCWSTR pszLabel) = 0;
  STDMETHOD(RemoveControlItem)(DWORD dwIDCtl, DWORD dwIDItem) = 0;
  STDMETHOD(RemoveAllControlItems)(DWORD dwIDCtl) = 0;
  STDMETHOD(GetControlItemState)(DWORD dwIDCtl, DWORD dwIDItem, CDCONTROLSTATEF * pdwState) = 0;
  STDMETHOD(SetControlItemState)(DWORD dwIDCtl, DWORD dwIDItem, CDCONTROLSTATEF dwState) = 0;
  STDMETHOD(GetSelectedControlItem)(DWORD dwIDCtl, DWORD * pdwIDItem) = 0;
  STDMETHOD(SetSelectedControlItem)(DWORD dwIDCtl, DWORD dwIDItem) = 0;
  STDMETHOD(StartVisualGroup)(DWORD dwIDCtl, LPCWSTR pszLabel) = 0;
  STDMETHOD(EndVisualGroup)() = 0;
  STDMETHOD(MakeProminent)(DWORD dwIDCtl) = 0;
  STDMETHOD(SetControlItemText)(DWORD dwIDCtl, DWORD dwIDItem, LPCWSTR pszLabel) = 0;
};
#else
typedef struct IFileDialogCustomize IFileDialogCustomize;
#endif

#ifdef __cplusplus
interface IFileDialogControlEvents : public IUnknown {
  STDMETHOD(OnItemSelected)(IFileDialogCustomize * pfdc, DWORD dwIDCtl, DWORD dwIDItem) = 0;
  STDMETHOD(OnButtonClicked)(IFileDialogCustomize * pfdc, DWORD dwIDCtl) = 0;
  STDMETHOD(OnCheckButtonToggled)(IFileDialogCustomize * pfdc, DWORD dwIDCtl, BOOL bChecked) = 0;
  STDMETHOD(OnControlActivating)(IFileDialogCustomize * pfdc, DWORD dwIDCtl) = 0;
};
#else
typedef struct IFileDialogControlEvents IFileDialogControlEvents;
#endif

WINPORT_UUIDOF(IShellItem)
WINPORT_UUIDOF(IShellItemArray)
WINPORT_UUIDOF(IModalWindow)
WINPORT_UUIDOF(IFileDialog)
WINPORT_UUIDOF(IFileOpenDialog)
WINPORT_UUIDOF(IFileSaveDialog)
WINPORT_UUIDOF(IFileDialogEvents)
WINPORT_UUIDOF(IFileDialogCustomize)
WINPORT_UUIDOF(IFileDialogControlEvents)

#endif
