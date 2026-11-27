// afxvisualmanager.h - CMFCVisualManager, which draws the Feature Pack's
// controls, and the looks an application picks with SetDefaultManager:
// CMFCVisualManager (classic 3D), CMFCVisualManagerOfficeXP (flat, with
// tinted highlights), CMFCVisualManagerWindows (the system's theme),
// CMFCVisualManagerOffice2003, CMFCVisualManagerVS2005,
// CMFCVisualManagerOffice2007 (four color styles), CMFCVisualManagerVS2008
// and CMFCVisualManagerWindows7. Each gives its colors in
// OnUpdateSystemColors; the drawing members use them.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#ifndef __AFXVISUALMANAGER_H__
#define __AFXVISUALMANAGER_H__

#ifndef __AFXWIN_H__
#include <afxwin.h>
#endif
#include <afxglobals.h>

class CMFCButton;
class CMFCEditBrowseCtrl;
class CMFCHeaderCtrl;
class CMFCBaseTabCtrl;
class CMFCPropertyGridCtrl;
class CMFCSpinButtonCtrl;
class CBasePane;
class CDockablePane;
class CMFCToolBar;
class CMFCToolBarButton;
class CMFCCaptionButton;
class CMFCStatusBar;
struct CMFCStatusBarPaneInfo;

// The spin button parts' states for OnDrawSpinButtons.
#define AFX_SPIN_PRESSEDUP 0x0001
#define AFX_SPIN_PRESSEDDOWN 0x0002
#define AFX_SPIN_HIGHLIGHTEDUP 0x0004
#define AFX_SPIN_HIGHLIGHTEDDOWN 0x0008
#define AFX_SPIN_DISABLED 0x0010

class CMFCVisualManager : public CObject {
  DECLARE_DYNCREATE(CMFCVisualManager)
public:
  CMFCVisualManager(BOOL bTemporary = FALSE);
  virtual ~CMFCVisualManager();

  // The look in use (made on first use), and choosing another.
  static CMFCVisualManager* AFXAPI GetInstance();
  static void AFXAPI SetDefaultManager(CRuntimeClass* pRTI);
  static void AFXAPI DestroyInstance(BOOL bAutoDestroyOnly = FALSE);
  static void AFXAPI RedrawAll();

  enum AFX_BUTTON_STATE { ButtonsIsRegular, ButtonsIsPressed, ButtonsIsHighlighted };

  virtual void OnUpdateSystemColors();

  // ----- Buttons.
  // Fills a CMFCButton's face; FALSE to leave it to the button.
  virtual BOOL OnDrawPushButton(CDC* pDC, CRect rect, CMFCButton* pButton, COLORREF& clrText);
  virtual void OnDrawButtonBorder(CDC* pDC, CRect& rect, CMFCButton* pButton, UINT uiState);
  virtual COLORREF GetButtonTextColor(CMFCButton* pButton, UINT uiState);
  virtual void OnDrawCheckBox(CDC* pDC, CRect rect, BOOL bHighlighted, BOOL bChecked, BOOL bEnabled);
  virtual void OnDrawCheckBoxEx(CDC* pDC, CRect rect, int nState, BOOL bHighlighted, BOOL bPressed,
                                BOOL bEnabled);
  virtual void OnDrawRadioButton(CDC* pDC, CRect rect, BOOL bOn, BOOL bHighlighted, BOOL bPressed, BOOL bEnabled);
  // The down arrow of a menu or color button.
  virtual void OnDrawMenuArrow(CDC* pDC, CRect rect, BOOL bEnabled);

  // ----- Edit controls' browse buttons and spin buttons.
  virtual BOOL OnDrawBrowseButton(CDC* pDC, CRect rect, CMFCEditBrowseCtrl* pEdit, AFX_BUTTON_STATE state,
                                  COLORREF& clrText);
  virtual void OnDrawSpinButtons(CDC* pDC, CRect rectSpin, int nState, BOOL bOrientation,
                                 CMFCSpinButtonCtrl* pSpinCtrl);

  // ----- Header controls.
  virtual void OnFillHeaderCtrlBackground(CMFCHeaderCtrl* pCtrl, CDC* pDC, CRect rect);
  virtual void OnDrawHeaderCtrlBorder(CMFCHeaderCtrl* pCtrl, CDC* pDC, CRect& rect, BOOL bIsPressed,
                                      BOOL bIsHighlighted);
  virtual void OnDrawHeaderCtrlSortArrow(CMFCHeaderCtrl* pCtrl, CDC* pDC, CRect& rect, BOOL bIsUp);

