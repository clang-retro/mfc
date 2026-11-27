// afxtoolbarbutton.h - CMFCToolBarButton, one button of a CMFCToolBar:
// its command, image, text and TBBS_ style, its place on the bar, and
// how it is measured, drawn and clicked. CMFCToolBar keeps copies,
// made with the button's runtime class and CopyFrom.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#ifndef __AFXTOOLBARBUTTON_H__
#define __AFXTOOLBARBUTTON_H__

#ifndef __AFXWIN_H__
#include <afxwin.h>
#endif
#include <afxext.h>

class CMFCToolBar;
class CMFCToolBarImages;

class CMFCToolBarButton : public CObject {
  DECLARE_SERIAL(CMFCToolBarButton)
  friend class CMFCToolBar;

public:
  CMFCToolBarButton();
  CMFCToolBarButton(UINT uiID, int iImage, LPCTSTR lpszText = NULL, BOOL bUserButton = FALSE,
                    BOOL bLocked = FALSE);
  virtual ~CMFCToolBarButton();

  virtual void CopyFrom(const CMFCToolBarButton& src);
  virtual BOOL CompareWith(const CMFCToolBarButton& other) const { return m_nID == other.m_nID; }
  virtual void Serialize(CArchive& ar);

  // ----- What it is.
  int GetImage() const { return m_iImage; }
  void SetImage(int iImage) { m_iImage = iImage; }
  BOOL IsLocked() const { return m_bLocked; }
  BOOL IsUserButton() const { return m_bUserButton; }
  virtual BOOL IsSeparator() const { return (m_nStyle & TBBS_SEPARATOR) != 0; }
  virtual BOOL IsHidden() const { return (m_nStyle & TBBS_HIDDEN) != 0 || !m_bVisible; }
  BOOL IsVisible() const { return m_bVisible; }
  void SetVisible(BOOL bShow = TRUE) { m_bVisible = bShow; }
  virtual BOOL IsEditable() const { return TRUE; }
  virtual BOOL IsDroppedDown() const { return FALSE; }
  virtual BOOL HaveHotBorder() const { return TRUE; }
  virtual BOOL IsExtraSize() const { return FALSE; }
  virtual BOOL CanBeStretched() const { return FALSE; }
  virtual BOOL OnGetCustomToolTipText(CString& /*strToolTip*/) { return FALSE; }
  virtual void SetStyle(UINT nStyle) { m_nStyle = nStyle; }
  UINT GetStyle() const { return m_nStyle; }
  virtual void EnableWindow(BOOL bEnable = TRUE);
  virtual BOOL IsWindowVisible() { return FALSE; }
  virtual HWND GetHwnd() { return NULL; }
  virtual void SetACCData(CWnd*, void*) {}

  // ----- On the bar.
  void SetRect(const CRect rect) { m_rect = rect; OnMove(); }
  const CRect& Rect() const { return m_rect; }
  CWnd* GetParentWnd() const { return m_pWndParent; }
  virtual void OnMove() {}
  virtual void OnShow(BOOL /*bShow*/) {}
  virtual void OnChangeParentWnd(CWnd* pWndParent) { m_pWndParent = pWndParent; }
  virtual SIZE OnCalculateSize(CDC* pDC, const CSize& sizeDefault, BOOL bHorz);
  virtual void OnDraw(CDC* pDC, const CRect& rect, CMFCToolBarImages* pImages, BOOL bHorz = TRUE,
                      BOOL bCustomizeMode = FALSE, BOOL bHighlight = FALSE, BOOL bDrawBorder = TRUE,
                      BOOL bGrayDisabledButtons = TRUE);
  // A click: TRUE when the button handled it, FALSE to send its command.
  virtual BOOL OnClick(CWnd* /*pWnd*/, BOOL /*bDelay*/ = TRUE) { return FALSE; }
  virtual BOOL OnClickUp() { return FALSE; }
  virtual BOOL OnDblClk(CWnd* /*pWnd*/) { return FALSE; }
  virtual BOOL OnContextHelp(CWnd* /*pWnd*/) { return FALSE; }
  virtual BOOL OnUpdateToolTip(CWnd*, int, CToolTipCtrl&, CString&) { return FALSE; }
  virtual void OnGlobalFontsChanged() {}
  virtual BOOL OnToolHitTest(const CWnd* pWnd, TOOLINFO* pTI);

  UINT m_nID;
  UINT m_nStyle;
  CString m_strText;
  BOOL m_bText, m_bImage, m_bWrap, m_bWholeText, m_bTextBelow, m_bDragFromCollection;
  DWORD_PTR m_dwdItemData;
  static BOOL m_bWrapText;
  static CLIPFORMAT m_cFormat;

protected:
  int m_iImage, m_iUserImage;
  BOOL m_bUserButton, m_bLocked, m_bVisible, m_bDisableFill;
  CRect m_rect;
  CWnd* m_pWndParent;
};

#endif  // __AFXTOOLBARBUTTON_H__
