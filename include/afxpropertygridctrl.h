// afxpropertygridctrl.h - CMFCPropertyGridCtrl, the Feature Pack's
// property list: named values in two columns, in groups or sorted by
// name, edited in place (text, numbers, a list of options, true and
// false) or through a button (CMFCPropertyGridColorProperty,
// CMFCPropertyGridFontProperty, CMFCPropertyGridFileProperty), with a
// header and a description area. A change goes to the owner as
// AFX_WM_PROPERTY_CHANGED (wParam the control's ID, lParam the property).
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#ifndef __AFXPROPERTYGRIDCTRL_H__
#define __AFXPROPERTYGRIDCTRL_H__

#ifndef __AFXWIN_H__
#include <afxwin.h>
#endif
#include <afxcmn.h>
#include <afxdisp.h>
#include <afxtempl.h>
#include <afxheaderctrl.h>
#include <afxglobals.h>

extern UINT AFX_WM_PROPERTY_CHANGED;

class CMFCPropertyGridCtrl;

class CMFCPropertyGridProperty : public CObject {
  DECLARE_DYNAMIC(CMFCPropertyGridProperty)
  friend class CMFCPropertyGridCtrl;

public:
  enum ClickArea { ClickExpandBox, ClickName, ClickValue, ClickDescription };

  // A group (or, with bIsValueList, a value made of its sub-items).
  CMFCPropertyGridProperty(const CString& strGroupName, DWORD_PTR dwData = 0, BOOL bIsValueList = FALSE);
  // A value.
  CMFCPropertyGridProperty(const CString& strName, const COleVariant& varValue, LPCTSTR lpszDescr = NULL,
                           DWORD_PTR dwData = 0, LPCTSTR lpszEditMask = NULL, LPCTSTR lpszEditTemplate = NULL,
                           LPCTSTR lpszValidChars = NULL);
  virtual ~CMFCPropertyGridProperty();

  // ----- Sub-items and options.
  BOOL AddSubItem(CMFCPropertyGridProperty* pProp);
  BOOL RemoveSubItem(CMFCPropertyGridProperty*& pProp, BOOL bDelete = TRUE);
  int GetSubItemsCount() const { return (int)m_lstSubItems.GetCount(); }
  CMFCPropertyGridProperty* GetSubItem(int nIndex) const;
  CMFCPropertyGridProperty* GetParent() const { return m_pParent; }
  CMFCPropertyGridProperty* FindSubItemByData(DWORD_PTR dwData) const;
  BOOL AddOption(LPCTSTR lpszOption, BOOL bInsertUnique = TRUE);
  void RemoveAllOptions() { m_lstOptions.RemoveAll(); }
  int GetOptionCount() const { return (int)m_lstOptions.GetCount(); }
  LPCTSTR GetOption(int nIndex) const;

  // ----- The value.
  virtual const COleVariant& GetValue() const { return m_varValue; }
  virtual void SetValue(const COleVariant& varValue);
  const COleVariant& GetOriginalValue() const { return m_varValueOrig; }
  void SetOriginalValue(const COleVariant& varValue);
  void ResetOriginalValue();
  BOOL IsModified() const { return m_bIsModified; }
  virtual BOOL IsValueChanged() const;

  // ----- The rest of it.
  LPCTSTR GetName() const { return m_strName; }
  void SetName(LPCTSTR lpszName, BOOL bRedraw = TRUE);
  const CString& GetDescription() const { return m_strDescr; }
  void SetDescription(const CString& strDescr) { m_strDescr = strDescr; }
  DWORD_PTR GetData() const { return m_dwData; }
  void SetData(DWORD_PTR dwData) { m_dwData = dwData; }
  void AllowEdit(BOOL bAllow = TRUE) { m_bAllowEdit = bAllow; }
  BOOL IsAllowEdit() const { return m_bAllowEdit; }
  BOOL IsGroup() const { return m_bGroup; }
  BOOL IsExpanded() const { return m_bExpanded; }
  void Expand(BOOL bExpand = TRUE);
  BOOL IsEnabled() const { return m_bEnabled; }
  void Enable(BOOL bEnable = TRUE);
  BOOL IsVisible() const { return m_bVisible; }
  void Show(BOOL bShow = TRUE, BOOL bAdjustLayout = TRUE);
  BOOL IsSelected() const;
  BOOL IsInPlaceEditing() const { return m_bInPlaceEdit; }
  BOOL IsParentExpanded() const;
  int GetHierarchyLevel() const;
  CRect GetRect() const { return m_Rect; }
  void Redraw();
  CMFCPropertyGridProperty* HitTest(CPoint point, ClickArea* pnArea = NULL);

