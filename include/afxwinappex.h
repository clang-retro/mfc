// afxwinappex.h - CWinAppEx, the Feature Pack's application: its settings
// under the application's registry key ("Workspace" by default, see
// SetRegistryBase), read and written by type with GetInt, WriteString,
// GetObject and the rest (GetSection... for a subkey), the main window's
// placement saved and restored, and the managers it starts on request
// (context menus, keyboard, mouse, tooltips, the shell).
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#ifndef __AFXWINAPPEX_H__
#define __AFXWINAPPEX_H__

#ifndef __AFXWIN_H__
#include <afxwin.h>
#endif
#include <afxsettingsstore.h>
#include <afxcmn.h>
#include <shlobj.h>

class CContextMenuManager;
class CKeyboardManager;
class CMouseManager;
class CShellManager;
class CTooltipManager;
class CFrameImpl;

class CWinAppEx : public CWinApp {
  DECLARE_DYNAMIC(CWinAppEx)
public:
  explicit CWinAppEx(BOOL bResourceSmartUpdate = FALSE);
  virtual ~CWinAppEx();

  virtual int ExitInstance();

  // ----- The managers.
  BOOL InitMouseManager();
  BOOL InitContextMenuManager();
  BOOL InitKeyboardManager();
  BOOL InitShellManager();
  BOOL InitTooltipManager();

  CMouseManager* GetMouseManager();
  CContextMenuManager* GetContextMenuManager();
  CKeyboardManager* GetKeyboardManager();
  CShellManager* GetShellManager();
  CTooltipManager* GetTooltipManager();

  // ----- The settings' key: Software\<key>\<application>\<base>.
  LPCTSTR SetRegistryBase(LPCTSTR lpszSectionName = NULL);
  LPCTSTR GetRegistryBase() { return m_strRegSection; }
  CString GetRegSectionPath(LPCTSTR szSectionAdd = _T(""));

  int GetInt(LPCTSTR lpszEntry, int nDefault = 0);
  CString GetString(LPCTSTR lpszEntry, LPCTSTR lpszDefault = _T(""));
  BOOL GetBinary(LPCTSTR lpszEntry, LPBYTE* ppData, UINT* pBytes);
  BOOL GetObject(LPCTSTR lpszEntry, CObject& obj);
  BOOL WriteInt(LPCTSTR lpszEntry, int nValue);
  BOOL WriteString(LPCTSTR lpszEntry, LPCTSTR lpszValue);
  BOOL WriteBinary(LPCTSTR lpszEntry, LPBYTE pData, UINT nBytes);
  BOOL WriteObject(LPCTSTR lpszEntry, CObject& obj);

  int GetSectionInt(LPCTSTR lpszSubSection, LPCTSTR lpszEntry, int nDefault = 0);
  CString GetSectionString(LPCTSTR lpszSubSection, LPCTSTR lpszEntry, LPCTSTR lpszDefault = _T(""));
  BOOL GetSectionBinary(LPCTSTR lpszSubSection, LPCTSTR lpszEntry, LPBYTE* ppData, UINT* pBytes);
  BOOL GetSectionObject(LPCTSTR lpszSubSection, LPCTSTR lpszEntry, CObject& obj);
  BOOL WriteSectionInt(LPCTSTR lpszSubSection, LPCTSTR lpszEntry, int nValue);
  BOOL WriteSectionString(LPCTSTR lpszSubSection, LPCTSTR lpszEntry, LPCTSTR lpszValue);
  BOOL WriteSectionBinary(LPCTSTR lpszSubSection, LPCTSTR lpszEntry, LPBYTE pData, UINT nBytes);
  BOOL WriteSectionObject(LPCTSTR lpszSubSection, LPCTSTR lpszEntry, CObject& obj);

  // ----- The state: what the application keeps between runs.
  BOOL IsStateExists(LPCTSTR lpszSectionName);
  BOOL DeleteState(LPCTSTR lpszSectionName = NULL);
  BOOL CleanState(LPCTSTR lpszSectionName = NULL);
  BOOL LoadState(CFrameWnd* pFrame, LPCTSTR lpszSectionName = NULL);
  BOOL SaveState(CFrameWnd* pFrame, LPCTSTR lpszSectionName = NULL);
  virtual void LoadCustomState() {}
  virtual void SaveCustomState() {}
  virtual void PreLoadState() {}
  virtual void PreSaveState() {}
  virtual void OnClosingMainFrame(CFrameImpl* pFrameImpl);

  // The main window's placement, kept in the "WindowPlacement" subkey.
  virtual BOOL LoadWindowPlacement(CRect& rectNormalPosition, int& nFlags, int& nShowCmd);
  virtual BOOL StoreWindowPlacement(const CRect& rectNormalPosition, int nFlags, int nShowCmd);
  virtual BOOL ReloadWindowPlacement(CFrameWnd* pFrame);
  void EnableLoadWindowPlacement(BOOL bEnable = TRUE) { m_bLoadWindowPlacement = bEnable; }

  int GetDataVersionMajor() const { return m_iSavedVersionMajor; }
  int GetDataVersionMinor() const { return m_iSavedVersionMinor; }
  int GetDataVersion() const;

