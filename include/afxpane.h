// afxpane.h - CPane, a CBasePane that docks to its frame's sides,
// floats in a CPaneFrameWnd, has a gripper and a minimum size; the base
// of the toolbars and the dockable panes.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#ifndef __AFXPANE_H__
#define __AFXPANE_H__

#include <afxbasepane.h>

class CPane : public CBasePane {
  DECLARE_DYNCREATE(CPane)
  friend class CDockingManager;
  friend class CPaneFrameWnd;

public:
  CPane();
  virtual ~CPane();

  virtual BOOL Create(LPCTSTR lpszClassName, DWORD dwStyle, const RECT& rect, CWnd* pParentWnd, UINT nID,
                      DWORD dwControlBarStyle = AFX_CBRS_FLOAT | AFX_CBRS_RESIZE, CCreateContext* pContext = NULL);
  virtual BOOL CreateEx(DWORD dwStyleEx, LPCTSTR lpszClassName, DWORD dwStyle, const RECT& rect,
                        CWnd* pParentWnd, UINT nID, DWORD dwControlBarStyle = AFX_CBRS_FLOAT | AFX_CBRS_RESIZE,
                        CCreateContext* pContext = NULL);

  // ----- Docking and floating.
  virtual BOOL DockToFrameWindow(DWORD dwAlignment, LPCRECT lpRect = NULL, DWORD dwDockFlags = 0,
                                 CBasePane* pRelativeBar = NULL, int nRelativeIndex = -1,
                                 BOOL bOuterEdge = FALSE);
  virtual BOOL DockPane(CBasePane* pDockBar, LPCRECT lpRect, AFX_DOCK_METHOD dockMethod);
  virtual BOOL FloatPane(CRect rectFloat, AFX_DOCK_METHOD dockMethod = DM_UNKNOWN, bool bShow = true);
  virtual void UndockPane(BOOL bDelay = FALSE);
  // Docks again where it was, after floating.
  virtual BOOL DockPaneStandard(BOOL& bWasDocked);
  CRect GetRecentFloatingRect() const { return m_rectFloat; }
  DWORD GetRecentDockAlignment() const { return m_dwRecentAlignment; }

  // ----- Size and look.
  virtual void SetMinSize(const CSize& size) { m_sizeMin = size; }
  void GetMinSize(CSize& size) const { size = m_sizeMin; }
  virtual void EnableGripper(BOOL bEnable);
  virtual BOOL HasGripper() const { return (m_dwStyle & CBRS_GRIPPER) != 0; }
  virtual int GetGripperWidth() const { return HasGripper() ? 8 : 0; }
  virtual CRect GetBorders() const { return m_rectBorders; }
  void SetBorders(int cxLeft = 0, int cyTop = 0, int cxRight = 0, int cyBottom = 0) {
    m_rectBorders.SetRect(cxLeft, cyTop, cxRight, cyBottom);
  }
  void SetBorders(LPCRECT lpRect) { m_rectBorders = *lpRect; }
  virtual CSize CalcFixedLayout(BOOL bStretch, BOOL bHorz);
  virtual CSize CalcSize(BOOL bVertDock);
  virtual BOOL IsTabbed() const { return CBasePane::IsTabbed(); }
  BOOL IsDialogControl() const { return FALSE; }
  void SetExclusiveRowMode(BOOL bExclusive = TRUE) { m_bExclusiveRow = bExclusive; }
  BOOL GetExclusiveRowMode() const { return m_bExclusiveRow; }

  static int m_cyTopBorder, m_cyBottomBorder, m_cxLeftBorder, m_cxRightBorder;
  static BOOL m_bHandleMinSize;

protected:
  CSize m_sizeMin;
  CRect m_rectBorders, m_rectFloat;
  DWORD m_dwRecentAlignment;
  BOOL m_bExclusiveRow;
};

#endif  // __AFXPANE_H__
