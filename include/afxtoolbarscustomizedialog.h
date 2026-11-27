// afxtoolbarscustomizedialog.h - CMFCToolBarsCustomizeDialog, the
// Customize window of the Feature Pack's toolbars: it lists the frame's
// toolbars to show or hide, makes, renames and deletes the user's own
// toolbars, turns text labels on, and resets a toolbar to its resource.
// It is modeless: Create shows it, and it deletes itself when closed.
// The commands it is told about (AddButton, AddMenu, AddToolBar) are kept
// by category for the program to ask about.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#ifndef __AFXTOOLBARSCUSTOMIZEDIALOG_H__
#define __AFXTOOLBARSCUSTOMIZEDIALOG_H__

#include <afxtoolbar.h>

class CMFCToolBarsCustomizeDialog : public CDialog {
  DECLARE_DYNAMIC(CMFCToolBarsCustomizeDialog)
public:
  enum {
    AFX_CUSTOMIZE_MENU_SHADOWS = 0x0001,
    AFX_CUSTOMIZE_TEXT_LABELS = 0x0002,
    AFX_CUSTOMIZE_MENU_ANIMATIONS = 0x0004,
    AFX_CUSTOMIZE_NOHELP = 0x0008,
    AFX_CUSTOMIZE_CONTEXT_HELP = 0x0010,
    AFX_CUSTOMIZE_NOTOOLS = 0x0020,
    AFX_CUSTOMIZE_MENUAMPERS = 0x0040,
    AFX_CUSTOMIZE_NO_LARGE_ICONS = 0x0080,
  };

  CMFCToolBarsCustomizeDialog(CFrameWnd* pWndParentFrame, BOOL bAutoSetFromMenus = FALSE,
                              UINT uiFlags = (AFX_CUSTOMIZE_MENU_SHADOWS | AFX_CUSTOMIZE_TEXT_LABELS |
                                              AFX_CUSTOMIZE_MENU_ANIMATIONS | AFX_CUSTOMIZE_NOHELP),
                              CList<CRuntimeClass*, CRuntimeClass*>* p_lstCustomPages = NULL);
  virtual ~CMFCToolBarsCustomizeDialog();

  virtual BOOL Create();
  void EnableUserDefinedToolbars(BOOL bEnable = TRUE) { m_bUserDefinedToolbars = bEnable; }
  void EnableTools(CObList*) {}

  // ----- The commands, by category.
  void AddButton(UINT uiCategoryId, const CMFCToolBarButton& button, int iInsertAfter = -1);
  void AddButton(LPCTSTR lpszCategory, const CMFCToolBarButton& button, int iInsertAfter = -1);
  int RemoveButton(UINT uiCategoryId, UINT uiCmdId);
  int RemoveButton(LPCTSTR lpszCategory, UINT uiCmdId);
  BOOL AddMenu(UINT uiMenuResId);
  void AddMenuCommands(const CMenu* pMenu, BOOL bPopup, LPCTSTR lpszCategory = NULL, LPCTSTR lpszMenuPath = NULL);
  BOOL AddToolBar(UINT uiCategoryId, UINT uiToolbarResId);
  BOOL AddToolBar(LPCTSTR lpszCategory, UINT uiToolbarResId);
  BOOL RenameCategory(LPCTSTR lpszCategoryOld, LPCTSTR lpszCategoryNew);
  int ReplaceButton(UINT uiCmd, const CMFCToolBarButton& button);
  BOOL SetUserCategory(LPCTSTR lpszCategory);
  void GetCategoriesList(CStringList& lstCategories) const;
  int GetCountInCategory(LPCTSTR lpszItemName, const CObList& lstCommands) const;
  BOOL GetCommandName(UINT uiCmd, CString& strCommandName) const;
  CFrameWnd* GetParentFrame() const { return m_pParentFrame; }
  UINT GetFlags() const { return m_uiFlags; }

  virtual BOOL OnEditToolbarMenuImage(CWnd*, CBitmap&, int) { return FALSE; }
  virtual void OnInitToolsPage() {}
  virtual void OnBeforeChangeTool(void*) {}
  virtual BOOL CheckToolsValidity(const CObList&) { return TRUE; }
  virtual BOOL OnAssignKey(ACCEL*) { return TRUE; }
  virtual BOOL OnRemoveKey(ACCEL*) { return TRUE; }
  virtual void OnAfterChangeTool(void*) {}

protected:
  virtual BOOL OnInitDialog();
  virtual void OnOK();
  virtual void OnCancel();
  virtual void PostNcDestroy();
  virtual BOOL OnCommand(WPARAM wParam, LPARAM lParam);

  void _FillList();
  CMFCToolBar* _SelectedToolBar() const;
  CObList* _Category(LPCTSTR lpszCategory, BOOL bCreate);
  void _AddMenuItems(HMENU hMenu, LPCTSTR lpszCategory);

  CFrameWnd* m_pParentFrame;
  BOOL m_bAutoSetFromMenus, m_bUserDefinedToolbars;
  UINT m_uiFlags;
  CStringList m_lstCategories;
  CMapStringToOb m_mapButtonsByCategory;  // category to a CObList of buttons
  CString m_strUserCategory;
  CObList m_lstShownToolBars;  // in the list's order
  CCheckListBox* m_pList;
  CByteArray m_template;
};

#endif  // __AFXTOOLBARSCUSTOMIZEDIALOG_H__
