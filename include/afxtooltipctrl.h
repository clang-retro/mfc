// afxtooltipctrl.h - CMFCToolTipInfo, the look the tooltip manager gives
// the Feature Pack's tooltips, and CMFCToolTipCtrl, a tooltip with a bold
// title, a description and an optional icon.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#ifndef __AFXTOOLTIPCTRL_H__
#define __AFXTOOLTIPCTRL_H__

#ifndef __AFXWIN_H__
#include <afxwin.h>
#endif
#include <afxcmn.h>

class CMFCToolTipInfo {
public:
  BOOL m_bBoldLabel, m_bDrawDescription, m_bDrawIcon, m_bRoundedCorners, m_bDrawSeparator;
  BOOL m_bBalloonTooltip, m_bVislManagerTheme;
  int m_nMaxDescrWidth, m_nGradientAngle;
  COLORREF m_clrFill, m_clrFillGradient, m_clrText, m_clrBorder;

  CMFCToolTipInfo()
      : m_bBoldLabel(TRUE), m_bDrawDescription(TRUE), m_bDrawIcon(TRUE), m_bRoundedCorners(FALSE),
        m_bDrawSeparator(FALSE), m_bBalloonTooltip(FALSE), m_bVislManagerTheme(FALSE), m_nMaxDescrWidth(200),
        m_nGradientAngle(-1), m_clrFill((COLORREF)-1), m_clrFillGradient((COLORREF)-1),
        m_clrText((COLORREF)-1), m_clrBorder((COLORREF)-1) {}
  CMFCToolTipInfo& operator=(const CMFCToolTipInfo& src) {
    m_bBoldLabel = src.m_bBoldLabel;
    m_bDrawDescription = src.m_bDrawDescription;
    m_bDrawIcon = src.m_bDrawIcon;
    m_bRoundedCorners = src.m_bRoundedCorners;
    m_bDrawSeparator = src.m_bDrawSeparator;
    m_bBalloonTooltip = src.m_bBalloonTooltip;
    m_bVislManagerTheme = src.m_bVislManagerTheme;
    m_nMaxDescrWidth = src.m_nMaxDescrWidth;
    m_nGradientAngle = src.m_nGradientAngle;
    m_clrFill = src.m_clrFill;
    m_clrFillGradient = src.m_clrFillGradient;
    m_clrText = src.m_clrText;
    m_clrBorder = src.m_clrBorder;
    return *this;
  }
};

class CMFCToolTipCtrl : public CToolTipCtrl {
  DECLARE_DYNCREATE(CMFCToolTipCtrl)
public:
  CMFCToolTipCtrl(CMFCToolTipInfo* pParams = NULL);
  virtual ~CMFCToolTipCtrl();

  void SetParams(CMFCToolTipInfo* pParams);
  const CMFCToolTipInfo& GetParams() const { return m_Params; }
  void SetDescription(const CString strDescription);
  CString GetDescription() const { return m_strDescription; }
  void SetFixedWidth(int nWidthRegular, int nWidthLargeImage) {
    m_nFixedWidthRegular = nWidthRegular;
    m_nFixedWidthWithImage = nWidthLargeImage;
  }
  void SetLocation(CPoint pt) { m_ptLocation = pt; }
  void SetHotButton(void*) {}

protected:
  CMFCToolTipInfo m_Params;
  CString m_strDescription;
  int m_nFixedWidthRegular, m_nFixedWidthWithImage;
  CPoint m_ptLocation;
};

#endif  // __AFXTOOLTIPCTRL_H__
