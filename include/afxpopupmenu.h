// afxpopupmenu.h - CMFCPopupMenu, a menu shown at a point. Create shows
// the menu and returns when it closes: the command picked goes to the
// owner (bOwnMessage) or to the application's main frame, and the
// object, made with new, deletes itself.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#ifndef __AFXPOPUPMENU_H__
#define __AFXPOPUPMENU_H__

#ifndef __AFXWIN_H__
#include <afxwin.h>
#endif

class CMFCToolBarMenuButton;

class CMFCPopupMenu : public CMiniFrameWnd {
  DECLARE_SERIAL(CMFCPopupMenu)
public:
  enum ANIMATION_TYPE { NO_ANIMATION, UNFOLD, SLIDE, FADE, SYSTEM_DEFAULT_ANIMATION };
  enum MENUAREA_TYPE { OUTSIDE, MENU, SHADOW_RIGHT, SHADOW_BOTTOM, LOGO, TEAROFF_CAPTION };
  enum DROP_DIRECTION { DROP_DIRECTION_NONE, DROP_DIRECTION_BOTTOM, DROP_DIRECTION_TOP, DROP_DIRECTION_RIGHT,
                        DROP_DIRECTION_LEFT };

  CMFCPopupMenu();
  CMFCPopupMenu(CMFCToolBarMenuButton* pParentBtn) : CMFCPopupMenu() { m_pParentBtn = pParentBtn; }
  virtual ~CMFCPopupMenu();

  virtual BOOL Create(CWnd* pWndParent, int x, int y, HMENU hMenu, BOOL bLocked = FALSE, BOOL bOwnMessage = FALSE);
  // The command picked in the last menu shown (0 for none).
  UINT GetLastCommand() const { return m_nLastCommand; }
  void SetRightAlign(BOOL bRightAlign = TRUE) { m_bRightAlign = bRightAlign; }
  void SetAutoDestroy(BOOL bAutoDestroy = TRUE) { m_bAutoDestroy = bAutoDestroy; }
  HMENU GetHMenu() const { return m_hMenu; }
  CMFCToolBarMenuButton* GetParentButton() const { return m_pParentBtn; }
  void SetMessageWnd(CWnd* pWnd) { m_pMessageWnd = pWnd; }
  CWnd* GetMessageWnd() const { return m_pMessageWnd; }

  static CMFCPopupMenu* AFXAPI GetActiveMenu() { return m_pActivePopupMenu; }
  static void AFXAPI SetForceShadow(BOOL bForce) { m_bForceShadow = bForce; }
  static void AFXAPI SetForceMenuFocus(BOOL bValue) { m_bForceMenuFocus = bValue; }
  static BOOL AFXAPI GetForceMenuFocus() { return m_bForceMenuFocus; }
  static void AFXAPI SetAnimationType(ANIMATION_TYPE type) { m_AnimationType = type; }
  static ANIMATION_TYPE AFXAPI GetAnimationType(BOOL = FALSE) { return m_AnimationType; }
  static void AFXAPI SetAnimationSpeed(UINT nElapse) { m_AnimationSpeed = nElapse; }
  static UINT AFXAPI GetAnimationSpeed() { return m_AnimationSpeed; }
  static void AFXAPI EnableMenuSound(BOOL bEnable = TRUE) { m_bMenuSound = bEnable; }
  static UINT AFXAPI IsMenuSound() { return m_bMenuSound; }
  static void AFXAPI AlwaysShowEmptyToolsEntry(BOOL = TRUE) {}

  static BOOL m_bForceShadow, m_bForceMenuFocus, m_bMenuSound;
  static ANIMATION_TYPE m_AnimationType;
  static UINT m_AnimationSpeed;

protected:
  HMENU m_hMenu;
  BOOL m_bRightAlign, m_bAutoDestroy;
  CMFCToolBarMenuButton* m_pParentBtn;
  CWnd* m_pMessageWnd;
  UINT m_nLastCommand;
  static CMFCPopupMenu* m_pActivePopupMenu;
};

#endif  // __AFXPOPUPMENU_H__