  // ----- Formatting and editing.
  virtual CString FormatProperty();
  virtual BOOL TextToVar(const CString& strText);
  virtual BOOL OnUpdateValue();
  virtual BOOL OnEdit(LPPOINT lptClick);
  virtual BOOL OnEndEdit();
  virtual void OnClickButton(CPoint point);
  virtual BOOL OnDblClk(CPoint point);
  virtual void OnSelectCombo();
  virtual BOOL HasButton() const;
  virtual BOOL OnSetCursor() const { return FALSE; }
  virtual CWnd* CreateInPlaceEdit(CRect rectEdit, BOOL& bDefaultFormat);
  virtual CComboBox* CreateCombo(CWnd* pWndParent, CRect rect);
  virtual void OnDrawName(CDC* pDC, CRect rect);
  virtual void OnDrawValue(CDC* pDC, CRect rect);
  virtual void OnDrawExpandBox(CDC* pDC, CRect rectExpand);
  virtual void OnDrawButton(CDC* pDC, CRect rectButton);
  virtual void OnDrawDescription(CDC* pDC, CRect rect);

  // How numbers are shown.
  static CString m_strFormatChar, m_strFormatShort, m_strFormatLong, m_strFormatUShort, m_strFormatULong,
      m_strFormatFloat, m_strFormatDouble;

protected:
  void _Init();
  void _SetOwnerList(CMFCPropertyGridCtrl* pWndList);
  void _Reposition(int& y);
  void _CollectVisible(CArray<CMFCPropertyGridProperty*, CMFCPropertyGridProperty*>& ar);
  BOOL _IsSubItem(CMFCPropertyGridProperty* pProp) const;
  BOOL _IsValueList() const { return m_bIsValueList; }
  virtual void AdjustButtonRect();
  virtual void AdjustInPlaceEditRect(CRect& rectEdit, CRect& rectSpin);

  CString m_strName, m_strDescr, m_strEditMask, m_strEditTempl, m_strValidChars;
  COleVariant m_varValue, m_varValueOrig;
  DWORD_PTR m_dwData;
  BOOL m_bGroup, m_bIsValueList, m_bExpanded, m_bEnabled, m_bVisible, m_bAllowEdit, m_bIsModified;
  BOOL m_bInPlaceEdit, m_bButtonIsDown, m_bNameIsTruncated, m_bValueIsTruncated;
  CStringList m_lstOptions;
  CList<CMFCPropertyGridProperty*, CMFCPropertyGridProperty*> m_lstSubItems;
  CMFCPropertyGridProperty* m_pParent;
  CMFCPropertyGridCtrl* m_pWndList;
  CWnd* m_pWndInPlace;
  CComboBox* m_pWndCombo;
  CRect m_Rect, m_rectButton;
};

class CMFCPropertyGridColorProperty : public CMFCPropertyGridProperty {
  DECLARE_DYNAMIC(CMFCPropertyGridColorProperty)
public:
  CMFCPropertyGridColorProperty(const CString& strName, const COLORREF& color, CPalette* pPalette = NULL,
                                LPCTSTR lpszDescr = NULL, DWORD_PTR dwData = 0);
  virtual ~CMFCPropertyGridColorProperty();

