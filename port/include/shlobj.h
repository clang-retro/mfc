// shlobj.h - the shell's objects: the files of a drop (CF_HDROP and
// WM_DROPFILES), the standard folders, the shell namespace of folders and
// item ID lists, and the folder browser (SHBrowseForFolder).
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#ifndef _WINPORT_SHLOBJ_H
#define _WINPORT_SHLOBJ_H

#include <windows.h>
#include <shellapi.h>
#include <shtypes.h>
#include <objidl.h>

#ifdef __cplusplus
extern "C" {
#endif

// An HDROP's memory: this header, then the paths from pFiles bytes in,
// each ending with a NUL and the list with another; wide (WCHAR) when
// fWide is set.
typedef struct _DROPFILES {
  DWORD pFiles;
  POINT pt;
  BOOL fNC;
  BOOL fWide;
} DROPFILES, *LPDROPFILES;

// The standard folders, by CSIDL. On Linux they are the XDG folders
// (~/.config, ~/.local/share, ~/.cache and the user directories in
// ~/.config/user-dirs.dirs) and the home folder; on macOS the folders
// under ~/Library and the home folder.
#define CSIDL_DESKTOP 0x0000
#define CSIDL_PROGRAMS 0x0002
#define CSIDL_PERSONAL 0x0005
#define CSIDL_MYDOCUMENTS CSIDL_PERSONAL
#define CSIDL_FAVORITES 0x0006
#define CSIDL_STARTUP 0x0007
#define CSIDL_RECENT 0x0008
#define CSIDL_SENDTO 0x0009
#define CSIDL_STARTMENU 0x000b
#define CSIDL_MYMUSIC 0x000d
#define CSIDL_MYVIDEO 0x000e
#define CSIDL_DESKTOPDIRECTORY 0x0010
#define CSIDL_FONTS 0x0014
#define CSIDL_TEMPLATES 0x0015
#define CSIDL_APPDATA 0x001a
#define CSIDL_LOCAL_APPDATA 0x001c
#define CSIDL_INTERNET_CACHE 0x0020
#define CSIDL_COOKIES 0x0021
#define CSIDL_HISTORY 0x0022
#define CSIDL_COMMON_APPDATA 0x0023
#define CSIDL_WINDOWS 0x0024
#define CSIDL_SYSTEM 0x0025
#define CSIDL_PROGRAM_FILES 0x0026
#define CSIDL_MYPICTURES 0x0027
#define CSIDL_PROFILE 0x0028
#define CSIDL_COMMON_DOCUMENTS 0x002e
#define CSIDL_FLAG_CREATE 0x8000
#define CSIDL_FLAG_DONT_VERIFY 0x4000
#define CSIDL_FLAG_MASK 0xff00
#define SHGFP_TYPE_CURRENT 0
#define SHGFP_TYPE_DEFAULT 1
HRESULT WINAPI SHGetFolderPathA(HWND, int, HANDLE, DWORD, LPSTR);
HRESULT WINAPI SHGetFolderPathW(HWND, int, HANDLE, DWORD, LPWSTR);
BOOL WINAPI SHGetSpecialFolderPathA(HWND, LPSTR, int, BOOL);
BOOL WINAPI SHGetSpecialFolderPathW(HWND, LPWSTR, int, BOOL);
#define SHGetFolderPath __WINPORT_AW(SHGetFolderPath)
#define SHGetSpecialFolderPath __WINPORT_AW(SHGetSpecialFolderPath)

// The shell namespace. Here the desktop folder is the file system's root
// "/", and an item ID list names a path under it: one item for each path
// component, holding the component in UTF-8. The empty list is the
// desktop itself.
extern const IID IID_IShellFolder, IID_IEnumIDList, IID_IShellLinkA, IID_IShellLinkW;
// shlguid.h's: the shell's shortcut object.
extern const CLSID CLSID_ShellLink;

typedef DWORD SHCONTF;
#define SHCONTF_CHECKING_FOR_CHILDREN 0x0010
#define SHCONTF_FOLDERS 0x0020
#define SHCONTF_NONFOLDERS 0x0040
#define SHCONTF_INCLUDEHIDDEN 0x0080
#define SHCONTF_INIT_ON_FIRST_NEXT 0x0100
#define SHCONTF_NETPRINTERSRCH 0x0200
#define SHCONTF_SHAREABLE 0x0400
#define SHCONTF_STORAGE 0x0800
#define SHCONTF_FASTITEMS 0x2000
#define SHCONTF_FLATLIST 0x4000
#define SHCONTF_ENABLE_ASYNC 0x8000

typedef DWORD SHGDNF;
#define SHGDN_NORMAL 0x0000
#define SHGDN_INFOLDER 0x0001
#define SHGDN_FOREDITING 0x1000
#define SHGDN_FORADDRESSBAR 0x4000
#define SHGDN_FORPARSING 0x8000

typedef ULONG SFGAOF;
#define SFGAO_CANCOPY 0x00000001L
#define SFGAO_CANMOVE 0x00000002L
#define SFGAO_CANLINK 0x00000004L
#define SFGAO_STORAGE 0x00000008L
#define SFGAO_CANRENAME 0x00000010L
#define SFGAO_CANDELETE 0x00000020L
#define SFGAO_HASPROPSHEET 0x00000040L
#define SFGAO_DROPTARGET 0x00000100L
#define SFGAO_CAPABILITYMASK 0x00000177L
#define SFGAO_ENCRYPTED 0x00002000L
#define SFGAO_ISSLOW 0x00004000L
#define SFGAO_GHOSTED 0x00008000L
#define SFGAO_LINK 0x00010000L
#define SFGAO_SHARE 0x00020000L
#define SFGAO_READONLY 0x00040000L
#define SFGAO_HIDDEN 0x00080000L
#define SFGAO_DISPLAYATTRMASK 0x000FC000L
#define SFGAO_NONENUMERATED 0x00100000L
#define SFGAO_NEWCONTENT 0x00200000L
#define SFGAO_STREAM 0x00400000L
#define SFGAO_STORAGEANCESTOR 0x00800000L
#define SFGAO_VALIDATE 0x01000000L
#define SFGAO_REMOVABLE 0x02000000L
#define SFGAO_COMPRESSED 0x04000000L
#define SFGAO_BROWSABLE 0x08000000L
#define SFGAO_FILESYSANCESTOR 0x10000000L
#define SFGAO_FOLDER 0x20000000L
#define SFGAO_FILESYSTEM 0x40000000L
#define SFGAO_HASSUBFOLDER 0x80000000L
#define SFGAO_CONTENTSMASK 0x80000000L

// The folder browser.
typedef int(CALLBACK* BFFCALLBACK)(HWND hwnd, UINT uMsg, LPARAM lParam, LPARAM lpData);
typedef struct _browseinfoA {
  HWND hwndOwner;
  PCIDLIST_ABSOLUTE pidlRoot;
  LPSTR pszDisplayName;
  LPCSTR lpszTitle;
  UINT ulFlags;
  BFFCALLBACK lpfn;
  LPARAM lParam;
  int iImage;
} BROWSEINFOA, *PBROWSEINFOA, *LPBROWSEINFOA;
typedef struct _browseinfoW {
  HWND hwndOwner;
  PCIDLIST_ABSOLUTE pidlRoot;
  LPWSTR pszDisplayName;
  LPCWSTR lpszTitle;
  UINT ulFlags;
  BFFCALLBACK lpfn;
  LPARAM lParam;
  int iImage;
} BROWSEINFOW, *PBROWSEINFOW, *LPBROWSEINFOW;
#define BIF_RETURNONLYFSDIRS 0x00000001
#define BIF_DONTGOBELOWDOMAIN 0x00000002
#define BIF_STATUSTEXT 0x00000004
#define BIF_RETURNFSANCESTORS 0x00000008
#define BIF_EDITBOX 0x00000010
#define BIF_VALIDATE 0x00000020
#define BIF_NEWDIALOGSTYLE 0x00000040
#define BIF_USENEWUI (BIF_NEWDIALOGSTYLE | BIF_EDITBOX)
#define BIF_BROWSEINCLUDEURLS 0x00000080
#define BIF_UAHINT 0x00000100
#define BIF_NONEWFOLDERBUTTON 0x00000200
#define BIF_NOTRANSLATETARGETS 0x00000400
#define BIF_BROWSEFORCOMPUTER 0x00001000
#define BIF_BROWSEFORPRINTER 0x00002000
#define BIF_BROWSEINCLUDEFILES 0x00004000
#define BIF_SHAREABLE 0x00008000
#define BIF_BROWSEFILEJUNCTIONS 0x00010000
// Notifications to the callback.
#define BFFM_INITIALIZED 1
#define BFFM_SELCHANGED 2
#define BFFM_VALIDATEFAILEDA 3
#define BFFM_VALIDATEFAILEDW 4
#define BFFM_IUNKNOWN 5
// Messages the callback sends the browser.
#define BFFM_SETSTATUSTEXTA (WM_USER + 100)
#define BFFM_ENABLEOK (WM_USER + 101)
#define BFFM_SETSELECTIONA (WM_USER + 102)
#define BFFM_SETSELECTIONW (WM_USER + 103)
#define BFFM_SETSTATUSTEXTW (WM_USER + 104)
#define BFFM_SETOKTEXT (WM_USER + 105)
#define BFFM_SETEXPANDED (WM_USER + 106)

HRESULT WINAPI SHGetMalloc(IMalloc** ppMalloc);
HRESULT WINAPI SHGetDesktopFolder(struct IShellFolder** ppshf);
// The network address control's class (msctls_netaddress), always there.
BOOL WINAPI InitNetworkAddressControl(void);

PIDLIST_ABSOLUTE WINAPI SHBrowseForFolderA(LPBROWSEINFOA lpbi);
PIDLIST_ABSOLUTE WINAPI SHBrowseForFolderW(LPBROWSEINFOW lpbi);
BOOL WINAPI SHGetPathFromIDListA(PCIDLIST_ABSOLUTE pidl, LPSTR pszPath);
BOOL WINAPI SHGetPathFromIDListW(PCIDLIST_ABSOLUTE pidl, LPWSTR pszPath);
HRESULT WINAPI SHGetSpecialFolderLocation(HWND hwnd, int csidl, PIDLIST_ABSOLUTE* ppidl);
HRESULT WINAPI SHGetFolderLocation(HWND hwnd, int csidl, HANDLE hToken, DWORD dwFlags, PIDLIST_ABSOLUTE* ppidl);
HRESULT WINAPI SHParseDisplayName(LPCWSTR pszName, IBindCtx* pbc, PIDLIST_ABSOLUTE* ppidl, SFGAOF sfgaoIn,
                                  SFGAOF* psfgaoOut);
HRESULT WINAPI SHBindToParent(PCIDLIST_ABSOLUTE pidl, REFIID riid, void** ppv, PCUITEMID_CHILD* ppidlLast);
UINT WINAPI ILGetSize(PCUIDLIST_RELATIVE pidl);
PIDLIST_RELATIVE WINAPI ILGetNext(PCUIDLIST_RELATIVE pidl);
PIDLIST_RELATIVE WINAPI ILClone(PCUIDLIST_RELATIVE pidl);
PITEMID_CHILD WINAPI ILCloneFirst(PCUIDLIST_RELATIVE pidl);
PIDLIST_ABSOLUTE WINAPI ILCombine(PCIDLIST_ABSOLUTE pidl1, PCUIDLIST_RELATIVE pidl2);
PIDLIST_RELATIVE WINAPI ILFindChild(PCIDLIST_ABSOLUTE pidlParent, PCIDLIST_ABSOLUTE pidlChild);
PITEMID_CHILD WINAPI ILFindLastID(PCUIDLIST_RELATIVE pidl);
BOOL WINAPI ILRemoveLastID(PIDLIST_RELATIVE pidl);
BOOL WINAPI ILIsEqual(PCIDLIST_ABSOLUTE pidl1, PCIDLIST_ABSOLUTE pidl2);
BOOL WINAPI ILIsParent(PCIDLIST_ABSOLUTE pidl1, PCIDLIST_ABSOLUTE pidl2, BOOL fImmediate);
void WINAPI ILFree(PIDLIST_RELATIVE pidl);
PIDLIST_ABSOLUTE WINAPI ILCreateFromPathA(LPCSTR pszPath);
PIDLIST_ABSOLUTE WINAPI ILCreateFromPathW(LPCWSTR pszPath);
#define ILIsEmpty(pidl) ((pidl) == NULL || (pidl)->mkid.cb == 0)
#define ILIsChild(pidl) (!ILIsEmpty(pidl) && ILIsEmpty(ILGetNext(pidl)))

#define BROWSEINFO __WINPORT_AW(BROWSEINFO)
#define PBROWSEINFO __WINPORT_AW(PBROWSEINFO)
#define LPBROWSEINFO __WINPORT_AW(LPBROWSEINFO)
#define SHBrowseForFolder __WINPORT_AW(SHBrowseForFolder)
#define SHGetPathFromIDList __WINPORT_AW(SHGetPathFromIDList)
#define ILCreateFromPath __WINPORT_AW(ILCreateFromPath)
#define IShellLink __WINPORT_AW(IShellLink)
#define IID_IShellLink __WINPORT_AW(IID_IShellLink)
#ifdef UNICODE
#define BFFM_SETSTATUSTEXT BFFM_SETSTATUSTEXTW
#define BFFM_SETSELECTION BFFM_SETSELECTIONW
#define BFFM_VALIDATEFAILED BFFM_VALIDATEFAILEDW
#else
#define BFFM_SETSTATUSTEXT BFFM_SETSTATUSTEXTA
#define BFFM_SETSELECTION BFFM_SETSELECTIONA
#define BFFM_VALIDATEFAILED BFFM_VALIDATEFAILEDA
#endif

#ifdef __cplusplus
}  // extern "C"
#endif