  BOOL IsResourceSmartUpdate() const { return m_bResourceSmartUpdate; }
  void EnableUserTools(UINT, UINT, CRuntimeClass* = NULL, UINT = 0, UINT = 0) {}
  void EnableHtmlHelp() {}
  void EnableTearOffMenus(LPCTSTR, UINT, UINT) {}
  BOOL ShowPopupMenu(UINT uiMenuResId, const CPoint& point, CWnd* pWnd);
  virtual BOOL OnViewDoubleClick(CWnd* /*pWnd*/, int /*iViewId*/) { return FALSE; }
  virtual BOOL OnWorkspaceIdle(CWnd* /*pWnd*/) { return FALSE; }

  BOOL m_bSaveState;
  BOOL m_bForceImageReset;
  UINT m_nAppLook;
  BOOL m_bHiColorIcons;

protected:
  virtual void OnAppContextHelp(CWnd* /*pWndControl*/, const DWORD /*dwHelpIDArray*/[]) {}
  virtual BOOL LoadWindowPlacementFrom(LPCTSTR lpszSection, CRect& rect, int& nFlags, int& nShowCmd);

  CString m_strRegSection;
  BOOL m_bResourceSmartUpdate;
  BOOL m_bLoadWindowPlacement;
  BOOL m_bKeyboardManagerAutocreated, m_bContextMenuManagerAutocreated, m_bMouseManagerAutocreated;
  BOOL m_bShellManagerAutocreated, m_bTooltipManagerAutocreated;
  int m_iSavedVersionMajor, m_iSavedVersionMinor;

private:
  CString _SectionPath(LPCTSTR lpszSubSection);
};

// The registry path of a part of the application's settings:
// Software\<key>\<application>\<lpszPostFix>\ (lpszProfileName, when
// given, in place of the application's name).
CString AFXAPI AFXGetRegPath(LPCTSTR lpszPostFix, LPCTSTR lpszProfileName = NULL);

// ---------------------------------------------------------------------
// The managers.

// Context menus by resource and by name, shown at a point.
class CContextMenuManager : public CObject {
public:
  CContextMenuManager();
  virtual ~CContextMenuManager();

  BOOL AddMenu(UINT uiMenuNameResId, UINT uiMenuResId);
  BOOL AddMenu(LPCTSTR lpszName, UINT uiMenuResId);
  HMENU GetMenuById(UINT nMenuResId) const;
  HMENU GetMenuByName(LPCTSTR lpszName, UINT* puiOrigResID = NULL) const;
  void GetMenuNames(CStringList& listOfNames) const;

  // The menu's first popup at (x, y); the command goes to pWndOwner
  // (bOwnMessage) or comes back from TrackPopupMenu.
  virtual BOOL ShowPopupMenu(UINT uiMenuResId, int x, int y, CWnd* pWndOwner, BOOL bOwnMessage = FALSE,
                             BOOL bRightAlign = FALSE);
  virtual BOOL ShowPopupMenu(HMENU hmenuPopup, int x, int y, CWnd* pWndOwner, BOOL bOwnMessage = FALSE,
                             BOOL bAutoDestroy = TRUE, BOOL bRightAlign = FALSE);
  virtual UINT TrackPopupMenu(HMENU hmenuPopup, int x, int y, CWnd* pWndOwner, BOOL bRightAlign = FALSE);

  virtual BOOL LoadState(LPCTSTR /*lpszProfileName*/ = NULL) { return TRUE; }
  virtual BOOL SaveState(LPCTSTR /*lpszProfileName*/ = NULL) { return TRUE; }
  virtual BOOL ResetState();

  void SetDontCloseActiveMenu(BOOL bSet = TRUE) { m_bDontCloseActiveMenu = bSet; }

protected:
  CMap<UINT, UINT, HMENU, HMENU> m_Menus;
  CMap<CString, LPCTSTR, HMENU, HMENU> m_MenuNames;
  BOOL m_bDontCloseActiveMenu;
};

// Accelerators: the application's default table and its keys' names.
class CKeyboardManager : public CObject {
public:
  CKeyboardManager();
  virtual ~CKeyboardManager();

  BOOL LoadState(LPCTSTR lpszProfileName = NULL, CFrameWnd* pDefaultFrame = NULL);
  BOOL SaveState(LPCTSTR lpszProfileName = NULL, CFrameWnd* pDefaultFrame = NULL);
  void ResetAll();

  // The first key of the frame's accelerators (or the application's)
  // that sends uiCmd.
  static BOOL AFXAPI FindDefaultAccelerator(UINT uiCmd, CString& str, CFrameWnd* pWndFrame, BOOL bIsDefaultFrame);
  static BOOL AFXAPI IsKeyPrintable(const UINT nChar);
  static UINT AFXAPI TranslateCharToUpper(const UINT nChar);
  static BOOL AFXAPI IsKeyHandled(WORD nKey, BYTE fVirt, CFrameWnd* pWndFrame, BOOL bIsDefaultFrame);
  static void AFXAPI ShowAllAccelerators(BOOL bShowAll = TRUE, LPCTSTR lpszDelimiter = _T("; "));
  static BOOL AFXAPI IsShowAllAccelerators() { return m_bAllAccelerators; }
  static CString AFXAPI GetKeyName(const ACCEL& accel);

