// afxtoolbar.h - CMFCToolBar, the Feature Pack's toolbar: buttons
// (CMFCToolBarButton and its kinds) loaded from a toolbar resource or
// added one by one, their images in the toolbars' shared image strip
// (each command's image known to the command manager), drawn by the
// visual manager, enabled and checked by command UI, with tooltips; laid
// out across or down as the bar is docked.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#ifndef __AFXTOOLBAR_H__
#define __AFXTOOLBAR_H__

#include <afxpane.h>
#include <afxtoolbarbutton.h>
#include <afxtoolbarimages.h>
#include <afxtoolbarmenubutton.h>
#include <afxtoolbarcomboboxbutton.h>
#include <afxtempl.h>

// Sent to the frame for a new user toolbar, and when the user resets one.
extern UINT AFX_WM_CREATETOOLBAR;
extern UINT AFX_WM_DELETETOOLBAR;
extern UINT AFX_WM_CUSTOMIZETOOLBAR;
extern UINT AFX_WM_RESETTOOLBAR;
extern UINT AFX_WM_SHOWREGULARMENU;
extern UINT AFX_WM_TOOLBARMENU;
extern UINT AFX_WM_CUSTOMIZEHELP;
extern UINT AFX_WM_GETDOCUMENTCOLORS;

class CMFCBaseToolBar : public CPane {
  DECLARE_DYNCREATE(CMFCBaseToolBar)
public:
  CMFCBaseToolBar() {}
  virtual ~CMFCBaseToolBar() {}
};

class CMFCToolBar : public CMFCBaseToolBar {
  DECLARE_SERIAL(CMFCToolBar)
  friend class CMFCToolBarButton;

public:
  CMFCToolBar();
  virtual ~CMFCToolBar();

  virtual BOOL Create(CWnd* pParentWnd, DWORD dwStyle = AFX_DEFAULT_TOOLBAR_STYLE, UINT nID = AFX_IDW_TOOLBAR);
  virtual BOOL CreateEx(CWnd* pParentWnd, DWORD dwCtrlStyle = TBSTYLE_FLAT, DWORD dwStyle = AFX_DEFAULT_TOOLBAR_STYLE,
                        CRect rcBorders = CRect(1, 1, 1, 1), UINT nID = AFX_IDW_TOOLBAR);

  // ----- Loading.
  virtual BOOL LoadToolBar(UINT uiResID, UINT uiColdResID = 0, UINT uiMenuResID = 0, BOOL bLocked = FALSE,
                           UINT uiDisabledResID = 0, UINT uiMenuDisabledResID = 0, UINT uiHotResID = 0);
  virtual BOOL LoadToolBarEx(UINT uiToolbarResID, void* params, BOOL bLocked = FALSE);
  virtual BOOL LoadBitmap(UINT uiResID, UINT uiColdResID = 0, UINT uiMenuResID = 0, BOOL bLocked = FALSE,
                          UINT uiDisabledResID = 0, UINT uiMenuDisabledResID = 0);
  BOOL SetButtons(const UINT* lpIDArray, int nIDCount, BOOL bRemapImages = TRUE);

  // ----- The buttons.
  virtual int InsertButton(const CMFCToolBarButton& button, INT_PTR iInsertAt = -1);
  virtual int InsertSeparator(INT_PTR iInsertAt = -1);
  virtual BOOL RemoveButton(int iIndex);
  virtual void RemoveAllButtons();
  int ReplaceButton(UINT uiCmd, const CMFCToolBarButton& button, BOOL bAll = FALSE);
  int GetCount() const { return (int)m_Buttons.GetCount(); }
  CMFCToolBarButton* GetButton(int iIndex) const;
  const CObList& GetAllButtons() const { return m_Buttons; }
  int CommandToIndex(UINT nIDFind, int iIndexFirst = 0) const;
  UINT GetItemID(int nIndex) const;
  virtual void GetItemRect(int nIndex, LPRECT lpRect) const;
  UINT GetButtonStyle(int nIndex) const;
  virtual void SetButtonStyle(int nIndex, UINT nStyle);
  CString GetButtonText(int nIndex) const;
  void GetButtonText(int nIndex, CString& rString) const { rString = GetButtonText(nIndex); }
  BOOL SetButtonText(int nIndex, LPCTSTR lpszText);
  void GetButtonInfo(int nIndex, UINT& nID, UINT& nStyle, int& iImage) const;
  void SetButtonInfo(int nIndex, UINT nID, UINT nStyle, int iImage);
  BOOL SetToolBarBtnText(UINT nBtnIndex, LPCTSTR szText = NULL, BOOL bShowText = TRUE, BOOL bShowImage = TRUE);
  virtual int HitTest(CPoint point);
  int GetHighlightedButton() const { return m_iHighlighted; }
  CMFCToolBarButton* GetHighlightedButtonPtr() const { return GetButton(m_iHighlighted); }
  // Every button with this command, on every toolbar.
  static int AFXAPI GetCommandButtons(UINT uiCmd, CObList& listButtons);
  static const CObList& AFXAPI GetAllToolbars();

