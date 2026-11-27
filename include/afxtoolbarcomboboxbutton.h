// afxtoolbarcomboboxbutton.h - CMFCToolBarComboBoxButton, a combo box on
// a toolbar: its items with their data, and its selection; picking an
// item sends the button's command to the frame.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#ifndef __AFXTOOLBARCOMBOBOXBUTTON_H__
#define __AFXTOOLBARCOMBOBOXBUTTON_H__

#include <afxtoolbarbutton.h>
#include <afxtempl.h>

class CMFCToolBarComboBoxButton : public CMFCToolBarButton {
  DECLARE_SERIAL(CMFCToolBarComboBoxButton)
public:
  CMFCToolBarComboBoxButton();
  CMFCToolBarComboBoxButton(UINT uiID, int iImage, DWORD dwStyle = CBS_DROPDOWNLIST, int iWidth = 0);
  virtual ~CMFCToolBarComboBoxButton();

  virtual void CopyFrom(const CMFCToolBarButton& src);
  virtual INT_PTR AddItem(LPCTSTR lpszItem, DWORD_PTR dwData = 0);
  INT_PTR GetCount() const { return m_lstItems.GetCount(); }
  LPCTSTR GetItem(int iIndex = -1) const;
  DWORD_PTR GetItemData(int iIndex = -1) const;
  int GetCurSel() const { return m_iSelIndex; }
  virtual BOOL SelectItem(int iIndex, BOOL bNotify = TRUE);
  BOOL SelectItem(DWORD_PTR dwData);
  BOOL SelectItem(LPCTSTR lpszText);
  BOOL DeleteItem(int iIndex);
  virtual void RemoveAllItems();
  int FindItem(LPCTSTR lpszText) const;
  LPCTSTR GetText() const { return m_strEdit; }
  void SetText(LPCTSTR lpszText);
  CComboBox* GetComboBox() const { return m_pWndCombo; }
  virtual BOOL IsWindowVisible();
  virtual HWND GetHwnd() { return m_pWndCombo != NULL ? m_pWndCombo->GetSafeHwnd() : NULL; }
  virtual void EnableWindow(BOOL bEnable = TRUE);
  void SetDropDownHeight(int nHeight) { m_nDropDownHeight = nHeight; }
  virtual BOOL CanBeStretched() const { return TRUE; }
  virtual SIZE OnCalculateSize(CDC* pDC, const CSize& sizeDefault, BOOL bHorz);
  virtual void OnDraw(CDC* pDC, const CRect& rect, CMFCToolBarImages* pImages, BOOL bHorz = TRUE,
                      BOOL bCustomizeMode = FALSE, BOOL bHighlight = FALSE, BOOL bDrawBorder = TRUE,
                      BOOL bGrayDisabledButtons = TRUE);
  virtual void OnMove();
  virtual void OnShow(BOOL bShow);
  virtual void OnChangeParentWnd(CWnd* pWndParent);
  virtual BOOL OnClick(CWnd* pWnd, BOOL bDelay = TRUE);
  // A change from the combo box (CBN_ code), from the toolbar.
  virtual BOOL NotifyCommand(int iNotifyCode);
  virtual CComboBox* CreateCombo(CWnd* pWndParent, const CRect& rect);

  // Every combo button with this command (on any toolbar).
  static CMFCToolBarComboBoxButton* AFXAPI GetByCmd(UINT uiCmd, BOOL bIsFocus = FALSE);
  static BOOL AFXAPI SelectItemAll(UINT uiCmd, int iIndex);
  static int AFXAPI GetCountAll(UINT uiCmd);
  static int AFXAPI GetCurSelAll(UINT uiCmd);
  static LPCTSTR AFXAPI GetItemAll(UINT uiCmd, int iIndex);

  static BOOL m_bFlat, m_bCenterVert;

protected:
  void _SyncCombo();

  DWORD m_dwStyle;
  int m_iWidth, m_iSelIndex, m_nDropDownHeight;
  CStringList m_lstItems;
  CList<DWORD_PTR, DWORD_PTR> m_lstItemData;
  CString m_strEdit;
  CComboBox* m_pWndCombo;
  BOOL m_bInSync;
};

#endif  // __AFXTOOLBARCOMBOBOXBUTTON_H__