#ifdef __cplusplus
interface IEnumIDList : public IUnknown {
  STDMETHOD(Next)(ULONG celt, PITEMID_CHILD* rgelt, ULONG* pceltFetched) = 0;
  STDMETHOD(Skip)(ULONG celt) = 0;
  STDMETHOD(Reset)() = 0;
  STDMETHOD(Clone)(IEnumIDList** ppenum) = 0;
};
#else
typedef struct IEnumIDList IEnumIDList;
#endif
typedef IEnumIDList* LPENUMIDLIST;

#ifdef __cplusplus
interface IShellFolder : public IUnknown {
  STDMETHOD(ParseDisplayName)(HWND hwnd, IBindCtx* pbc, LPWSTR pszDisplayName, ULONG* pchEaten,
                              PIDLIST_RELATIVE* ppidl, ULONG* pdwAttributes) = 0;
  STDMETHOD(EnumObjects)(HWND hwnd, SHCONTF grfFlags, IEnumIDList** ppenumIDList) = 0;
  STDMETHOD(BindToObject)(PCUIDLIST_RELATIVE pidl, IBindCtx* pbc, REFIID riid, void** ppv) = 0;
  STDMETHOD(BindToStorage)(PCUIDLIST_RELATIVE pidl, IBindCtx* pbc, REFIID riid, void** ppv) = 0;
  STDMETHOD(CompareIDs)(LPARAM lParam, PCUIDLIST_RELATIVE pidl1, PCUIDLIST_RELATIVE pidl2) = 0;
  STDMETHOD(CreateViewObject)(HWND hwndOwner, REFIID riid, void** ppv) = 0;
  STDMETHOD(GetAttributesOf)(UINT cidl, PCUITEMID_CHILD_ARRAY apidl, SFGAOF* rgfInOut) = 0;
  STDMETHOD(GetUIObjectOf)(HWND hwndOwner, UINT cidl, PCUITEMID_CHILD_ARRAY apidl, REFIID riid, UINT* rgfReserved,
                           void** ppv) = 0;
  STDMETHOD(GetDisplayNameOf)(PCUITEMID_CHILD pidl, SHGDNF uFlags, STRRET* pName) = 0;
  STDMETHOD(SetNameOf)(HWND hwnd, PCUITEMID_CHILD pidl, LPCWSTR pszName, SHGDNF uFlags,
                       PITEMID_CHILD* ppidlOut) = 0;
};
#else
typedef struct IShellFolder IShellFolder;
#endif
typedef IShellFolder* LPSHELLFOLDER;
#define SHCIDS_ALLFIELDS 0x80000000L
#define SHCIDS_CANONICALONLY 0x10000000L
#define SHCIDS_COLUMNMASK 0x0000FFFFL