  // ----- Look.
  virtual void AdjustLayout();
  virtual CSize CalcFixedLayout(BOOL bStretch, BOOL bHorz);
  virtual CSize CalcSize(BOOL bVertDock);
  void EnableTextLabels(BOOL bEnable = TRUE);
  BOOL AreTextLabels() const { return m_bTextLabels; }
  virtual void EnableCustomizeButton(BOOL bEnable, int iCustomizeCmd, const CString& strCustomizeText,
                                     BOOL bQuickCustomize = TRUE);
  virtual void EnableCustomizeButton(BOOL bEnable, int iCustomizeCmd, UINT uiCustomizeTextResId,
                                     BOOL bQuickCustomize = TRUE);
  BOOL IsExistCustomizeButton() const { return m_bCustomizeButton; }
  BOOL IsLocked() const { return m_bLocked; }
  CMFCToolBarImages* GetLockedImages() { return m_bLocked ? &m_ImagesLocked : NULL; }
  CMFCToolBarImages* GetLockedColdImages() { return m_bLocked ? &m_ColdImagesLocked : NULL; }
  CMFCToolBarImages* GetLockedDisabledImages() { return m_bLocked ? &m_DisabledImagesLocked : NULL; }
  // The shared images every unlocked toolbar draws from.
  static CMFCToolBarImages* AFXAPI GetImages() { return &m_Images; }
  static CMFCToolBarImages* AFXAPI GetColdImages() { return &m_ColdImages; }
  static CMFCToolBarImages* AFXAPI GetDisabledImages() { return &m_DisabledImages; }
  static CMFCToolBarImages* AFXAPI GetMenuImages() { return &m_MenuImages; }
  static CMFCToolBarImages* AFXAPI GetDisabledMenuImages() { return &m_DisabledMenuImages; }
  // The images a button of this toolbar draws from.
  CMFCToolBarImages* _ButtonImages() { return m_bLocked ? &m_ImagesLocked : &m_Images; }
  void CleanUpLockedImages();
  CSize GetButtonSize() const { return m_bLocked ? m_sizeCurButtonLocked : m_sizeButton; }
  CSize GetImageSize() const { return m_bLocked ? m_sizeCurImageLocked : m_sizeImage; }
  void SetLockedSizes(SIZE sizeButton, SIZE sizeImage, BOOL bDontScale = FALSE);
  void SetRouteCommandsViaFrame(BOOL bValue) { m_bRouteCommandsViaFrame = bValue; }
  BOOL GetRouteCommandsViaFrame() const { return m_bRouteCommandsViaFrame; }
  virtual BOOL AllowShowOnList() const { return TRUE; }
  virtual BOOL AllowShowOnPaneMenu() const { return AllowShowOnList(); }
  virtual BOOL IsButtonExtraSizeAvailable() const { return TRUE; }
  virtual BOOL OnUserToolTip(CMFCToolBarButton* /*pButton*/, CString& /*strTTText*/) const { return FALSE; }
  virtual BOOL OnBeforeRemoveButton(CMFCToolBarButton*, DROPEFFECT) { return TRUE; }
  virtual void OnUpdateCmdUI(CFrameWnd* pTarget, BOOL bDisableIfNoHndler);
  virtual BOOL IsMenuBar() const { return FALSE; }
  virtual void OnChangeHot(int iHot);
  virtual void DoPaint(CDC* pDC);
  virtual void DrawSeparator(CDC* pDC, const CRect& rect, BOOL bHorz);
  virtual BOOL DrawButton(CDC* pDC, CMFCToolBarButton* pButton, CMFCToolBarImages* pImages, BOOL bHighlighted,
                          BOOL bDrawDisabledImages);
  virtual BOOL OnSendCommand(const CMFCToolBarButton* /*pButton*/) { return FALSE; }
  virtual void ResetImages() {}
  BOOL IsUserDefined() const { return m_bUserDefined; }
  void SetUserDefined(BOOL b) { m_bUserDefined = b; }

