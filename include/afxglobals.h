// afxglobals.h - AFX_GLOBAL_DATA, the Feature Pack's shared system
// colors, brushes, fonts and cursors (GetGlobalData(), afxGlobalData),
// kept up to date with the system by UpdateSysColors and UpdateFonts.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#ifndef __AFXGLOBALS_H__
#define __AFXGLOBALS_H__

#ifndef __AFXWIN_H__
#include <afxwin.h>
#endif

struct AFX_GLOBAL_DATA {
  AFX_GLOBAL_DATA();
  ~AFX_GLOBAL_DATA();

  // Colors.
  COLORREF clrBarFace, clrBarText, clrBarShadow, clrBarHilite, clrBarDkShadow, clrBarLight;
  COLORREF clrBtnFace, clrBtnText, clrBtnShadow, clrBtnHilite, clrBtnDkShadow, clrBtnLight;
  COLORREF clrGrayedText, clrHilite, clrTextHilite;
  COLORREF clrHotLinkNormalText, clrHotLinkHoveredText, clrHotLinkVisitedText;
  COLORREF clrHotText;
  COLORREF clrWindow, clrWindowText, clrWindowFrame;
  COLORREF clrCaptionText, clrMenuText, clrActiveCaption, clrInactiveCaption;
  COLORREF clrInactiveCaptionText, clrActiveBorder, clrInactiveBorder;
  COLORREF clrActiveCaptionGradient, clrInactiveCaptionGradient;
  COLORREF clrAppWorkspace;

  // Brushes of those colors.
  CBrush brBtnFace, brBarFace, brHilite, brLight, brWindow, brBlack, brActiveCaption, brInactiveCaption;

  // Fonts: the menu font and variations of it.
  CFont fontRegular, fontTooltip, fontBold, fontDefaultGUIBold, fontUnderline, fontDefaultGUIUnderline;
  CFont fontVert, fontVertCaption, fontSmall, fontMarlett;

  CPen penHilite, penBarFace, penBarShadow;

  HCURSOR m_hcurStretch, m_hcurStretchVert, m_hcurHand, m_hcurSizeAll, m_hcurMoveTab, m_hcurNoMoveTab;
  HCURSOR GetHandCursor() const { return m_hcurHand; }

  int m_nBitsPerPixel;
  int m_nTextHeightHorz, m_nTextHeightVert, m_nTextWidthHorz, m_nTextWidthVert;
  int m_nDragFrameThicknessFloat, m_nDragFrameThicknessDock;
  int m_nAutoHideToolBarSpacing, m_nAutoHideToolBarMargin;
  double m_dblRibbonImageScale;
  BOOL m_bUseSystemFont, m_bInSettingChange, m_bIsBlackHighContrast, m_bIsWhiteHighContrast;
  BOOL m_bUseBuiltIn32BitIcons, m_bMenuAnimation, m_bMenuFadeEffect, m_bIsRTL, m_bEnableAccessibility;
  BOOL bIsWindowsVista, bIsWindows7, bIsOSAlphaBlendingSupport, bDisableAero;

  int GetTextHeight(BOOL bHorz = TRUE) const { return bHorz ? m_nTextHeightHorz : m_nTextHeightVert; }
  int GetTextWidth(BOOL bHorz = TRUE) const { return bHorz ? m_nTextWidthHorz : m_nTextWidthVert; }
  double GetRibbonImageScale() const { return m_dblRibbonImageScale; }
  BOOL IsHighContrastMode() const { return m_bIsWhiteHighContrast || m_bIsBlackHighContrast; }
  BOOL IsWindowsThemingSupported() const { return TRUE; }
  BOOL IsDwmCompositionEnabled() const { return FALSE; }
  BOOL IsAccessibilitySupport() const { return m_bEnableAccessibility; }
  void EnableAccessibilitySupport(BOOL bEnable = TRUE) { m_bEnableAccessibility = bEnable; }

  void UpdateSysColors();
  void UpdateFonts();
  void OnSettingChange();
  BOOL SetMenuFont(LPLOGFONT lpLogFont, BOOL bHorz);
  void CleanUp();

  // Draws the part of a window's parent behind it (a transparent
  // control's background), by WM_ERASEBKGND and WM_PRINTCLIENT to it.
  BOOL DrawParentBackground(CWnd* pWnd, CDC* pDC, LPRECT lpRect = NULL);
  // A frame of a color, as the Feature Pack's controls draw borders.
  void DrawFocusRect(CDC* pDC, const CRect& rect, BOOL bComboBox = FALSE);
  HWND GetDefaultMenuParent() const { return NULL; }

  // The system's text for a color name, and fonts by name.
  static int GetShellAutohideBars() { return 0; }

private:
  void _CreateFonts();
};

AFX_GLOBAL_DATA* AFXAPI GetGlobalData();
#define afxGlobalData (*GetGlobalData())

// A color between two (nPercent of the way from clrFrom to clrTo).
COLORREF AFXAPI AfxBlendColor(COLORREF clrFrom, COLORREF clrTo, int nPercent);

#endif  // __AFXGLOBALS_H__