  // ----- Tab controls.
  virtual void OnEraseTabsArea(CDC* pDC, CRect rect, const CMFCBaseTabCtrl* pTabWnd);
  virtual void OnDrawTab(CDC* pDC, CRect rectTab, int iTab, BOOL bIsActive, const CMFCBaseTabCtrl* pTabWnd);
  virtual void OnDrawTabCloseButton(CDC* pDC, CRect rect, const CMFCBaseTabCtrl* pTabWnd, BOOL bIsHighlighted,
                                    BOOL bIsPressed, BOOL bIsDisabled);
  virtual COLORREF GetTabTextColor(const CMFCBaseTabCtrl* pTabWnd, int iTab, BOOL bIsActive);
  virtual int GetTabHorzMargin(const CMFCBaseTabCtrl*) { return 4; }

  // ----- Property grids.
  virtual COLORREF GetPropertyGridGroupColor(CMFCPropertyGridCtrl* pPropList);
  virtual COLORREF GetPropertyGridGroupTextColor(CMFCPropertyGridCtrl* pPropList);
  virtual COLORREF OnFillPropertyGridGroup(CDC* pDC, CMFCPropertyGridCtrl* pPropList, CRect rect);

  // ----- Bars and their buttons.
  virtual void OnFillBarBackground(CDC* pDC, CBasePane* pBar, CRect rectClient, CRect rectClip,
                                   BOOL bNCArea = FALSE);
  virtual void OnDrawBarGripper(CDC* pDC, CRect rectGripper, BOOL bHorz, CBasePane* pBar);
  virtual void OnDrawSeparator(CDC* pDC, CBasePane* pBar, CRect rect, BOOL bIsHoriz);
  virtual void OnFillButtonInterior(CDC* pDC, CMFCToolBarButton* pButton, CRect rect, AFX_BUTTON_STATE state);
  virtual void OnDrawButtonBorder(CDC* pDC, CMFCToolBarButton* pButton, CRect rect, AFX_BUTTON_STATE state);
  virtual COLORREF GetToolbarButtonTextColor(CMFCToolBarButton* pButton, AFX_BUTTON_STATE state);
  virtual void OnHighlightMenuItem(CDC* pDC, CMFCToolBarButton* pButton, CRect rect, COLORREF& clrText);

  // ----- Dockable panes: the caption (returns its text color), its
  // buttons, and the divider that resizes the pane.
  virtual COLORREF OnDrawPaneCaption(CDC* pDC, CDockablePane* pBar, BOOL bActive, CRect rectCaption,
                                     CRect rectButtons);
  virtual void OnDrawCaptionButton(CDC* pDC, CMFCCaptionButton* pButton, BOOL bActive, BOOL bHorz,
                                   BOOL bMaximized, BOOL bDisabled, int nImageID = -1);
  virtual void OnDrawPaneDivider(CDC* pDC, CBasePane* pBar, CRect rect, BOOL bIsHoriz);
  virtual void OnDrawAutoHideButtonBorder(CDC* pDC, CRect rectBounds, CRect rectBorderSize, CBasePane* pButton);
  virtual int GetDockingPaneCaptionExtraHeight() const { return 0; }

  // ----- Status bars.
  virtual void OnDrawStatusBarPaneBorder(CDC* pDC, CMFCStatusBar* pBar, CRect rectPane, UINT uiID, UINT nStyle);
  virtual void OnDrawStatusBarProgress(CDC* pDC, CMFCStatusBar* pStatusBar, CRect rectProgress, int nProgressTotal,
                                       int nProgressCurr, COLORREF clrBar, COLORREF clrProgressBarDest,
                                       COLORREF clrProgressText, BOOL bProgressText);
  virtual void OnDrawStatusBarSizeBox(CDC* pDC, CMFCStatusBar* pStatBar, CRect rectSizeBox);

  // ----- Colors of the look.
  virtual COLORREF GetHighlightColor() const { return m_clrHighlight; }
  virtual COLORREF GetHighlightBorderColor() const { return m_clrHighlightBorder; }
  virtual COLORREF GetPressedColor() const { return m_clrPressed; }
  virtual COLORREF GetFaceColor() const { return m_clrFace; }
  virtual COLORREF GetMenuBarColor() const { return m_clrFace; }
  virtual BOOL IsFlat() const { return m_bFlat; }