  // ----- The application's toolbars, together.
  static void AFXAPI SetSizes(SIZE sizeButton, SIZE sizeImage);
  static void AFXAPI SetMenuSizes(SIZE sizeButton, SIZE sizeImage);
  static CSize AFXAPI GetMenuImageSize() { return m_sizeMenuImage; }
  static CSize AFXAPI GetMenuButtonSize() { return m_sizeMenuButton; }
  static BOOL AFXAPI IsCustomizeMode() { return FALSE; }
  static void AFXAPI EnableQuickCustomization(BOOL bEnable = TRUE) { m_bAltCustomization = bEnable; }
  static void AFXAPI SetBasicCommands(CList<UINT, UINT>& lstCommands);
  static void AFXAPI AddBasicCommand(UINT uiCmd);
  static BOOL AFXAPI IsBasicCommand(UINT uiCmd);
  static const CList<UINT, UINT>& AFXAPI GetBasicCommands() { return m_lstBasicCommands; }
  static void AFXAPI SetNonPermittedCommands(CList<UINT, UINT>& lstCommands);
  static BOOL AFXAPI IsCommandPermitted(UINT uiCmd) { return m_lstUnpermittedCommands.Find(uiCmd) == NULL; }
  static BOOL AFXAPI GetShowTooltips() { return m_bShowTooltips; }
  static void AFXAPI SetShowTooltips(BOOL bValue) { m_bShowTooltips = bValue; }
  static void AFXAPI SetLook2000(BOOL = TRUE) {}
  static BOOL AFXAPI IsLook2000() { return FALSE; }
  static void AFXAPI SetHotTextColor(COLORREF clrText) { m_clrTextHot = clrText; }
  static COLORREF AFXAPI GetHotTextColor() { return m_clrTextHot; }
  static void AFXAPI ResetAllImages();
  static CMFCToolBarImages* AFXAPI GetUserImages() { return m_pUserImages; }
  static void AFXAPI SetUserImages(CMFCToolBarImages* pUserImages) { m_pUserImages = pUserImages; }
  static void AFXAPI AutoGrayInactiveImages(BOOL bEnable = TRUE, int = 0, BOOL = TRUE) { m_bAutoGrayInactiveImages = bEnable; }
  static BOOL AFXAPI IsAutoGrayInactiveImages() { return m_bAutoGrayInactiveImages; }
  static void AFXAPI CleanUpImages();
  static BOOL m_bExtCharTranslation;
  static void AFXAPI SetLargeIcons(BOOL bLargeIcons = TRUE) { m_bLargeIcons = bLargeIcons; }
  static BOOL AFXAPI IsLargeIcons() { return m_bLargeIcons; }
  static BOOL AFXAPI GetDroppedDownMenu(int* = NULL) { return FALSE; }

  static CMFCToolBarImages m_Images, m_ColdImages, m_DisabledImages, m_MenuImages, m_DisabledMenuImages;
  static CMFCToolBarImages* m_pUserImages;
  static BOOL m_bDisableLabelsEdit, m_bDontScaleImages, m_bShowTooltips, m_bShowShortcutKeys;
  static BOOL m_bAltCustomization, m_bLargeIcons, m_bAutoGrayInactiveImages;

  int m_iSelected;

protected:
  void _InitDefault();
  void _AddButtonsFromIDs(const UINT* lpIDArray, int nIDCount, int iFirstImage);
  void _UpdateTooltips();
  CMFCToolBarButton* _CreateCopy(const CMFCToolBarButton& button);
  CFrameWnd* _CommandTarget() const;
  void _SendCommand(CMFCToolBarButton* pButton);
  // Lays the buttons out across or down; the size it takes.
  CSize _Layout(BOOL bHorz, BOOL bApply);
  CRect _GripperRect() const;
  CRect _CustomizeRect() const;
  void _ShowCustomizeMenu();

  virtual BOOL PreTranslateMessage(MSG* pMsg);
  virtual BOOL OnCommand(WPARAM wParam, LPARAM lParam);
  afx_msg int OnCreate(LPCREATESTRUCT lpCreateStruct);
  afx_msg void OnDestroy();
  afx_msg void OnSize(UINT nType, int cx, int cy);
  afx_msg void OnMouseMove(UINT nFlags, CPoint point);
  afx_msg void OnLButtonDown(UINT nFlags, CPoint point);
  afx_msg void OnLButtonUp(UINT nFlags, CPoint point);
  afx_msg void OnLButtonDblClk(UINT nFlags, CPoint point);
  afx_msg void OnCancelMode();
  afx_msg LRESULT OnMouseLeave(WPARAM, LPARAM);
  afx_msg void OnContextMenu(CWnd* pWnd, CPoint point);
  afx_msg LRESULT OnSetFont(WPARAM wParam, LPARAM lParam);
  afx_msg LRESULT OnGetFont(WPARAM, LPARAM);
  afx_msg BOOL OnToolTipText(UINT nID, NMHDR* pNMHDR, LRESULT* pResult);
  DECLARE_MESSAGE_MAP()

  CObList m_Buttons;
  CMFCToolBarImages m_ImagesLocked, m_ColdImagesLocked, m_DisabledImagesLocked;
  CSize m_sizeCurButtonLocked, m_sizeCurImageLocked;
  int m_iHighlighted, m_iButtonCapture;
  BOOL m_bLocked, m_bTextLabels, m_bCustomizeButton, m_bRouteCommandsViaFrame, m_bUserDefined, m_bTracking;
  BOOL m_bHorzLayout;
  CPoint m_ptDragStart;  // where a drag of the gripper began
  int m_iCustomizeCmd;
  CString m_strCustomizeText;
  UINT m_uiOriginalResID;
  CToolTipCtrl* m_pToolTip;
  HFONT m_hFont;

  static CSize m_sizeButton, m_sizeImage, m_sizeMenuButton, m_sizeMenuImage;
  static CList<UINT, UINT> m_lstBasicCommands, m_lstUnpermittedCommands;
  static COLORREF m_clrTextHot;
  static CObList* m_pAllToolbars;
};

typedef CMFCToolBar CMFCToolBarEx;

#endif  // __AFXTOOLBAR_H__
