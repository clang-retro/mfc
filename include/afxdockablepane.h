// afxdockablepane.h - CDockablePane, a resizable pane with a caption
// (its title, a close button and an auto-hide pin) that docks to a side
// of its frame, floats in a CPaneFrameWnd, joins other panes as a tab of
// a CTabbedPane, or hides in an auto-hide bar on its side. The pane's
// client area is what is left inside the caption and the divider that
// resizes it; a pane lays its own child windows out there in OnSize.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#ifndef __AFXDOCKABLEPANE_H__
#define __AFXDOCKABLEPANE_H__

#include <afxpane.h>

class CTabbedPane;
class CMFCAutoHideBar;

// A button on a pane's caption.
#define AFX_HTCLOSE HTCLOSE
#define AFX_HTMAXBUTTON HTMAXBUTTON
#define AFX_HTMINBUTTON HTMINBUTTON
#define AFX_HTLEFTBUTTON (HTMAXBUTTON + 0x100)
#define AFX_HTRIGHTBUTTON (HTMAXBUTTON + 0x101)
#define AFX_HTMENU (HTMAXBUTTON + 0x102)

class CMFCCaptionButton : public CObject {
public:
  CMFCCaptionButton(UINT nHit = 0, BOOL bLeftAlign = FALSE)
      : m_nHit(nHit), m_bLeftAlign(bLeftAlign), m_bPushed(FALSE), m_bFocused(FALSE), m_bHidden(FALSE),
        m_bEnabled(TRUE), m_rect(0, 0, 0, 0) {}
  virtual ~CMFCCaptionButton() {}

  UINT GetHit() const { return m_nHit; }
  CRect GetRect() const { return m_rect; }
  void SetRect(const CRect& rect) { m_rect = rect; }
  CSize GetSize() const { return CSize(14, 14); }
  BOOL IsPushed() const { return m_bPushed; }
  BOOL IsFocused() const { return m_bFocused; }
  BOOL IsHidden() const { return m_bHidden; }
  void Hide(BOOL bHide) { m_bHidden = bHide; }
  BOOL IsEnabled() const { return m_bEnabled; }

  UINT m_nHit;
  BOOL m_bLeftAlign, m_bPushed, m_bFocused, m_bHidden, m_bEnabled;
  CRect m_rect;
};

class CDockablePane : public CPane {
  DECLARE_SERIAL(CDockablePane)
  friend class CDockingManager;
  friend class CTabbedPane;
  friend class CMFCAutoHideBar;
  friend class CPaneFrameWnd;
  friend class CBaseTabbedPane;

public:
  CDockablePane();
  virtual ~CDockablePane();

  virtual BOOL Create(LPCTSTR lpszCaption, CWnd* pParentWnd, const RECT& rect, BOOL bHasGripper, UINT nID,
                      DWORD dwStyle, DWORD dwTabbedStyle = AFX_CBRS_REGULAR_TABS,
                      DWORD dwControlBarStyle = AFX_DEFAULT_DOCKING_PANE_STYLE, CCreateContext* pContext = NULL);
  virtual BOOL Create(LPCTSTR lpszWindowName, CWnd* pParentWnd, CSize sizeDefault, BOOL bHasGripper, UINT nID,
                      DWORD dwStyle = WS_CHILD | WS_VISIBLE | CBRS_TOP | CBRS_HIDE_INPLACE,
                      DWORD dwTabbedStyle = AFX_CBRS_REGULAR_TABS,
                      DWORD dwControlBarStyle = AFX_DEFAULT_DOCKING_PANE_STYLE);
  virtual BOOL CreateEx(DWORD dwStyleEx, LPCTSTR lpszCaption, CWnd* pParentWnd, const RECT& rect,
                        BOOL bHasGripper, UINT nID, DWORD dwStyle, DWORD dwTabbedStyle = AFX_CBRS_REGULAR_TABS,
                        DWORD dwControlBarStyle = AFX_DEFAULT_DOCKING_PANE_STYLE, CCreateContext* pContext = NULL);

  virtual BOOL IsResizable() const { return TRUE; }
  virtual BOOL IsAutoHideMode() const { return m_pAutoHideBar != NULL; }
  virtual BOOL IsTabbed() const { return m_pTabbedParent != NULL; }
  virtual BOOL HasGripper() const { return m_bHasGripper; }
  virtual void EnableGripper(BOOL bEnable);
  virtual int GetCaptionHeight() const;
  virtual BOOL IsHideInAutoHideMode() const { return FALSE; }