  BOOL IsMenuFlatLook() const { return m_bMenuFlatLook; }
  void SetMenuFlatLook(BOOL bMenuFlatLook = TRUE) { m_bMenuFlatLook = bMenuFlatLook; }
  BOOL IsAutoDestroy() const { return m_bAutoDestroy; }
  BOOL IsShadowHighlightedImage() const { return m_bShadowHighlightedImage; }
  void SetShadowHighlightedImage(BOOL bShadow = TRUE) { m_bShadowHighlightedImage = bShadow; }
  BOOL IsEmbossDisabledImage() const { return m_bEmbossDisabledImage; }
  void SetEmbossDisabledImage(BOOL bEmboss = TRUE) { m_bEmbossDisabledImage = bEmboss; }
  BOOL IsFadeInactiveImage() const { return m_bFadeInactiveImage; }
  void SetFadeInactiveImage(BOOL bFade = TRUE) { m_bFadeInactiveImage = bFade; }
  virtual BOOL IsOwnerDrawMenuCheck() { return FALSE; }
  virtual BOOL IsHighlightWholeMenuItem() { return FALSE; }
  virtual int GetMenuImageMargin() const { return 2; }
  virtual BOOL IsWindowsThemingSupported() const { return FALSE; }

protected:
  // A rectangle filled with one color and framed with another.
  void _FillFramed(CDC* pDC, const CRect& rect, COLORREF clrFill, COLORREF clrFrame);

  BOOL m_bTemporary, m_bAutoDestroy, m_bFlat;
  BOOL m_bMenuFlatLook, m_bShadowHighlightedImage, m_bEmbossDisabledImage, m_bFadeInactiveImage;
  COLORREF m_clrFace, m_clrHighlight, m_clrHighlightBorder, m_clrPressed, m_clrPressedBorder;

  static CRuntimeClass* m_pRTIDefault;
  static CMFCVisualManager* m_pVisManager;
};

class CMFCVisualManagerOfficeXP : public CMFCVisualManager {
  DECLARE_DYNCREATE(CMFCVisualManagerOfficeXP)
public:
  CMFCVisualManagerOfficeXP(BOOL bTemporary = FALSE);
  virtual void OnUpdateSystemColors();
};

class CMFCVisualManagerWindows : public CMFCVisualManagerOfficeXP {
  DECLARE_DYNCREATE(CMFCVisualManagerWindows)
public:
  CMFCVisualManagerWindows(BOOL bTemporary = FALSE);
  virtual void OnUpdateSystemColors();
  virtual BOOL IsWindowsThemingSupported() const { return TRUE; }
  static BOOL m_b3DTabsXPTheme;
};

class CMFCVisualManagerOffice2003 : public CMFCVisualManagerOfficeXP {
  DECLARE_DYNCREATE(CMFCVisualManagerOffice2003)
public:
  CMFCVisualManagerOffice2003();
  virtual void OnUpdateSystemColors();
  static void AFXAPI SetUseGlobalTheme(BOOL bUseGlobalTheme = TRUE) { m_bUseGlobalTheme = bUseGlobalTheme; }
  static BOOL AFXAPI IsUseGlobalTheme() { return m_bUseGlobalTheme; }
  static void AFXAPI SetStatusBarOfficeXPLook(BOOL bStatusBarOfficeXPLook = TRUE) {
    m_bStatusBarOfficeXPLook = bStatusBarOfficeXPLook;
  }
  static void AFXAPI SetDefaultWinXPColors(BOOL bDefaultWinXPColors = TRUE) {
    m_bDefaultWinXPColors = bDefaultWinXPColors;
  }

protected:
  static BOOL m_bUseGlobalTheme, m_bStatusBarOfficeXPLook, m_bDefaultWinXPColors;
};

class CMFCVisualManagerVS2005 : public CMFCVisualManagerOffice2003 {
  DECLARE_DYNCREATE(CMFCVisualManagerVS2005)
public:
  CMFCVisualManagerVS2005();
  virtual void OnUpdateSystemColors();
  static BOOL m_bRoundedAutohideButtons;
};

class CMFCVisualManagerOffice2007 : public CMFCVisualManagerOffice2003 {
  DECLARE_DYNCREATE(CMFCVisualManagerOffice2007)
public:
  enum Style { Office2007_LunaBlue, Office2007_ObsidianBlack, Office2007_Aqua, Office2007_Silver };

  CMFCVisualManagerOffice2007();
  virtual void OnUpdateSystemColors();
  static BOOL AFXAPI SetStyle(Style style, LPCTSTR lpszPath = NULL);
  static Style AFXAPI GetStyle() { return m_Style; }
  static void AFXAPI SetCustomColor(COLORREF clrStyle) { m_clrCustom = clrStyle; }

protected:
  static Style m_Style;
  static COLORREF m_clrCustom;
};

class CMFCVisualManagerVS2008 : public CMFCVisualManagerVS2005 {
  DECLARE_DYNCREATE(CMFCVisualManagerVS2008)
public:
  CMFCVisualManagerVS2008();
  virtual void OnUpdateSystemColors();
};

class CMFCVisualManagerWindows7 : public CMFCVisualManagerWindows {
  DECLARE_DYNCREATE(CMFCVisualManagerWindows7)
public:
  CMFCVisualManagerWindows7();
  virtual void OnUpdateSystemColors();
};

#endif  // __AFXVISUALMANAGER_H__