  COLORREF GetColor() const { return m_Color; }
  void SetColor(COLORREF color);
  void EnableAutomaticButton(LPCTSTR lpszLabel, COLORREF colorAutomatic, BOOL bEnable = TRUE);
  void EnableOtherButton(LPCTSTR lpszLabel, BOOL bAltColorDlg = TRUE, BOOL bEnable = TRUE);
  void SetColumnsNumber(int nColumnsNumber) { m_nColumnsNumber = nColumnsNumber; }

  virtual void OnDrawValue(CDC* pDC, CRect rect);
  virtual void OnClickButton(CPoint point);
  virtual BOOL OnEdit(LPPOINT lptClick);
  virtual BOOL OnUpdateValue();
  virtual CString FormatProperty();
  virtual BOOL TextToVar(const CString& strText);
  virtual BOOL HasButton() const { return TRUE; }

protected:
  virtual void AdjustInPlaceEditRect(CRect& rectEdit, CRect& rectSpin);

  COLORREF m_Color, m_ColorOrig, m_ColorAutomatic;
  CString m_strAutoColor, m_strOtherColor;
  BOOL m_bStdColorDlg;
  int m_nColumnsNumber;
};

class CMFCPropertyGridFontProperty : public CMFCPropertyGridProperty {
  DECLARE_DYNAMIC(CMFCPropertyGridFontProperty)
public:
  CMFCPropertyGridFontProperty(const CString& strName, LOGFONT& lf,
                               DWORD dwFontDialogFlags = CF_EFFECTS | CF_SCREENFONTS, LPCTSTR lpszDescr = NULL,
                               DWORD_PTR dwData = 0, COLORREF color = (COLORREF)-1);
  virtual ~CMFCPropertyGridFontProperty();

  LPLOGFONT GetLogFont() { return &m_lf; }
  COLORREF GetColor() const { return m_Color; }

  virtual void OnClickButton(CPoint point);
  virtual CString FormatProperty();
  virtual BOOL HasButton() const { return TRUE; }

protected:
  LOGFONT m_lf, m_lfOrig;
  DWORD m_dwSelFontFlags;
  COLORREF m_Color;
};

class CMFCPropertyGridFileProperty : public CMFCPropertyGridProperty {
  DECLARE_DYNAMIC(CMFCPropertyGridFileProperty)
public:
  // A folder.
  CMFCPropertyGridFileProperty(const CString& strName, const CString& strFolderName, DWORD_PTR dwData = 0,
                               LPCTSTR lpszDescr = NULL);
  // A file to open or save.
  CMFCPropertyGridFileProperty(const CString& strName, BOOL bOpenFileDialog, const CString& strFileName,
                               LPCTSTR lpszDefExt = NULL, DWORD dwFlags = OFN_HIDEREADONLY | OFN_OVERWRITEPROMPT,
                               LPCTSTR lpszFilter = NULL, LPCTSTR lpszDescr = NULL, DWORD_PTR dwData = 0);
  virtual ~CMFCPropertyGridFileProperty();

  virtual void OnClickButton(CPoint point);
  virtual BOOL HasButton() const { return TRUE; }

protected:
  BOOL m_bIsFolder, m_bOpenFileDialog;
  DWORD m_dwFileOpenFlags;
  CString m_strDefExt, m_strFilter;
};

class CMFCPropertyGridCtrl : public CWnd {
  DECLARE_DYNAMIC(CMFCPropertyGridCtrl)
  friend class CMFCPropertyGridProperty;

public:
  CMFCPropertyGridCtrl();
  virtual ~CMFCPropertyGridCtrl();

  virtual BOOL Create(DWORD dwStyle, const RECT& rect, CWnd* pParentWnd, UINT nID);