  // ----- Docking.
  virtual BOOL DockToFrameWindow(DWORD dwAlignment, LPCRECT lpRect = NULL, DWORD dwDockFlags = 0,
                                 CBasePane* pRelativeBar = NULL, int nRelativeIndex = -1,
                                 BOOL bOuterEdge = FALSE);
  // Docks beside pTargetWindow (in its row), on its dwAlignment side.
  virtual BOOL DockToWindow(CDockablePane* pTargetWindow, DWORD dwAlignment, LPCRECT lpRect = NULL);
  virtual BOOL FloatPane(CRect rectFloat, AFX_DOCK_METHOD dockMethod = DM_UNKNOWN, bool bShow = true);
  // Puts this pane in a tab next to pTabControlBarAttachTo, making a
  // CTabbedPane where it was if it is not one already.
  virtual CDockablePane* AttachToTabWnd(CDockablePane* pTabControlBarAttachTo, AFX_DOCK_METHOD dockMethod,
                                        BOOL bSetActive = TRUE, CDockablePane** ppTabbedControlBar = NULL);
  CTabbedPane* GetParentTabbedPane() const;
  virtual BOOL CanBeAttached() const { return TRUE; }
  virtual BOOL CanBeTabbedDocument() const { return FALSE; }
  virtual BOOL IsAccessibilityCompatible() { return TRUE; }

  // ----- Auto-hide.
  virtual void SetAutoHideMode(BOOL bMode, DWORD dwAlignment, CMFCAutoHideBar* pCurrAutoHideBar = NULL,
                               BOOL bUseTimer = TRUE);
  // Slides an auto-hidden pane out over the client area, or back.
  virtual void Slide(BOOL bSlideOut, BOOL bUseTimer = TRUE);
  BOOL IsSlidOut() const { return m_bSlidOut; }
  CMFCAutoHideBar* GetAutoHideToolBar() const { return m_pAutoHideBar; }

  // ----- Showing, the caption and the icon.
  virtual void ShowPane(BOOL bShow, BOOL bDelay, BOOL bActivate);
  virtual void OnPressCloseButton();
  virtual void OnPressButtons(UINT /*nHit*/) {}
  virtual void OnAfterChangeParent(CWnd* /*pWndOldParent*/) {}
  virtual CSize CalcFixedLayout(BOOL bStretch, BOOL bHorz);
  HICON GetIcon(BOOL bBigIcon = TRUE) const { return bBigIcon ? m_hIconBig : m_hIconSmall; }
  HICON SetIcon(HICON hIcon, BOOL bBigIcon);
  BOOL IsActive() const;
  static int m_nTimeOutBeforeAutoHide, m_nSlideSteps;
  static BOOL m_bHideInAutoHideMode, m_bDisableAnimation;

protected:
  virtual void OnDrawCaption(CDC* pDC, CRect rectCaption);
  void _UpdateNonClient();
  CRect _CaptionRect() const;    // in window coordinates
  CRect _DividerRect() const;    // in window coordinates, empty when none
  DWORD _DividerSide() const;    // the side of the pane the divider is on
  void _LayoutButtons();
  CMFCCaptionButton* _ButtonAt(CPoint ptWindow);
  CMFCCaptionButton* _Button(UINT nHit);
  void _PlaceSlidOut();         // over the client area, on its side
  void _StartFloatDrag(CPoint ptScreen);

  afx_msg int OnCreate(LPCREATESTRUCT lpCreateStruct);
  afx_msg void OnNcCalcSize(BOOL bCalcValidRects, NCCALCSIZE_PARAMS* lpncsp);
  afx_msg void OnNcPaint();
  afx_msg LRESULT OnNcHitTest(CPoint point);
  afx_msg void OnNcLButtonDown(UINT nHitTest, CPoint point);
  afx_msg void OnNcLButtonDblClk(UINT nHitTest, CPoint point);
  afx_msg void OnNcMouseMove(UINT nHitTest, CPoint point);
  afx_msg void OnMouseMove(UINT nFlags, CPoint point);
  afx_msg void OnLButtonUp(UINT nFlags, CPoint point);
  afx_msg void OnCancelMode();
  afx_msg BOOL OnSetCursor(CWnd* pWnd, UINT nHitTest, UINT message);
  afx_msg void OnSize(UINT nType, int cx, int cy);
  afx_msg void OnSetFocus(CWnd* pOldWnd);
  afx_msg LRESULT OnSetText(WPARAM wParam, LPARAM lParam);
  afx_msg void OnContextMenu(CWnd* pWnd, CPoint point);
  afx_msg void OnTimer(UINT_PTR nIDEvent);
  afx_msg LRESULT OnIdleUpdateCmdUI(WPARAM wParam, LPARAM lParam);
  DECLARE_MESSAGE_MAP()

  BOOL m_bHasGripper, m_bSlidOut, m_bTrackingDivider, m_bTrackingButton, m_bCaptionDown, m_bWasActive;
  DWORD m_dwTabbedStyle;
  CMFCAutoHideBar* m_pAutoHideBar;
  CObList m_lstCaptionButtons;
  CMFCCaptionButton* m_pPressedButton;
  CPoint m_ptTrackStart;
  CSize m_sizeTrackStart;
  HICON m_hIconBig, m_hIconSmall;
};

#endif  // __AFXDOCKABLEPANE_H__