  static BOOL m_bAllAccelerators;
  static CString m_strDelimiter;
};

// Mouse double-clicks on views, by view.
class CMouseManager : public CObject {
public:
  CMouseManager() {}
  virtual ~CMouseManager() {}
  BOOL AddView(int iViewId, UINT uiViewNameResId, UINT uiIconId = 0);
  BOOL AddView(int iViewId, LPCTSTR lpszViewName, UINT uiIconId = 0);
  UINT GetViewDblClickCommand(int iId) const;
  void SetCommandForDblClk(int iViewId, UINT uiCmd);
  int GetViewIdByName(LPCTSTR lpszName) const;
  void GetViewNames(CStringList& listOfNames) const;
  BOOL LoadState(LPCTSTR = NULL) { return TRUE; }
  BOOL SaveState(LPCTSTR = NULL) { return TRUE; }

protected:
  CMap<CString, LPCTSTR, int, int> m_ViewsNames;
  CMap<int, int, UINT, UINT> m_ViewsToCommands;
};

// Shell items: browsing for a folder and ITEMIDLIST helpers.
struct AFX_SHELLITEMINFO {
  IShellFolder* pParentFolder;
  LPITEMIDLIST pidlFQ;
  LPITEMIDLIST pidlRel;
  AFX_SHELLITEMINFO() : pParentFolder(NULL), pidlFQ(NULL), pidlRel(NULL) {}
};
typedef AFX_SHELLITEMINFO* LPAFX_SHELLITEMINFO;

class CShellManager : public CObject {
public:
  CShellManager();
  virtual ~CShellManager();

  BOOL BrowseForFolder(CString& strOutFolder, CWnd* pWndParent = NULL, LPCTSTR lplszInitialFolder = NULL,
                       LPCTSTR lpszTitle = NULL, UINT ulFlags = BIF_RETURNONLYFSDIRS, LPINT piFolderImage = NULL);
  LPITEMIDLIST CreateItem(UINT cbSize);
  void FreeItem(LPITEMIDLIST pidl);
  UINT GetItemCount(LPCITEMIDLIST pidl);
  UINT GetItemSize(LPCITEMIDLIST pidl);
  LPITEMIDLIST CopyItem(LPCITEMIDLIST pidlSource);
  LPITEMIDLIST GetNextItem(LPCITEMIDLIST pidl);
  int GetParentItem(LPCITEMIDLIST lpidl, LPITEMIDLIST& lpidlParent);
  LPITEMIDLIST ConcatenateItem(LPCITEMIDLIST pidl1, LPCITEMIDLIST pidl2);
  HRESULT ItemFromPath(LPCTSTR lpszPath, LPITEMIDLIST& pidl);

protected:
  static int CALLBACK BrowseCallbackProc(HWND hwnd, UINT uMsg, LPARAM lParam, LPARAM lpData);
  LPMALLOC m_pMalloc;
  CString m_strInitialPath;
};

// Tooltips of the kinds the Feature Pack's windows ask for.
#define AFX_TOOLTIP_TYPE_DEFAULT 0x0001
#define AFX_TOOLTIP_TYPE_TOOLBAR 0x0002
#define AFX_TOOLTIP_TYPE_TAB 0x0004
#define AFX_TOOLTIP_TYPE_MINIFRAME 0x0008
#define AFX_TOOLTIP_TYPE_DOCKBAR 0x0010
#define AFX_TOOLTIP_TYPE_EDIT 0x0020
#define AFX_TOOLTIP_TYPE_BUTTON 0x0040
#define AFX_TOOLTIP_TYPE_TOOLBOX 0x0080
#define AFX_TOOLTIP_TYPE_PLANNER 0x0100
#define AFX_TOOLTIP_TYPE_RIBBON 0x0200
#define AFX_TOOLTIP_TYPE_CAPTIONBAR 0x0400
#define AFX_TOOLTIP_TYPE_ALL 0xFFFF

class CTooltipManager : public CObject {
public:
  CTooltipManager();
  virtual ~CTooltipManager();

  void SetTooltipParams(UINT nTypes, CRuntimeClass* pRTC = NULL, void* pParams = NULL);
  void SetTooltipText(TOOLINFO* pTI, CToolTipCtrl* pToolTip, UINT nType, const CString strText,
                      LPCTSTR lpszDescr = NULL);
  static BOOL AFXAPI CreateToolTip(CToolTipCtrl*& pToolTip, CWnd* pWndParent, UINT nType);
  static void AFXAPI DeleteToolTip(CToolTipCtrl*& pToolTip);

protected:
  CRuntimeClass* m_rtc[16];
};

extern CContextMenuManager* afxContextMenuManager;
extern CKeyboardManager* afxKeyboardManager;
extern CMouseManager* afxMouseManager;
extern CShellManager* afxShellManager;
extern CTooltipManager* afxTooltipManager;

#endif  // __AFXWINAPPEX_H__