  // ----- The properties.
  int AddProperty(CMFCPropertyGridProperty* pProp, BOOL bRedraw = TRUE, BOOL bAdjustLayout = TRUE);
  BOOL DeleteProperty(CMFCPropertyGridProperty*& pProp, BOOL bRedraw = TRUE, BOOL bAdjustLayout = TRUE);
  void RemoveAll();
  CMFCPropertyGridProperty* GetProperty(int nIndex) const;
  int GetPropertyCount() const { return (int)m_lstProps.GetCount(); }
  CMFCPropertyGridProperty* FindItemByData(DWORD_PTR dwData, BOOL bSearchSubItems = TRUE) const;
  CMFCPropertyGridProperty* HitTest(CPoint pt, CMFCPropertyGridProperty::ClickArea* pnArea = NULL,
                                    BOOL bPropsOnly = FALSE) const;

  // ----- The selection and editing.
  void SetCurSel(CMFCPropertyGridProperty* pProp, BOOL bRedraw = TRUE);
  CMFCPropertyGridProperty* GetCurSel() const { return m_pSel; }
  void EnsureVisible(CMFCPropertyGridProperty* pProp, BOOL bExpandParents = FALSE);
  void ExpandAll(BOOL bExpand = TRUE);
  virtual BOOL EditItem(CMFCPropertyGridProperty* pProp, LPPOINT lptClick = NULL);
  virtual BOOL EndEditItem(BOOL bUpdateData = TRUE);
  virtual void OnPropertyChanged(CMFCPropertyGridProperty* pProp) const;

  // ----- The look.
  void EnableHeaderCtrl(BOOL bEnable = TRUE, LPCTSTR lpszLeftColumn = _T("Property"),
                        LPCTSTR lpszRightColumn = _T("Value"));
  BOOL IsHeaderCtrl() const { return m_bHeaderCtrl; }
  CMFCHeaderCtrl& GetHeaderCtrl() { return m_wndHeader; }
  void EnableDescriptionArea(BOOL bEnable = TRUE);
  BOOL IsDescriptionArea() const { return m_bDescriptionArea; }
  int GetDescriptionHeight() const { return m_nDescrHeight; }
  int GetDescriptionRows() const { return m_nDescrRows; }
  void SetDescriptionRows(int nDescRows);
  void SetAlphabeticMode(BOOL bSet = TRUE);
  BOOL IsAlphabeticMode() const { return m_bAlphabeticMode; }
  void SetVSDotNetLook(BOOL bSet = TRUE);
  BOOL IsVSDotNetLook() const { return m_bVSDotNetLook; }
  void MarkModifiedProperties(BOOL bMark = TRUE, BOOL bRedraw = TRUE);
  BOOL IsMarkModifiedProperties() const { return m_bMarkModifiedProperties; }
  void ResetOriginalValues(BOOL bRedraw = TRUE);
  void SetBoolLabels(LPCTSTR lpszTrue, LPCTSTR lpszFalse);
  void SetListDelimiter(TCHAR c) { m_cListDelimeter = c; }
  TCHAR GetListDelimiter() const { return m_cListDelimeter; }
  void SetCustomColors(COLORREF clrBackground, COLORREF clrText, COLORREF clrGroupBackground,
                       COLORREF clrGroupText, COLORREF clrDescriptionBackground, COLORREF clrDescriptionText,
                       COLORREF clrLine);
  void GetCustomColors(COLORREF& clrBackground, COLORREF& clrText, COLORREF& clrGroupBackground,
                       COLORREF& clrGroupText, COLORREF& clrDescriptionBackground, COLORREF& clrDescriptionText,
                       COLORREF& clrLine);
  COLORREF GetTextColor() const { return m_clrText == (COLORREF)-1 ? afxGlobalData.clrWindowText : m_clrText; }
  COLORREF GetBkColor() const { return m_clrBackground == (COLORREF)-1 ? afxGlobalData.clrWindow : m_clrBackground; }
  COLORREF GetGroupBkColor() const;
  COLORREF GetGroupTextColor() const;
  COLORREF GetLineColor() const;
  int GetLeftColumnWidth() const { return m_nLeftColumnWidth; }
  void SetLeftColumnWidth(int cx);
  int GetRowHeight() const { return m_nRowHeight; }
  int GetListRect(CRect& rect) const;
  CFont& GetBoldFont() { return m_fontBold; }
  HFONT GetFont() const { return m_hFont != NULL ? m_hFont : (HFONT)afxGlobalData.fontRegular.GetSafeHandle(); }
  const CString& GetTrueLabel() const { return m_strTrue; }
  const CString& GetFalseLabel() const { return m_strFalse; }