// A shortcut (CLSID_ShellLink): a .lnk file, in Windows' format. A .lnk
// file in a folder is a link (SFGAO_LINK); symbolic links are followed by
// the system itself.
#define SLR_NO_UI 0x0001
#define SLR_ANY_MATCH 0x0002
#define SLR_UPDATE 0x0004
#define SLR_NOUPDATE 0x0008
#define SLGP_SHORTPATH 0x0001
#define SLGP_UNCPRIORITY 0x0002
#define SLGP_RAWPATH 0x0004
#ifdef __cplusplus
interface IShellLinkA : public IUnknown {
  STDMETHOD(GetPath)(LPSTR pszFile, int cch, WIN32_FIND_DATAA* pfd, DWORD fFlags) = 0;
  STDMETHOD(GetIDList)(PIDLIST_ABSOLUTE* ppidl) = 0;
  STDMETHOD(SetIDList)(PCIDLIST_ABSOLUTE pidl) = 0;
  STDMETHOD(GetDescription)(LPSTR pszName, int cch) = 0;
  STDMETHOD(SetDescription)(LPCSTR pszName) = 0;
  STDMETHOD(GetWorkingDirectory)(LPSTR pszDir, int cch) = 0;
  STDMETHOD(SetWorkingDirectory)(LPCSTR pszDir) = 0;
  STDMETHOD(GetArguments)(LPSTR pszArgs, int cch) = 0;
  STDMETHOD(SetArguments)(LPCSTR pszArgs) = 0;
  STDMETHOD(GetHotkey)(WORD* pwHotkey) = 0;
  STDMETHOD(SetHotkey)(WORD wHotkey) = 0;
  STDMETHOD(GetShowCmd)(int* piShowCmd) = 0;
  STDMETHOD(SetShowCmd)(int iShowCmd) = 0;
  STDMETHOD(GetIconLocation)(LPSTR pszIconPath, int cch, int* piIcon) = 0;
  STDMETHOD(SetIconLocation)(LPCSTR pszIconPath, int iIcon) = 0;
  STDMETHOD(SetRelativePath)(LPCSTR pszPathRel, DWORD dwReserved) = 0;
  STDMETHOD(Resolve)(HWND hwnd, DWORD fFlags) = 0;
  STDMETHOD(SetPath)(LPCSTR pszFile) = 0;
};
#else
typedef struct IShellLinkA IShellLinkA;
#endif
#ifdef __cplusplus
interface IShellLinkW : public IUnknown {
  STDMETHOD(GetPath)(LPWSTR pszFile, int cch, WIN32_FIND_DATAW* pfd, DWORD fFlags) = 0;
  STDMETHOD(GetIDList)(PIDLIST_ABSOLUTE* ppidl) = 0;
  STDMETHOD(SetIDList)(PCIDLIST_ABSOLUTE pidl) = 0;
  STDMETHOD(GetDescription)(LPWSTR pszName, int cch) = 0;
  STDMETHOD(SetDescription)(LPCWSTR pszName) = 0;
  STDMETHOD(GetWorkingDirectory)(LPWSTR pszDir, int cch) = 0;
  STDMETHOD(SetWorkingDirectory)(LPCWSTR pszDir) = 0;
  STDMETHOD(GetArguments)(LPWSTR pszArgs, int cch) = 0;
  STDMETHOD(SetArguments)(LPCWSTR pszArgs) = 0;
  STDMETHOD(GetHotkey)(WORD* pwHotkey) = 0;
  STDMETHOD(SetHotkey)(WORD wHotkey) = 0;
  STDMETHOD(GetShowCmd)(int* piShowCmd) = 0;
  STDMETHOD(SetShowCmd)(int iShowCmd) = 0;
  STDMETHOD(GetIconLocation)(LPWSTR pszIconPath, int cch, int* piIcon) = 0;
  STDMETHOD(SetIconLocation)(LPCWSTR pszIconPath, int iIcon) = 0;
  STDMETHOD(SetRelativePath)(LPCWSTR pszPathRel, DWORD dwReserved) = 0;
  STDMETHOD(Resolve)(HWND hwnd, DWORD fFlags) = 0;
  STDMETHOD(SetPath)(LPCWSTR pszFile) = 0;
};
#else
typedef struct IShellLinkW IShellLinkW;
#endif

WINPORT_UUIDOF(IEnumIDList)
WINPORT_UUIDOF(IShellFolder)
WINPORT_UUIDOF(IShellLinkA)
WINPORT_UUIDOF(IShellLinkW)

#include <shobjidl.h>

#endif
