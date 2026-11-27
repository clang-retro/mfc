// afxmenubar.h - CMFCMenuBar, the Feature Pack's menu bar: a toolbar
// whose buttons are the frame's menu's top items. The frame's own menu
// is taken away and shown here instead; Alt with an item's letter, or
// F10, opens its menu.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#ifndef __AFXMENUBAR_H__
#define __AFXMENUBAR_H__

#include <afxtoolbar.h>

class CMFCMenuBar : public CMFCToolBar {
  DECLARE_SERIAL(CMFCMenuBar)
public:
  CMFCMenuBar();
  virtual ~CMFCMenuBar();

  virtual BOOL Create(CWnd* pParentWnd, DWORD dwStyle = AFX_DEFAULT_TOOLBAR_STYLE, UINT nID = AFX_IDW_MENUBAR);
  virtual BOOL CreateEx(CWnd* pParentWnd, DWORD dwCtrlStyle = TBSTYLE_FLAT, DWORD dwStyle = AFX_DEFAULT_TOOLBAR_STYLE,
                        CRect rcBorders = CRect(1, 1, 1, 1), UINT nID = AFX_IDW_MENUBAR);

  // Shows a menu's top items (the frame's default menu with bDefaultMenu).
  virtual void CreateFromMenu(HMENU hMenu, BOOL bDefaultMenu = FALSE, BOOL bForceUpdate = FALSE);
  HMENU GetHMenu() const { return m_hMenu; }
  HMENU GetDefaultMenu() const { return m_hDefaultMenu; }
  virtual BOOL IsMenuBar() const { return TRUE; }
  virtual CSize CalcFixedLayout(BOOL bStretch, BOOL bHorz);
  virtual BOOL AllowShowOnList() const { return FALSE; }
  virtual void OnUpdateCmdUI(CFrameWnd* pTarget, BOOL bDisableIfNoHndler);
  // Opens the item whose letter is nChar (Alt+letter); FALSE when none.
  virtual BOOL TranslateChar(UINT nChar);
  // Opens the menu of item iItem.
  BOOL OpenItem(int iItem);
  void SetMaximizeMode(BOOL, CWnd* = NULL, BOOL = TRUE) {}
  void SetDefaultMenuResId(UINT uiResId) { m_uiDefMenuResId = uiResId; }
  UINT GetDefaultMenuResId() const { return m_uiDefMenuResId; }

  static void AFXAPI SetShowAllCommands(BOOL bShowAllCommands = TRUE) { m_bShowAllCommands = bShowAllCommands; }
  static BOOL AFXAPI IsShowAllCommands() { return m_bShowAllCommands; }
  static void AFXAPI SetRecentlyUsedMenus(BOOL bOn = TRUE) { m_bRecentlyUsedMenus = bOn; }
  static BOOL AFXAPI IsRecentlyUsedMenus() { return m_bRecentlyUsedMenus; }
  static void AFXAPI SetShowAllCommandsDelay(BOOL = TRUE) {}
  static void AFXAPI HighlightDisabledItems(BOOL bHighlight = TRUE) { m_bHighlightDisabledItems = bHighlight; }
  static BOOL AFXAPI IsHighlightDisabledItems() { return m_bHighlightDisabledItems; }
  static void AFXAPI EnableMenuShadows(BOOL bEnable = TRUE) { m_bMenuShadows = bEnable; }
  static BOOL AFXAPI IsMenuShadows() { return m_bMenuShadows; }

  static BOOL m_bShowAllCommands, m_bRecentlyUsedMenus, m_bHighlightDisabledItems, m_bMenuShadows;

protected:
  HMENU m_hMenu, m_hDefaultMenu;
  UINT m_uiDefMenuResId;
};

#endif  // __AFXMENUBAR_H__
