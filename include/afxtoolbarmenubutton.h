// afxtoolbarmenubutton.h - CMFCToolBarMenuButton, a toolbar button with
// a menu: a click (or the arrow beside it) drops the menu down under the
// button, and the command picked goes to the frame. The menu bar's items
// are menu buttons too.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#ifndef __AFXTOOLBARMENUBUTTON_H__
#define __AFXTOOLBARMENUBUTTON_H__

#include <afxtoolbarbutton.h>

class CMFCPopupMenu;

class CMFCToolBarMenuButton : public CMFCToolBarButton {
  DECLARE_SERIAL(CMFCToolBarMenuButton)
  friend class CMFCMenuBar;
  friend class CMFCToolBar;
public:
  CMFCToolBarMenuButton();
  CMFCToolBarMenuButton(const CMFCToolBarMenuButton& src);
  CMFCToolBarMenuButton(UINT uiID, HMENU hMenu, int iImage, LPCTSTR lpszText = NULL, BOOL bUserButton = FALSE);
  virtual ~CMFCToolBarMenuButton();

  virtual void CopyFrom(const CMFCToolBarButton& src);
  // A copy of the button's menu, for the caller to destroy.
  virtual HMENU CreateMenu() const;
  HMENU GetMenu() const { return m_hMenu; }
  virtual void CreateFromMenu(HMENU hMenu);
  virtual BOOL OpenPopupMenu(CWnd* pWnd = NULL);
  virtual BOOL OnClick(CWnd* pWnd, BOOL bDelay = TRUE);
  virtual BOOL IsDroppedDown() const { return m_bDroppedDown; }
  virtual SIZE OnCalculateSize(CDC* pDC, const CSize& sizeDefault, BOOL bHorz);
  virtual void OnDraw(CDC* pDC, const CRect& rect, CMFCToolBarImages* pImages, BOOL bHorz = TRUE,
                      BOOL bCustomizeMode = FALSE, BOOL bHighlight = FALSE, BOOL bDrawBorder = TRUE,
                      BOOL bGrayDisabledButtons = TRUE);
  CMFCPopupMenu* GetPopupMenu() const { return NULL; }
  void SetMenuOnly(BOOL bMenuOnly) { m_bMenuOnly = bMenuOnly; }
  void SetMessageWnd(CWnd* pWndMessage) { m_pWndMessage = pWndMessage; }
  void SetMenuPaletteMode(BOOL, int = 2) {}
  void SetTearOff(UINT) {}
  // The command picked last (0 when none), and the window it went to.
  UINT GetLastCommand() const { return m_nLastCommand; }

  BOOL m_bDrawDownArrow, m_bMenuMode, m_bClickedOnMenu, m_bMenuOnly;
  static BOOL m_bAlwaysCallOwnerDraw;

protected:
  HMENU m_hMenu;
  BOOL m_bOwnMenu, m_bDroppedDown;
  CWnd* m_pWndMessage;
  UINT m_nLastCommand;
};

#endif  // __AFXTOOLBARMENUBUTTON_H__