  virtual void AdjustLayout();
  virtual int CompareProps(const CMFCPropertyGridProperty* pProp1, const CMFCPropertyGridProperty* pProp2) const;

protected:
  virtual void Init();
  virtual void PreSubclassWindow();
  virtual void OnFillBackground(CDC* pDC, CRect rectClient);
  virtual void OnDrawList(CDC* pDC);
  virtual int OnDrawProperty(CDC* pDC, CMFCPropertyGridProperty* pProp) const;
  virtual void OnDrawDescription(CDC* pDC, CRect rect);
  virtual BOOL PreTranslateMessage(MSG* pMsg);
  virtual BOOL OnNotify(WPARAM wParam, LPARAM lParam, LRESULT* pResult);
  virtual BOOL OnCommand(WPARAM wParam, LPARAM lParam);

  void _BuildRows();
  void _UpdateScroll();
  void _CreateFonts();
  int _VisibleRows() const;

  afx_msg int OnCreate(LPCREATESTRUCT lpCreateStruct);
  afx_msg void OnDestroy();
  afx_msg void OnSize(UINT nType, int cx, int cy);
  afx_msg void OnPaint();
  afx_msg BOOL OnEraseBkgnd(CDC* pDC);
  afx_msg void OnLButtonDown(UINT nFlags, CPoint point);
  afx_msg void OnLButtonUp(UINT nFlags, CPoint point);
  afx_msg void OnLButtonDblClk(UINT nFlags, CPoint point);
  afx_msg void OnMouseMove(UINT nFlags, CPoint point);
  afx_msg void OnVScroll(UINT nSBCode, UINT nPos, CScrollBar* pScrollBar);
  afx_msg BOOL OnMouseWheel(UINT nFlags, short zDelta, CPoint pt);
  afx_msg void OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags);
  afx_msg UINT OnGetDlgCode();
  afx_msg void OnSetFocus(CWnd* pOldWnd);
  afx_msg void OnKillFocus(CWnd* pNewWnd);
  afx_msg BOOL OnSetCursor(CWnd* pWnd, UINT nHitTest, UINT message);
  afx_msg void OnCancelMode();
  afx_msg LRESULT OnSetFont(WPARAM, LPARAM);
  afx_msg LRESULT OnGetFont(WPARAM, LPARAM);
  DECLARE_MESSAGE_MAP()

  CList<CMFCPropertyGridProperty*, CMFCPropertyGridProperty*> m_lstProps;
  CArray<CMFCPropertyGridProperty*, CMFCPropertyGridProperty*> m_arRows;  // shown, in order
  CMFCPropertyGridProperty* m_pSel;
  CMFCHeaderCtrl m_wndHeader;
  CScrollBar m_wndScrollVert;
  CFont m_fontBold;
  HFONT m_hFont;
  CString m_strTrue, m_strFalse, m_strLeftColumn, m_strRightColumn;
  TCHAR m_cListDelimeter;
  BOOL m_bHeaderCtrl, m_bDescriptionArea, m_bAlphabeticMode, m_bVSDotNetLook, m_bMarkModifiedProperties;
  BOOL m_bTracking, m_bFocused, m_bInitialized;
  int m_nLeftColumnWidth, m_nRowHeight, m_nHeaderHeight, m_nDescrHeight, m_nDescrRows, m_nVertScrollOffset;
  int m_nTrackOffset;
  CRect m_rectList, m_rectDescr;
  COLORREF m_clrBackground, m_clrText, m_clrGroupBackground, m_clrGroupText, m_clrDescriptionBackground,
      m_clrDescriptionText, m_clrLine;
};

#endif  // __AFXPROPERTYGRIDCTRL_H__
