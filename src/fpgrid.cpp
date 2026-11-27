// fpgrid.cpp - CMFCPropertyGridCtrl and its properties: plain values,
// groups and value lists, colors, fonts and files.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include "afximpl.h"
#include <afxpropertygridctrl.h>
#include <afxmaskededit.h>
#include <afxvisualmanager.h>
#include <afxwinappex.h>
#include <afxdlgs.h>
#include <math.h>
#include <stdlib.h>

UINT AFX_WM_PROPERTY_CHANGED = ::RegisterWindowMessage(_T("AFX_WM_PROPERTY_CHANGED"));

enum { kInPlaceID = 3, kComboID = 4, kHeaderID = 5 };

#ifdef _WINPORT  // (LONG and ULONG are int there)
CString CMFCPropertyGridProperty::m_strFormatLong = _T("%d");
CString CMFCPropertyGridProperty::m_strFormatULong = _T("%u");
#else
CString CMFCPropertyGridProperty::m_strFormatLong = _T("%ld");
CString CMFCPropertyGridProperty::m_strFormatULong = _T("%lu");
#endif
CString CMFCPropertyGridProperty::m_strFormatChar = _T("%c");
CString CMFCPropertyGridProperty::m_strFormatShort = _T("%d");
CString CMFCPropertyGridProperty::m_strFormatUShort = _T("%u");
CString CMFCPropertyGridProperty::m_strFormatFloat = _T("%f");
CString CMFCPropertyGridProperty::m_strFormatDouble = _T("%lf");

// ---------------------------------------------------------------------
// The visual manager's property grids.

COLORREF CMFCVisualManager::GetPropertyGridGroupColor(CMFCPropertyGridCtrl* pPropList) {
  return pPropList->IsVSDotNetLook() ? afxGlobalData.clrBtnFace : afxGlobalData.clrBtnFace;
}

COLORREF CMFCVisualManager::GetPropertyGridGroupTextColor(CMFCPropertyGridCtrl*) { return afxGlobalData.clrBtnText; }

COLORREF CMFCVisualManager::OnFillPropertyGridGroup(CDC* pDC, CMFCPropertyGridCtrl* pPropList, CRect rect) {
  pDC->FillSolidRect(rect, pPropList->GetGroupBkColor());
  return pPropList->GetGroupTextColor();
}

// ---------------------------------------------------------------------
// CMFCPropertyGridProperty

IMPLEMENT_DYNAMIC(CMFCPropertyGridProperty, CObject)

void CMFCPropertyGridProperty::_Init() {
  m_dwData = 0;
  m_bGroup = m_bIsValueList = FALSE;
  m_bExpanded = TRUE;
  m_bEnabled = m_bVisible = m_bAllowEdit = TRUE;
  m_bIsModified = m_bInPlaceEdit = m_bButtonIsDown = m_bNameIsTruncated = m_bValueIsTruncated = FALSE;
  m_pParent = NULL;
  m_pWndList = NULL;
  m_pWndInPlace = NULL;
  m_pWndCombo = NULL;
  m_Rect.SetRectEmpty();
  m_rectButton.SetRectEmpty();
}

CMFCPropertyGridProperty::CMFCPropertyGridProperty(const CString& strGroupName, DWORD_PTR dwData,
                                                   BOOL bIsValueList) {
  _Init();
  m_strName = strGroupName;
  m_dwData = dwData;
  m_bGroup = TRUE;
  m_bIsValueList = bIsValueList;
}

CMFCPropertyGridProperty::CMFCPropertyGridProperty(const CString& strName, const COleVariant& varValue,
                                                   LPCTSTR lpszDescr, DWORD_PTR dwData, LPCTSTR lpszEditMask,
                                                   LPCTSTR lpszEditTemplate, LPCTSTR lpszValidChars)
    : m_varValue(varValue), m_varValueOrig(varValue) {
  _Init();
  m_strName = strName;
  m_strDescr = lpszDescr != NULL ? lpszDescr : _T("");
  m_dwData = dwData;
  m_strEditMask = lpszEditMask != NULL ? lpszEditMask : _T("");
  m_strEditTempl = lpszEditTemplate != NULL ? lpszEditTemplate : _T("");
  m_strValidChars = lpszValidChars != NULL ? lpszValidChars : _T("");
}

CMFCPropertyGridProperty::~CMFCPropertyGridProperty() {
  while (!m_lstSubItems.IsEmpty()) delete m_lstSubItems.RemoveHead();
  OnEndEdit();
}

void CMFCPropertyGridProperty::_SetOwnerList(CMFCPropertyGridCtrl* pWndList) {
  m_pWndList = pWndList;
  for (POSITION pos = m_lstSubItems.GetHeadPosition(); pos != NULL;)
    m_lstSubItems.GetNext(pos)->_SetOwnerList(pWndList);
}

BOOL CMFCPropertyGridProperty::AddSubItem(CMFCPropertyGridProperty* pProp) {
  if (pProp == NULL) return FALSE;
  if (!m_bGroup && !m_bIsValueList) {
    // A property with sub-items: shown expandable, not a group band.
  }
  pProp->m_pParent = this;
  pProp->_SetOwnerList(m_pWndList);
  m_lstSubItems.AddTail(pProp);
  return TRUE;
}

BOOL CMFCPropertyGridProperty::_IsSubItem(CMFCPropertyGridProperty* pProp) const {
  for (POSITION pos = m_lstSubItems.GetHeadPosition(); pos != NULL;) {
    CMFCPropertyGridProperty* p = m_lstSubItems.GetNext(pos);
    if (p == pProp || p->_IsSubItem(pProp)) return TRUE;
  }
  return FALSE;
}

BOOL CMFCPropertyGridProperty::RemoveSubItem(CMFCPropertyGridProperty*& pProp, BOOL bDelete) {
  POSITION pos = m_lstSubItems.Find(pProp);
  if (pos == NULL) return FALSE;
  if (m_pWndList != NULL && (m_pWndList->m_pSel == pProp || pProp->_IsSubItem(m_pWndList->m_pSel)))
    m_pWndList->SetCurSel(NULL, FALSE);
  m_lstSubItems.RemoveAt(pos);
  if (bDelete) {
    delete pProp;
    pProp = NULL;
  } else {
    pProp->m_pParent = NULL;
  }
  if (m_pWndList != NULL) m_pWndList->AdjustLayout();
  return TRUE;
}

CMFCPropertyGridProperty* CMFCPropertyGridProperty::GetSubItem(int nIndex) const {
  POSITION pos = m_lstSubItems.FindIndex(nIndex);
  return pos != NULL ? m_lstSubItems.GetAt(pos) : NULL;
}

CMFCPropertyGridProperty* CMFCPropertyGridProperty::FindSubItemByData(DWORD_PTR dwData) const {
  for (POSITION pos = m_lstSubItems.GetHeadPosition(); pos != NULL;) {
    CMFCPropertyGridProperty* p = m_lstSubItems.GetNext(pos);
    if (p->m_dwData == dwData) return p;
    CMFCPropertyGridProperty* pFound = p->FindSubItemByData(dwData);
    if (pFound != NULL) return pFound;
  }
  return NULL;
}

BOOL CMFCPropertyGridProperty::AddOption(LPCTSTR lpszOption, BOOL bInsertUnique) {
  if (bInsertUnique && m_lstOptions.Find(lpszOption) != NULL) return FALSE;
  m_lstOptions.AddTail(lpszOption);
  return TRUE;
}

LPCTSTR CMFCPropertyGridProperty::GetOption(int nIndex) const {
  POSITION pos = m_lstOptions.FindIndex(nIndex);
  return pos != NULL ? (LPCTSTR)m_lstOptions.GetAt(pos) : NULL;
}

void CMFCPropertyGridProperty::SetValue(const COleVariant& varValue) {
  if (m_varValue.vt != VT_EMPTY && m_varValue.vt != varValue.vt) {
    // The type stays: the new value converts to it.
    COleVariant var(varValue);
    TRY { var.ChangeType(m_varValue.vt); }
    CATCH_ALL(e) { return; }
    END_CATCH_ALL
    m_varValue = var;
  } else {
    m_varValue = varValue;
  }
  m_bIsModified = IsValueChanged();
  Redraw();
}

void CMFCPropertyGridProperty::SetOriginalValue(const COleVariant& varValue) {
  m_varValueOrig = varValue;
  m_bIsModified = IsValueChanged();
}

void CMFCPropertyGridProperty::ResetOriginalValue() {
  m_bIsModified = FALSE;
  m_varValue = m_varValueOrig;
  for (POSITION pos = m_lstSubItems.GetHeadPosition(); pos != NULL;) m_lstSubItems.GetNext(pos)->ResetOriginalValue();
}

BOOL CMFCPropertyGridProperty::IsValueChanged() const {
  if (m_varValueOrig.vt != m_varValue.vt) return TRUE;
  return !(m_varValue == m_varValueOrig);
}

void CMFCPropertyGridProperty::SetName(LPCTSTR lpszName, BOOL bRedraw) {
  m_strName = lpszName;
  if (bRedraw) Redraw();
}

void CMFCPropertyGridProperty::Expand(BOOL bExpand) {
  if (m_bExpanded == bExpand) return;
  m_bExpanded = bExpand;
  if (m_pWndList != NULL && m_pWndList->m_hWnd != NULL) {
    // The selection leaves a hidden sub-item.
    if (!bExpand && _IsSubItem(m_pWndList->m_pSel)) m_pWndList->SetCurSel(this, FALSE);
    m_pWndList->AdjustLayout();
  }
}

void CMFCPropertyGridProperty::Enable(BOOL bEnable) {
  m_bEnabled = bEnable;
  Redraw();
}

void CMFCPropertyGridProperty::Show(BOOL bShow, BOOL bAdjustLayout) {
  m_bVisible = bShow;
  if (bAdjustLayout && m_pWndList != NULL && m_pWndList->m_hWnd != NULL) m_pWndList->AdjustLayout();
}

BOOL CMFCPropertyGridProperty::IsSelected() const { return m_pWndList != NULL && m_pWndList->m_pSel == this; }

BOOL CMFCPropertyGridProperty::IsParentExpanded() const {
  for (CMFCPropertyGridProperty* p = m_pParent; p != NULL; p = p->m_pParent)
    if (!p->m_bExpanded) return FALSE;
  return TRUE;
}

int CMFCPropertyGridProperty::GetHierarchyLevel() const {
  int n = 0;
  for (CMFCPropertyGridProperty* p = m_pParent; p != NULL; p = p->m_pParent) n++;
  return n;
}

void CMFCPropertyGridProperty::Redraw() {
  if (m_pWndList != NULL && m_pWndList->m_hWnd != NULL && !m_Rect.IsRectEmpty()) {
    m_pWndList->InvalidateRect(m_Rect);
    if (IsSelected() && m_pWndList->m_bDescriptionArea) m_pWndList->InvalidateRect(m_pWndList->m_rectDescr);
  }
}

CMFCPropertyGridProperty* CMFCPropertyGridProperty::HitTest(CPoint point, ClickArea* pnArea) {
  if (m_Rect.PtInRect(point)) {
    if (pnArea != NULL) {
      int nRow = m_pWndList != NULL ? m_pWndList->m_nRowHeight : 16;
      int xExpand = m_Rect.left + (GetHierarchyLevel() + 1) * nRow;
      if (m_pWndList != NULL && m_pWndList->m_bAlphabeticMode) xExpand -= nRow * GetHierarchyLevel();
      if (!m_lstSubItems.IsEmpty() && point.x < xExpand && point.x >= xExpand - nRow)
        *pnArea = ClickExpandBox;
      else if (m_bGroup && !m_bIsValueList)
        *pnArea = ClickName;
      else if (m_pWndList != NULL && point.x > m_Rect.left + m_pWndList->m_nLeftColumnWidth)
        *pnArea = ClickValue;
      else
        *pnArea = ClickName;
    }
    return this;
  }
  return NULL;
}

void CMFCPropertyGridProperty::_CollectVisible(CArray<CMFCPropertyGridProperty*, CMFCPropertyGridProperty*>& ar) {
  if (!m_bVisible) return;
  ar.Add(this);
  if (!m_bExpanded) return;
  for (POSITION pos = m_lstSubItems.GetHeadPosition(); pos != NULL;) m_lstSubItems.GetNext(pos)->_CollectVisible(ar);
}

void CMFCPropertyGridProperty::_Reposition(int&) {}

static CString AfxFormatVar(const COleVariant& var, const CString& strTrue, const CString& strFalse) {
  CString str;
  switch (var.vt) {
    case VT_BSTR:
      return CString(var.bstrVal);
    case VT_I1:
      str.Format(CMFCPropertyGridProperty::m_strFormatChar, (int)var.cVal);
      return str;
    case VT_UI1:
      str.Format(_T("%u"), (unsigned)var.bVal);
      return str;
    case VT_I2:
      str.Format(CMFCPropertyGridProperty::m_strFormatShort, (int)var.iVal);
      return str;
    case VT_UI2:
      str.Format(CMFCPropertyGridProperty::m_strFormatUShort, (unsigned)var.uiVal);
      return str;
    case VT_I4:
      str.Format(CMFCPropertyGridProperty::m_strFormatLong, var.lVal);
      return str;
    case VT_UI4:
      str.Format(CMFCPropertyGridProperty::m_strFormatULong, var.ulVal);
      return str;
    case VT_INT:
      str.Format(_T("%d"), var.intVal);
      return str;
    case VT_UINT:
      str.Format(_T("%u"), var.uintVal);
      return str;
    case VT_R4:
      str.Format(CMFCPropertyGridProperty::m_strFormatFloat, (double)var.fltVal);
      return str;
    case VT_R8:
      str.Format(CMFCPropertyGridProperty::m_strFormatDouble, var.dblVal);
      return str;
    case VT_BOOL:
      return var.boolVal != VARIANT_FALSE ? strTrue : strFalse;
    case VT_EMPTY:
    case VT_NULL:
      return CString();
    default: {
      COleVariant v(var);
      TRY { v.ChangeType(VT_BSTR); }
      CATCH_ALL(e) { return CString(); }
      END_CATCH_ALL
      return CString(v.bstrVal);
    }
  }
}

CString CMFCPropertyGridProperty::FormatProperty() {
  if (m_bIsValueList) {
    // The sub-items' values, in a list.
    CString str;
    TCHAR c = m_pWndList != NULL ? m_pWndList->m_cListDelimeter : _T(',');
    for (POSITION pos = m_lstSubItems.GetHeadPosition(); pos != NULL;) {
      CMFCPropertyGridProperty* p = m_lstSubItems.GetNext(pos);
      str += p->FormatProperty();
      if (pos != NULL) {
        str += c;
        str += _T(' ');
      }
    }
    return str;
  }
  CString strTrue = m_pWndList != NULL ? m_pWndList->m_strTrue : CString(_T("True"));
  CString strFalse = m_pWndList != NULL ? m_pWndList->m_strFalse : CString(_T("False"));
  return AfxFormatVar(m_varValue, strTrue, strFalse);
}

BOOL CMFCPropertyGridProperty::TextToVar(const CString& strTextIn) {
  CString strText = strTextIn;
  strText.TrimLeft();
  strText.TrimRight();
  if (m_bIsValueList) {
    TCHAR c = m_pWndList != NULL ? m_pWndList->m_cListDelimeter : _T(',');
    int nStart = 0;
    for (POSITION pos = m_lstSubItems.GetHeadPosition(); pos != NULL;) {
      CMFCPropertyGridProperty* p = m_lstSubItems.GetNext(pos);
      int n = strText.Find(c, nStart);
      CString strItem = n < 0 ? strText.Mid(nStart) : strText.Mid(nStart, n - nStart);
      if (n < 0 && pos != NULL) return FALSE;  // too few
      if (!p->TextToVar(strItem)) return FALSE;
      nStart = n + 1;
    }
    return TRUE;
  }
  LPTSTR pEnd = NULL;
  switch (m_varValue.vt) {
    case VT_BSTR:
      m_varValue = COleVariant((LPCTSTR)strTextIn);
      return TRUE;
    case VT_I1:
    case VT_UI1:
    case VT_I2:
    case VT_UI2:
    case VT_I4:
    case VT_UI4:
    case VT_INT:
    case VT_UINT: {
      if (strText.IsEmpty()) return FALSE;
      BOOL bUnsigned = m_varValue.vt == VT_UI1 || m_varValue.vt == VT_UI2 || m_varValue.vt == VT_UI4 ||
                       m_varValue.vt == VT_UINT;
      if (bUnsigned && strText[0] == _T('-')) return FALSE;
      long long n = bUnsigned ? (long long)_tcstoul(strText, &pEnd, 10) : (long long)_tcstol(strText, &pEnd, 10);
      if (pEnd == NULL || *pEnd != 0) return FALSE;
      switch (m_varValue.vt) {
        case VT_I1:
          m_varValue.cVal = (CHAR)n;
          break;
        case VT_UI1:
          m_varValue.bVal = (BYTE)n;
          break;
        case VT_I2:
          m_varValue.iVal = (SHORT)n;
          break;
        case VT_UI2:
          m_varValue.uiVal = (USHORT)n;
          break;
        case VT_I4:
          m_varValue.lVal = (LONG)n;
          break;
        case VT_UI4:
          m_varValue.ulVal = (ULONG)n;
          break;
        case VT_INT:
          m_varValue.intVal = (INT)n;
          break;
        default:
          m_varValue.uintVal = (UINT)n;
      }
      return TRUE;
    }
    case VT_R4:
    case VT_R8: {
      if (strText.IsEmpty()) return FALSE;
      double d = _tcstod(strText, &pEnd);
      if (pEnd == NULL || *pEnd != 0) return FALSE;
      if (m_varValue.vt == VT_R4)
        m_varValue.fltVal = (float)d;
      else
        m_varValue.dblVal = d;
      return TRUE;
    }
    case VT_BOOL: {
      CString strTrue = m_pWndList != NULL ? m_pWndList->m_strTrue : CString(_T("True"));
      CString strFalse = m_pWndList != NULL ? m_pWndList->m_strFalse : CString(_T("False"));
      if (strText.CompareNoCase(strTrue) == 0)
        m_varValue.boolVal = VARIANT_TRUE;
      else if (strText.CompareNoCase(strFalse) == 0)
        m_varValue.boolVal = VARIANT_FALSE;
      else
        return FALSE;
      return TRUE;
    }
    default:
      return FALSE;
  }
}

BOOL CMFCPropertyGridProperty::HasButton() const {
  return !m_lstOptions.IsEmpty() || m_varValue.vt == VT_BOOL;
}

void CMFCPropertyGridProperty::AdjustButtonRect() {
  if (m_pWndList == NULL) return;
  int nRow = m_pWndList->m_nRowHeight;
  m_rectButton = m_Rect;
  m_rectButton.left = m_rectButton.right - nRow + 2;
  m_rectButton.top += 1;
}

void CMFCPropertyGridProperty::AdjustInPlaceEditRect(CRect& rectEdit, CRect& rectSpin) {
  rectSpin.SetRectEmpty();
  rectEdit = m_Rect;
  rectEdit.left += m_pWndList->m_nLeftColumnWidth + 4;
  rectEdit.top += 2;
  rectEdit.bottom -= 1;
  if (HasButton()) {
    AdjustButtonRect();
    rectEdit.right = m_rectButton.left;
  }
}

CWnd* CMFCPropertyGridProperty::CreateInPlaceEdit(CRect rectEdit, BOOL& bDefaultFormat) {
  CEdit* pEdit;
  if (!m_strEditMask.IsEmpty() || !m_strEditTempl.IsEmpty()) {
    CMFCMaskedEdit* pMasked = new CMFCMaskedEdit;
    pMasked->EnableSetMaskedCharsOnly(FALSE);
    pMasked->EnableGetMaskedCharsOnly(FALSE);
    pEdit = pMasked;
    bDefaultFormat = FALSE;
  } else {
    pEdit = new CEdit;
    bDefaultFormat = TRUE;
  }
  DWORD dwStyle = WS_VISIBLE | WS_CHILD | ES_AUTOHSCROLL;
  if (!m_bEnabled || !m_bAllowEdit) dwStyle |= ES_READONLY;
  pEdit->Create(dwStyle, rectEdit, m_pWndList, kInPlaceID);
  if (!bDefaultFormat) {
    CMFCMaskedEdit* pMasked = (CMFCMaskedEdit*)pEdit;
    pMasked->EnableMask(m_strEditMask, m_strEditTempl, _T('_'), m_strValidChars.IsEmpty() ? NULL : (LPCTSTR)m_strValidChars);
    pMasked->SetWindowText(FormatProperty());
  }
  return pEdit;
}

CComboBox* CMFCPropertyGridProperty::CreateCombo(CWnd* pWndParent, CRect rect) {
  rect.bottom = rect.top + 200;
  CComboBox* pCombo = new CComboBox;
  DWORD dwStyle = WS_CHILD | WS_VISIBLE | WS_VSCROLL | CBS_AUTOHSCROLL |
                  (m_bAllowEdit && m_varValue.vt != VT_BOOL ? CBS_DROPDOWN : CBS_DROPDOWNLIST);
  if (!pCombo->Create(dwStyle, rect, pWndParent, kComboID)) {
    delete pCombo;
    return NULL;
  }
  return pCombo;
}

BOOL CMFCPropertyGridProperty::OnEdit(LPPOINT) {
  if (m_pWndList == NULL || (m_bGroup && !m_bIsValueList)) return FALSE;
  m_pWndInPlace = NULL;
  m_pWndCombo = NULL;
  CRect rectEdit, rectSpin;
  AdjustInPlaceEditRect(rectEdit, rectSpin);
  HFONT hFont = m_pWndList->GetFont();
  if (!m_lstOptions.IsEmpty() || m_varValue.vt == VT_BOOL) {
    // A list of the options (or true and false).
    CRect rectCombo = rectEdit;
    rectCombo.right = m_Rect.right;
    rectCombo.top -= 2;
    m_pWndCombo = CreateCombo(m_pWndList, rectCombo);
    if (m_pWndCombo == NULL) return FALSE;
    m_pWndCombo->SendMessage(WM_SETFONT, (WPARAM)hFont, 0);
    if (m_varValue.vt == VT_BOOL) {
      m_pWndCombo->AddString(m_pWndList->m_strTrue);
      m_pWndCombo->AddString(m_pWndList->m_strFalse);
    } else {
      for (POSITION pos = m_lstOptions.GetHeadPosition(); pos != NULL;) m_pWndCombo->AddString(m_lstOptions.GetNext(pos));
    }
    CString str = FormatProperty();
    int nSel = m_pWndCombo->FindStringExact(-1, str);
    if (nSel >= 0)
      m_pWndCombo->SetCurSel(nSel);
    else
      m_pWndCombo->SetWindowText(str);
    m_pWndCombo->SetFocus();
    m_bInPlaceEdit = TRUE;
    return TRUE;
  }
  if (!m_bAllowEdit) return FALSE;
  BOOL bDefaultFormat = TRUE;
  m_pWndInPlace = CreateInPlaceEdit(rectEdit, bDefaultFormat);
  if (m_pWndInPlace == NULL) return FALSE;
  m_pWndInPlace->SendMessage(WM_SETFONT, (WPARAM)hFont, 0);
  if (bDefaultFormat) m_pWndInPlace->SetWindowText(FormatProperty());
  m_pWndInPlace->SetFocus();
  if (m_pWndInPlace->IsKindOf(RUNTIME_CLASS(CEdit))) ((CEdit*)m_pWndInPlace)->SetSel(0, -1);
  m_bInPlaceEdit = TRUE;
  return TRUE;
}

BOOL CMFCPropertyGridProperty::OnUpdateValue() {
  CString strText;
  if (m_pWndCombo != NULL && m_pWndCombo->GetSafeHwnd() != NULL) {
    int nSel = m_pWndCombo->GetCurSel();
    if ((m_pWndCombo->GetStyle() & 0x0003) == CBS_DROPDOWNLIST && nSel >= 0)
      m_pWndCombo->GetLBText(nSel, strText);
    else
      m_pWndCombo->GetWindowText(strText);
  } else if (m_pWndInPlace != NULL && m_pWndInPlace->GetSafeHwnd() != NULL) {
    m_pWndInPlace->GetWindowText(strText);
  } else {
    return TRUE;
  }
  if (strText == FormatProperty()) return TRUE;
  COleVariant varOld(m_varValue);
  if (!TextToVar(strText)) {
    m_varValue = varOld;
    return FALSE;
  }
  m_bIsModified = IsValueChanged();
  if (m_bIsValueList)
    for (POSITION pos = m_lstSubItems.GetHeadPosition(); pos != NULL;) {
      CMFCPropertyGridProperty* p = m_lstSubItems.GetNext(pos);
      p->m_bIsModified = p->IsValueChanged();
    }
  if (m_pWndList != NULL) m_pWndList->OnPropertyChanged(this);
  return TRUE;
}

BOOL CMFCPropertyGridProperty::OnEndEdit() {
  m_bInPlaceEdit = FALSE;
  if (m_pWndInPlace != NULL) {
    CWnd* p = m_pWndInPlace;
    m_pWndInPlace = NULL;
    if (p->GetSafeHwnd() != NULL) p->DestroyWindow();
    delete p;
  }
  if (m_pWndCombo != NULL) {
    CWnd* p = m_pWndCombo;
    m_pWndCombo = NULL;
    if (p->GetSafeHwnd() != NULL) p->DestroyWindow();
    delete p;
  }
  return TRUE;
}

void CMFCPropertyGridProperty::OnSelectCombo() {
  if (m_pWndCombo == NULL) return;
  OnUpdateValue();
  Redraw();
}

void CMFCPropertyGridProperty::OnClickButton(CPoint) {
  // Options: the list drops down.
  if (m_pWndCombo != NULL) m_pWndCombo->ShowDropDown(TRUE);
}

BOOL CMFCPropertyGridProperty::OnDblClk(CPoint) {
  // The next option (or the other of true and false).
  if (!m_bEnabled || m_pWndList == NULL) return FALSE;
  CString strNext;
  CString str = FormatProperty();
  if (m_varValue.vt == VT_BOOL) {
    strNext = m_varValue.boolVal != VARIANT_FALSE ? m_pWndList->m_strFalse : m_pWndList->m_strTrue;
  } else if (!m_lstOptions.IsEmpty()) {
    POSITION pos = m_lstOptions.Find(str);
    if (pos != NULL) m_lstOptions.GetNext(pos);
    strNext = pos != NULL ? m_lstOptions.GetAt(pos) : m_lstOptions.GetHead();
  } else {
    return FALSE;
  }
  m_pWndList->EndEditItem(FALSE);
  if (!TextToVar(strNext)) return FALSE;
  m_bIsModified = IsValueChanged();
  m_pWndList->OnPropertyChanged(this);
  Redraw();
  return TRUE;
}

void CMFCPropertyGridProperty::OnDrawExpandBox(CDC* pDC, CRect rect) {
  CRect r(0, 0, 9, 9);
  r.OffsetRect(rect.left + (rect.Width() - 9) / 2, rect.top + (rect.Height() - 9) / 2);
  pDC->FillSolidRect(r, afxGlobalData.clrWindow);
  CBrush br(afxGlobalData.clrBtnShadow);
  pDC->FrameRect(r, &br);
  int y = r.top + 4;
  pDC->FillSolidRect(r.left + 2, y, 5, 1, afxGlobalData.clrWindowText);
  if (!m_bExpanded) pDC->FillSolidRect(r.left + 4, r.top + 2, 1, 5, afxGlobalData.clrWindowText);
}

void CMFCPropertyGridProperty::OnDrawName(CDC* pDC, CRect rect) {
  COLORREF clrOld = (COLORREF)-1;
  if (IsSelected() && !(m_bGroup && !m_bIsValueList)) {
    BOOL bFocused = m_pWndList->m_bFocused || m_bInPlaceEdit;
    pDC->FillSolidRect(rect, bFocused ? afxGlobalData.clrHilite : afxGlobalData.clrBtnFace);
    clrOld = pDC->SetTextColor(bFocused ? afxGlobalData.clrTextHilite : afxGlobalData.clrBtnText);
  } else if (!m_bEnabled) {
    clrOld = pDC->SetTextColor(afxGlobalData.clrGrayedText);
  }
  CRect r = rect;
  r.DeflateRect(3, 0);
  pDC->DrawText(m_strName, r, DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_NOPREFIX | DT_END_ELLIPSIS);
  m_bNameIsTruncated = pDC->GetTextExtent(m_strName).cx > r.Width();
  if (clrOld != (COLORREF)-1) pDC->SetTextColor(clrOld);
}

void CMFCPropertyGridProperty::OnDrawValue(CDC* pDC, CRect rect) {
  CFont* pOld = NULL;
  if (m_pWndList->m_bMarkModifiedProperties && m_bIsModified) pOld = pDC->SelectObject(&m_pWndList->m_fontBold);
  COLORREF clrOld = (COLORREF)-1;
  if (!m_bEnabled) clrOld = pDC->SetTextColor(afxGlobalData.clrGrayedText);
  CString str = FormatProperty();
  CRect r = rect;
  r.DeflateRect(4, 0, 2, 0);
  pDC->DrawText(str, r, DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_NOPREFIX | DT_END_ELLIPSIS);
  m_bValueIsTruncated = pDC->GetTextExtent(str).cx > r.Width();
  if (clrOld != (COLORREF)-1) pDC->SetTextColor(clrOld);
  if (pOld != NULL) pDC->SelectObject(pOld);
}

void CMFCPropertyGridProperty::OnDrawButton(CDC* pDC, CRect rect) {
  pDC->DrawFrameControl(rect, DFC_BUTTON, DFCS_BUTTONPUSH | (m_bButtonIsDown ? DFCS_PUSHED : 0));
  if (!m_lstOptions.IsEmpty() || m_varValue.vt == VT_BOOL) {
    CMFCVisualManager::GetInstance()->OnDrawMenuArrow(pDC, rect, m_bEnabled);
  } else {
    CRect r = rect;
    if (m_bButtonIsDown) r.OffsetRect(1, 1);
    pDC->DrawText(_T("..."), r, DT_SINGLELINE | DT_CENTER | DT_VCENTER | DT_NOPREFIX);
  }
}

void CMFCPropertyGridProperty::OnDrawDescription(CDC* pDC, CRect rect) {
  CFont* pOld = pDC->SelectObject(&m_pWndList->m_fontBold);
  CRect r = rect;
  r.DeflateRect(4, 2);
  int cy = pDC->DrawText(m_strName, r, DT_SINGLELINE | DT_LEFT | DT_NOPREFIX | DT_END_ELLIPSIS);
  pDC->SelectObject(pOld);
  r.top += cy + 2;
  pDC->DrawText(m_strDescr, r, DT_WORDBREAK | DT_LEFT | DT_NOPREFIX | DT_END_ELLIPSIS);
}

// ---------------------------------------------------------------------
// CMFCPropertyGridColorProperty

IMPLEMENT_DYNAMIC(CMFCPropertyGridColorProperty, CMFCPropertyGridProperty)

CMFCPropertyGridColorProperty::CMFCPropertyGridColorProperty(const CString& strName, const COLORREF& color,
                                                             CPalette*, LPCTSTR lpszDescr, DWORD_PTR dwData)
    : CMFCPropertyGridProperty(strName, COleVariant((long)color), lpszDescr, dwData), m_Color(color),
      m_ColorOrig(color), m_ColorAutomatic(RGB(0, 0, 0)), m_bStdColorDlg(FALSE), m_nColumnsNumber(5) {}

CMFCPropertyGridColorProperty::~CMFCPropertyGridColorProperty() {}

void CMFCPropertyGridColorProperty::SetColor(COLORREF color) {
  m_Color = color;
  m_varValue = COleVariant((long)color);
  m_bIsModified = m_Color != m_ColorOrig;
  Redraw();
}

void CMFCPropertyGridColorProperty::EnableAutomaticButton(LPCTSTR lpszLabel, COLORREF colorAutomatic,
                                                          BOOL bEnable) {
  m_ColorAutomatic = colorAutomatic;
  m_strAutoColor = bEnable && lpszLabel != NULL ? lpszLabel : _T("");
}

void CMFCPropertyGridColorProperty::EnableOtherButton(LPCTSTR lpszLabel, BOOL bAltColorDlg, BOOL bEnable) {
  m_bStdColorDlg = !bAltColorDlg;
  m_strOtherColor = bEnable && lpszLabel != NULL ? lpszLabel : _T("");
}

CString CMFCPropertyGridColorProperty::FormatProperty() {
  if (m_Color == (COLORREF)-1) return m_strAutoColor;
  CString str;
  str.Format(_T("%02X%02X%02X"), GetRValue(m_Color), GetGValue(m_Color), GetBValue(m_Color));
  return str;
}

BOOL CMFCPropertyGridColorProperty::TextToVar(const CString& strText) {
  CString str = strText;
  str.TrimLeft();
  str.TrimRight();
  if (!m_strAutoColor.IsEmpty() && str.CompareNoCase(m_strAutoColor) == 0) {
    m_Color = (COLORREF)-1;
  } else {
    if (str.GetLength() != 6) return FALSE;
    LPTSTR pEnd = NULL;
    unsigned long n = _tcstoul(str, &pEnd, 16);
    if (pEnd == NULL || *pEnd != 0) return FALSE;
    m_Color = RGB((n >> 16) & 0xFF, (n >> 8) & 0xFF, n & 0xFF);
  }
  m_varValue = COleVariant((long)m_Color);
  return TRUE;
}

BOOL CMFCPropertyGridColorProperty::OnUpdateValue() { return CMFCPropertyGridProperty::OnUpdateValue(); }

void CMFCPropertyGridColorProperty::AdjustInPlaceEditRect(CRect& rectEdit, CRect& rectSpin) {
  CMFCPropertyGridProperty::AdjustInPlaceEditRect(rectEdit, rectSpin);
  rectEdit.left += m_Rect.Height() + 2;  // after the swatch
}

BOOL CMFCPropertyGridColorProperty::OnEdit(LPPOINT lptClick) { return CMFCPropertyGridProperty::OnEdit(lptClick); }

void CMFCPropertyGridColorProperty::OnDrawValue(CDC* pDC, CRect rect) {
  CRect rectColor = rect;
  rectColor.DeflateRect(3, 2);
  rectColor.right = rectColor.left + rectColor.Height() + 4;
  COLORREF clr = m_Color == (COLORREF)-1 ? m_ColorAutomatic : m_Color;
  pDC->FillSolidRect(rectColor, clr);
  CBrush br(afxGlobalData.clrBtnDkShadow);
  pDC->FrameRect(rectColor, &br);
  CRect rectText = rect;
  rectText.left = rectColor.right;
  CMFCPropertyGridProperty::OnDrawValue(pDC, rectText);
}

void CMFCPropertyGridColorProperty::OnClickButton(CPoint) {
  if (m_pWndList == NULL) return;
  m_pWndList->EndEditItem(FALSE);
  CColorDialog dlg(m_Color == (COLORREF)-1 ? m_ColorAutomatic : m_Color, CC_FULLOPEN, m_pWndList);
  if (dlg.DoModal() != IDOK) return;
  SetColor(dlg.GetColor());
  m_pWndList->OnPropertyChanged(this);
}

// ---------------------------------------------------------------------
// CMFCPropertyGridFontProperty

IMPLEMENT_DYNAMIC(CMFCPropertyGridFontProperty, CMFCPropertyGridProperty)

CMFCPropertyGridFontProperty::CMFCPropertyGridFontProperty(const CString& strName, LOGFONT& lf,
                                                           DWORD dwFontDialogFlags, LPCTSTR lpszDescr,
                                                           DWORD_PTR dwData, COLORREF color)
    : CMFCPropertyGridProperty(strName, COleVariant(_T("")), lpszDescr, dwData), m_lf(lf), m_lfOrig(lf),
      m_dwSelFontFlags(dwFontDialogFlags), m_Color(color) {
  m_bAllowEdit = FALSE;
  m_varValue = COleVariant(FormatProperty());
  m_varValueOrig = m_varValue;
}

CMFCPropertyGridFontProperty::~CMFCPropertyGridFontProperty() {}

CString CMFCPropertyGridFontProperty::FormatProperty() {
  // The face and the size in points.
  HDC hdc = ::GetDC(NULL);
  int nLogY = hdc != NULL ? ::GetDeviceCaps(hdc, LOGPIXELSY) : 96;
  if (hdc != NULL) ::ReleaseDC(NULL, hdc);
  int nPoints = m_lf.lfHeight != 0 ? ::MulDiv(m_lf.lfHeight < 0 ? -m_lf.lfHeight : m_lf.lfHeight, 72, nLogY) : 0;
  CString str;
  str.Format(_T("%s, %d"), m_lf.lfFaceName, nPoints);
  return str;
}

void CMFCPropertyGridFontProperty::OnClickButton(CPoint) {
  if (m_pWndList == NULL) return;
  m_pWndList->EndEditItem(FALSE);
  LOGFONT lf = m_lf;
  CFontDialog dlg(&lf, m_dwSelFontFlags, NULL, m_pWndList);
  if (m_Color != (COLORREF)-1) dlg.m_cf.rgbColors = m_Color;
  if (dlg.DoModal() != IDOK) return;
  dlg.GetCurrentFont(&m_lf);
  m_Color = dlg.GetColor();
  m_varValue = COleVariant(FormatProperty());
  m_bIsModified = memcmp(&m_lf, &m_lfOrig, sizeof(LOGFONT)) != 0;
  Redraw();
  m_pWndList->OnPropertyChanged(this);
}

// ---------------------------------------------------------------------
// CMFCPropertyGridFileProperty

IMPLEMENT_DYNAMIC(CMFCPropertyGridFileProperty, CMFCPropertyGridProperty)

CMFCPropertyGridFileProperty::CMFCPropertyGridFileProperty(const CString& strName, const CString& strFolderName,
                                                           DWORD_PTR dwData, LPCTSTR lpszDescr)
    : CMFCPropertyGridProperty(strName, COleVariant((LPCTSTR)strFolderName), lpszDescr, dwData),
      m_bIsFolder(TRUE), m_bOpenFileDialog(FALSE), m_dwFileOpenFlags(0) {}

CMFCPropertyGridFileProperty::CMFCPropertyGridFileProperty(const CString& strName, BOOL bOpenFileDialog,
                                                           const CString& strFileName, LPCTSTR lpszDefExt,
                                                           DWORD dwFlags, LPCTSTR lpszFilter, LPCTSTR lpszDescr,
                                                           DWORD_PTR dwData)
    : CMFCPropertyGridProperty(strName, COleVariant((LPCTSTR)strFileName), lpszDescr, dwData),
      m_bIsFolder(FALSE), m_bOpenFileDialog(bOpenFileDialog), m_dwFileOpenFlags(dwFlags),
      m_strDefExt(lpszDefExt != NULL ? lpszDefExt : _T("")), m_strFilter(lpszFilter != NULL ? lpszFilter : _T("")) {}

CMFCPropertyGridFileProperty::~CMFCPropertyGridFileProperty() {}

void CMFCPropertyGridFileProperty::OnClickButton(CPoint) {
  if (m_pWndList == NULL) return;
  m_pWndList->EndEditItem(FALSE);
  CString strPath = FormatProperty();
  CString strNew;
  BOOL bOK = FALSE;
  if (m_bIsFolder) {
    CShellManager shellTemp;
    CShellManager* pShell = afxShellManager != NULL ? afxShellManager : &shellTemp;
    bOK = pShell->BrowseForFolder(strNew, m_pWndList, strPath);
  } else {
    CFileDialog dlg(m_bOpenFileDialog, m_strDefExt.IsEmpty() ? NULL : (LPCTSTR)m_strDefExt,
                    strPath.IsEmpty() ? NULL : (LPCTSTR)strPath, m_dwFileOpenFlags,
                    m_strFilter.IsEmpty() ? NULL : (LPCTSTR)m_strFilter, m_pWndList);
    bOK = dlg.DoModal() == IDOK;
    if (bOK) strNew = dlg.GetPathName();
  }
  if (!bOK) return;
  m_varValue = COleVariant((LPCTSTR)strNew);
  m_bIsModified = IsValueChanged();
  Redraw();
  m_pWndList->OnPropertyChanged(this);
}

// ---------------------------------------------------------------------
// CMFCPropertyGridCtrl

IMPLEMENT_DYNAMIC(CMFCPropertyGridCtrl, CWnd)

BEGIN_MESSAGE_MAP(CMFCPropertyGridCtrl, CWnd)
  ON_WM_CREATE()
  ON_WM_DESTROY()
  ON_WM_SIZE()
  ON_WM_PAINT()
  ON_WM_ERASEBKGND()
  ON_WM_LBUTTONDOWN()
  ON_WM_LBUTTONUP()
  ON_WM_LBUTTONDBLCLK()
  ON_WM_MOUSEMOVE()
  ON_WM_VSCROLL()
  ON_WM_MOUSEWHEEL()
  ON_WM_KEYDOWN()
  ON_WM_GETDLGCODE()
  ON_WM_SETFOCUS()
  ON_WM_KILLFOCUS()
  ON_WM_SETCURSOR()
  ON_WM_CANCELMODE()
  ON_MESSAGE(WM_SETFONT, OnSetFont)
  ON_MESSAGE(WM_GETFONT, OnGetFont)
END_MESSAGE_MAP()

CMFCPropertyGridCtrl::CMFCPropertyGridCtrl()
    : m_pSel(NULL), m_hFont(NULL), m_strTrue(_T("True")), m_strFalse(_T("False")),
      m_strLeftColumn(_T("Property")), m_strRightColumn(_T("Value")), m_cListDelimeter(_T(',')),
      m_bHeaderCtrl(TRUE), m_bDescriptionArea(TRUE), m_bAlphabeticMode(FALSE), m_bVSDotNetLook(FALSE),
      m_bMarkModifiedProperties(FALSE), m_bTracking(FALSE), m_bFocused(FALSE), m_bInitialized(FALSE),
      m_nLeftColumnWidth(0), m_nRowHeight(0), m_nHeaderHeight(0), m_nDescrHeight(0), m_nDescrRows(3),
      m_nVertScrollOffset(0), m_nTrackOffset(0), m_clrBackground((COLORREF)-1), m_clrText((COLORREF)-1),
      m_clrGroupBackground((COLORREF)-1), m_clrGroupText((COLORREF)-1), m_clrDescriptionBackground((COLORREF)-1),
      m_clrDescriptionText((COLORREF)-1), m_clrLine((COLORREF)-1) {}

CMFCPropertyGridCtrl::~CMFCPropertyGridCtrl() {
  while (!m_lstProps.IsEmpty()) delete m_lstProps.RemoveHead();
}

BOOL CMFCPropertyGridCtrl::Create(DWORD dwStyle, const RECT& rect, CWnd* pParentWnd, UINT nID) {
  LPCTSTR lpszClass = AfxRegisterWndClass(CS_DBLCLKS, ::LoadCursor(NULL, IDC_ARROW), NULL, NULL);
  return CWnd::CreateEx(0, lpszClass, _T(""), dwStyle | WS_CLIPCHILDREN, rect, pParentWnd, nID);
}

int CMFCPropertyGridCtrl::OnCreate(LPCREATESTRUCT lpCreateStruct) {
  if (CWnd::OnCreate(lpCreateStruct) == -1) return -1;
  Init();
  return 0;
}

void CMFCPropertyGridCtrl::PreSubclassWindow() {
  CWnd::PreSubclassWindow();
  // A custom control in a dialog: set up now (created, OnCreate does it).
  if (m_hWnd != NULL && !m_bInitialized && AfxGetThreadState()->m_pWndInit == NULL) Init();
}

void CMFCPropertyGridCtrl::Init() {
  if (m_bInitialized) return;
  m_bInitialized = TRUE;
  _CreateFonts();
  if (m_bHeaderCtrl && m_wndHeader.m_hWnd == NULL) {
    m_wndHeader.Create(WS_CHILD | WS_VISIBLE | HDS_HORZ, CRect(0, 0, 0, 0), this, kHeaderID);
    m_wndHeader.SendMessage(WM_SETFONT, (WPARAM)GetFont(), 0);
    HDITEM hdi;
    memset(&hdi, 0, sizeof(hdi));
    hdi.mask = HDI_TEXT | HDI_WIDTH | HDI_FORMAT;
    hdi.fmt = HDF_LEFT | HDF_STRING;
    hdi.cxy = 100;
    hdi.pszText = (LPTSTR)(LPCTSTR)m_strLeftColumn;
    m_wndHeader.InsertItem(0, &hdi);
    hdi.pszText = (LPTSTR)(LPCTSTR)m_strRightColumn;
    hdi.cxy = 100;
    m_wndHeader.InsertItem(1, &hdi);
  }
  AdjustLayout();
}

void CMFCPropertyGridCtrl::OnDestroy() {
  EndEditItem(FALSE);
  CWnd::OnDestroy();
}

void CMFCPropertyGridCtrl::_CreateFonts() {
  if (m_fontBold.GetSafeHandle() != NULL) m_fontBold.DeleteObject();
  LOGFONT lf;
  memset(&lf, 0, sizeof(lf));
  ::GetObject(GetFont(), sizeof(lf), &lf);
  lf.lfWeight = FW_BOLD;
  m_fontBold.CreateFontIndirect(&lf);
  CWindowDC dc(this);
  HGDIOBJ hOld = ::SelectObject(dc.GetSafeHdc(), GetFont());
  TEXTMETRIC tm;
  dc.GetTextMetrics(&tm);
  ::SelectObject(dc.GetSafeHdc(), hOld);
  m_nRowHeight = tm.tmHeight + 4;
}

LRESULT CMFCPropertyGridCtrl::OnSetFont(WPARAM wParam, LPARAM lParam) {
  m_hFont = (HFONT)wParam;
  _CreateFonts();
  if (m_wndHeader.m_hWnd != NULL) m_wndHeader.SendMessage(WM_SETFONT, wParam, 0);
  AdjustLayout();
  if (LOWORD(lParam)) Invalidate();
  return 0;
}

LRESULT CMFCPropertyGridCtrl::OnGetFont(WPARAM, LPARAM) { return (LRESULT)GetFont(); }

int CMFCPropertyGridCtrl::AddProperty(CMFCPropertyGridProperty* pProp, BOOL bRedraw, BOOL bAdjustLayout) {
  if (pProp == NULL) return -1;
  for (POSITION pos = m_lstProps.GetHeadPosition(); pos != NULL;)
    if (m_lstProps.GetNext(pos) == pProp) return -1;
  pProp->_SetOwnerList(this);
  m_lstProps.AddTail(pProp);
  if (bAdjustLayout) AdjustLayout();
  if (bRedraw && m_hWnd != NULL) Invalidate();
  return (int)m_lstProps.GetCount() - 1;
}

BOOL CMFCPropertyGridCtrl::DeleteProperty(CMFCPropertyGridProperty*& pProp, BOOL bRedraw, BOOL bAdjustLayout) {
  if (pProp == NULL) return FALSE;
  if (m_pSel == pProp || pProp->_IsSubItem(m_pSel)) SetCurSel(NULL, FALSE);
  POSITION pos = m_lstProps.Find(pProp);
  if (pos != NULL) {
    m_lstProps.RemoveAt(pos);
    delete pProp;
    pProp = NULL;
  } else {
    // A sub-item: its parent removes it.
    CMFCPropertyGridProperty* pParent = pProp->m_pParent;
    if (pParent == NULL || !pParent->RemoveSubItem(pProp, TRUE)) return FALSE;
  }
  if (bAdjustLayout) AdjustLayout();
  if (bRedraw && m_hWnd != NULL) Invalidate();
  return TRUE;
}

void CMFCPropertyGridCtrl::RemoveAll() {
  SetCurSel(NULL, FALSE);
  while (!m_lstProps.IsEmpty()) delete m_lstProps.RemoveHead();
  m_arRows.RemoveAll();
  m_nVertScrollOffset = 0;
  AdjustLayout();
  if (m_hWnd != NULL) Invalidate();
}

CMFCPropertyGridProperty* CMFCPropertyGridCtrl::GetProperty(int nIndex) const {
  POSITION pos = m_lstProps.FindIndex(nIndex);
  return pos != NULL ? m_lstProps.GetAt(pos) : NULL;
}

CMFCPropertyGridProperty* CMFCPropertyGridCtrl::FindItemByData(DWORD_PTR dwData, BOOL bSearchSubItems) const {
  for (POSITION pos = m_lstProps.GetHeadPosition(); pos != NULL;) {
    CMFCPropertyGridProperty* p = m_lstProps.GetNext(pos);
    if (p->m_dwData == dwData) return p;
    if (bSearchSubItems) {
      CMFCPropertyGridProperty* pFound = p->FindSubItemByData(dwData);
      if (pFound != NULL) return pFound;
    }
  }
  return NULL;
}

CMFCPropertyGridProperty* CMFCPropertyGridCtrl::HitTest(CPoint pt, CMFCPropertyGridProperty::ClickArea* pnArea,
                                                        BOOL bPropsOnly) const {
  if (!bPropsOnly && m_bDescriptionArea && m_rectDescr.PtInRect(pt)) {
    if (pnArea != NULL) *pnArea = CMFCPropertyGridProperty::ClickDescription;
    return m_pSel;
  }
  for (int i = 0; i < m_arRows.GetSize(); i++) {
    CMFCPropertyGridProperty* p = m_arRows[i]->HitTest(pt, pnArea);
    if (p != NULL) return p;
  }
  return NULL;
}

int CMFCPropertyGridCtrl::CompareProps(const CMFCPropertyGridProperty* pProp1,
                                       const CMFCPropertyGridProperty* pProp2) const {
  return pProp1->m_strName.CompareNoCase(pProp2->m_strName);
}

COLORREF CMFCPropertyGridCtrl::GetGroupBkColor() const {
  if (m_clrGroupBackground != (COLORREF)-1) return m_clrGroupBackground;
  return CMFCVisualManager::GetInstance()->GetPropertyGridGroupColor((CMFCPropertyGridCtrl*)this);
}

COLORREF CMFCPropertyGridCtrl::GetGroupTextColor() const {
  if (m_clrGroupText != (COLORREF)-1) return m_clrGroupText;
  return CMFCVisualManager::GetInstance()->GetPropertyGridGroupTextColor((CMFCPropertyGridCtrl*)this);
}

COLORREF CMFCPropertyGridCtrl::GetLineColor() const {
  if (m_clrLine != (COLORREF)-1) return m_clrLine;
  return m_bVSDotNetLook ? afxGlobalData.clrBtnFace : afxGlobalData.clrBtnShadow;
}

void CMFCPropertyGridCtrl::_BuildRows() {
  m_arRows.RemoveAll();
  if (m_bAlphabeticMode) {
    // Every property that is not a group band, sorted by name.
    CArray<CMFCPropertyGridProperty*, CMFCPropertyGridProperty*> arTop;
    CList<CMFCPropertyGridProperty*, CMFCPropertyGridProperty*> lst;
    for (POSITION pos = m_lstProps.GetHeadPosition(); pos != NULL;) lst.AddTail(m_lstProps.GetNext(pos));
    while (!lst.IsEmpty()) {
      CMFCPropertyGridProperty* p = lst.RemoveHead();
      if (!p->m_bVisible) continue;
      if (p->m_bGroup && !p->m_bIsValueList) {
        for (POSITION pos = p->m_lstSubItems.GetHeadPosition(); pos != NULL;) lst.AddTail(p->m_lstSubItems.GetNext(pos));
        continue;
      }
      arTop.Add(p);
    }
    for (int i = 1; i < arTop.GetSize(); i++)
      for (int j = i; j > 0 && CompareProps(arTop[j - 1], arTop[j]) > 0; j--) {
        CMFCPropertyGridProperty* t = arTop[j];
        arTop[j] = arTop[j - 1];
        arTop[j - 1] = t;
      }
    for (int i = 0; i < arTop.GetSize(); i++) arTop[i]->_CollectVisible(m_arRows);
  } else {
    for (POSITION pos = m_lstProps.GetHeadPosition(); pos != NULL;) m_lstProps.GetNext(pos)->_CollectVisible(m_arRows);
  }
}

int CMFCPropertyGridCtrl::GetListRect(CRect& rect) const {
  rect = m_rectList;
  return m_rectList.Height();
}

int CMFCPropertyGridCtrl::_VisibleRows() const {
  return m_nRowHeight > 0 ? m_rectList.Height() / m_nRowHeight : 0;
}

void CMFCPropertyGridCtrl::_UpdateScroll() {
  int nTotal = (int)m_arRows.GetSize();
  int nPage = _VisibleRows();
  if (m_nVertScrollOffset > nTotal - nPage) m_nVertScrollOffset = nTotal - nPage;
  if (m_nVertScrollOffset < 0) m_nVertScrollOffset = 0;
  SCROLLINFO si;
  memset(&si, 0, sizeof(si));
  si.cbSize = sizeof(si);
  si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
  si.nMin = 0;
  si.nMax = nTotal > nPage ? nTotal - 1 : 0;
  si.nPage = nTotal > nPage ? (UINT)nPage : 0;
  si.nPos = m_nVertScrollOffset;
  ::SetScrollInfo(m_hWnd, SB_VERT, &si, TRUE);
}

void CMFCPropertyGridCtrl::AdjustLayout() {
  if (m_hWnd == NULL) return;
  if (m_nRowHeight == 0) _CreateFonts();
  _BuildRows();
  // The scroll bar first: it changes the client area.
  CRect rectClient;
  GetClientRect(rectClient);
  int nHeader = m_bHeaderCtrl ? m_nRowHeight + 4 : 0;
  m_nDescrHeight = m_bDescriptionArea ? m_nDescrRows * m_nRowHeight + 6 : 0;
  int nListHeight = rectClient.Height() - nHeader - m_nDescrHeight;
  BOOL bScroll = m_nRowHeight > 0 && m_arRows.GetSize() * m_nRowHeight > nListHeight;
  ShowScrollBar(SB_VERT, bScroll);
  GetClientRect(rectClient);
  if (m_nLeftColumnWidth == 0) m_nLeftColumnWidth = rectClient.Width() / 2;
  if (m_nLeftColumnWidth > rectClient.Width() - 20) m_nLeftColumnWidth = rectClient.Width() > 40 ? rectClient.Width() - 20 : rectClient.Width() / 2;
  m_nHeaderHeight = nHeader;
  if (m_wndHeader.m_hWnd != NULL) {
    m_wndHeader.ShowWindow(m_bHeaderCtrl ? SW_SHOWNOACTIVATE : SW_HIDE);
    if (m_bHeaderCtrl) {
      m_wndHeader.SetWindowPos(NULL, rectClient.left, rectClient.top, rectClient.Width(), nHeader,
                               SWP_NOZORDER | SWP_NOACTIVATE);
      HDITEM hdi;
      memset(&hdi, 0, sizeof(hdi));
      hdi.mask = HDI_WIDTH;
      hdi.cxy = m_nLeftColumnWidth + 1;
      m_wndHeader.SetItem(0, &hdi);
      hdi.cxy = rectClient.Width() - m_nLeftColumnWidth;
      m_wndHeader.SetItem(1, &hdi);
    }
  }
  m_rectList = rectClient;
  m_rectList.top += nHeader;
  m_rectList.bottom -= m_nDescrHeight;
  if (m_rectList.bottom < m_rectList.top) m_rectList.bottom = m_rectList.top;
  m_rectDescr = rectClient;
  m_rectDescr.top = m_rectList.bottom;
  _UpdateScroll();
  // Each property's place.
  for (POSITION pos = m_lstProps.GetHeadPosition(); pos != NULL;) {
    CArray<CMFCPropertyGridProperty*, CMFCPropertyGridProperty*> ar;
    CMFCPropertyGridProperty* p = m_lstProps.GetNext(pos);
    // Hidden ones: no rectangle.
    CList<CMFCPropertyGridProperty*, CMFCPropertyGridProperty*> lst;
    lst.AddTail(p);
    while (!lst.IsEmpty()) {
      CMFCPropertyGridProperty* q = lst.RemoveHead();
      q->m_Rect.SetRectEmpty();
      for (POSITION p2 = q->m_lstSubItems.GetHeadPosition(); p2 != NULL;) lst.AddTail(q->m_lstSubItems.GetNext(p2));
    }
  }
  for (int i = m_nVertScrollOffset; i < m_arRows.GetSize(); i++) {
    int y = m_rectList.top + (i - m_nVertScrollOffset) * m_nRowHeight;
    if (y >= m_rectList.bottom) break;
    m_arRows[i]->m_Rect = CRect(m_rectList.left, y, m_rectList.right, y + m_nRowHeight);
  }
  // The editor follows its property.
  if (m_pSel != NULL && m_pSel->m_bInPlaceEdit) {
    if (m_pSel->m_Rect.IsRectEmpty()) {
      EndEditItem(TRUE);
    } else {
      CRect rectEdit, rectSpin;
      m_pSel->AdjustInPlaceEditRect(rectEdit, rectSpin);
      if (m_pSel->m_pWndInPlace != NULL) m_pSel->m_pWndInPlace->MoveWindow(rectEdit);
      if (m_pSel->m_pWndCombo != NULL) {
        rectEdit.right = m_pSel->m_Rect.right;
        rectEdit.top -= 2;
        rectEdit.bottom = rectEdit.top + 200;
        m_pSel->m_pWndCombo->MoveWindow(rectEdit);
      }
    }
  }
  if (m_pSel != NULL && m_pSel->HasButton()) m_pSel->AdjustButtonRect();
  Invalidate();
}

void CMFCPropertyGridCtrl::OnSize(UINT nType, int cx, int cy) {
  CWnd::OnSize(nType, cx, cy);
  AdjustLayout();
}

void CMFCPropertyGridCtrl::SetLeftColumnWidth(int cx) {
  m_nLeftColumnWidth = cx;
  AdjustLayout();
}

BOOL CMFCPropertyGridCtrl::OnEraseBkgnd(CDC*) { return TRUE; }

void CMFCPropertyGridCtrl::OnFillBackground(CDC* pDC, CRect rectClient) { pDC->FillSolidRect(rectClient, GetBkColor()); }

void CMFCPropertyGridCtrl::OnPaint() {
  CPaintDC dcPaint(this);
  CRect rectClient;
  GetClientRect(rectClient);
  CDC dc;
  CBitmap bmp;
  CDC* pDC = &dcPaint;
  if (dc.CreateCompatibleDC(&dcPaint) && bmp.CreateCompatibleBitmap(&dcPaint, rectClient.Width(), rectClient.Height()))
    pDC = &dc;
  CBitmap* pOldBmp = pDC == &dc ? dc.SelectObject(&bmp) : NULL;
  OnFillBackground(pDC, rectClient);
  HGDIOBJ hOldFont = ::SelectObject(pDC->GetSafeHdc(), GetFont());
  pDC->SetBkMode(TRANSPARENT);
  pDC->SetTextColor(GetTextColor());
  OnDrawList(pDC);
  if (m_bDescriptionArea) OnDrawDescription(pDC, m_rectDescr);
  ::SelectObject(pDC->GetSafeHdc(), hOldFont);
  if (pDC == &dc) {
    if (m_bHeaderCtrl) dcPaint.ExcludeClipRect(CRect(rectClient.left, rectClient.top, rectClient.right, m_nHeaderHeight));
    dcPaint.BitBlt(0, 0, rectClient.Width(), rectClient.Height(), &dc, 0, 0, SRCCOPY);
    dc.SelectObject(pOldBmp);
  }
}

void CMFCPropertyGridCtrl::OnDrawList(CDC* pDC) {
  CRgn rgn;
  rgn.CreateRectRgnIndirect(m_rectList);
  pDC->SelectClipRgn(&rgn);
  // The margin column where the expand boxes are.
  CRect rectMargin = m_rectList;
  rectMargin.right = rectMargin.left + m_nRowHeight;
  pDC->FillSolidRect(rectMargin, GetGroupBkColor());
  for (int i = m_nVertScrollOffset; i < m_arRows.GetSize(); i++) {
    CMFCPropertyGridProperty* p = m_arRows[i];
    if (p->m_Rect.IsRectEmpty()) break;
    OnDrawProperty(pDC, p);
  }
  // The divider, down from the last row too.
  int nLastBottom = m_rectList.top + ((int)m_arRows.GetSize() - m_nVertScrollOffset) * m_nRowHeight;
  if (nLastBottom < m_rectList.bottom) {
    CRect r(m_rectList.left + m_nLeftColumnWidth, nLastBottom, m_rectList.left + m_nLeftColumnWidth + 1,
            m_rectList.bottom);
    (void)r;
  }
  pDC->SelectClipRgn(NULL);
}

int CMFCPropertyGridCtrl::OnDrawProperty(CDC* pDC, CMFCPropertyGridProperty* pProp) const {
  CRect rect = pProp->m_Rect;
  int nLevel = m_bAlphabeticMode ? 0 : pProp->GetHierarchyLevel();
  // In alphabetic mode, sub-items of a value still indent under it.
  if (m_bAlphabeticMode)
    for (CMFCPropertyGridProperty* q = pProp->m_pParent; q != NULL && !(q->m_bGroup && !q->m_bIsValueList); q = q->m_pParent)
      nLevel++;
  CRect rectExpand(rect.left + nLevel * m_nRowHeight, rect.top, rect.left + (nLevel + 1) * m_nRowHeight, rect.bottom);
  COLORREF clrLine = GetLineColor();
  if (pProp->m_bGroup && !pProp->m_bIsValueList) {
    // A band across the row.
    COLORREF clrText = CMFCVisualManager::GetInstance()->OnFillPropertyGridGroup(pDC, (CMFCPropertyGridCtrl*)this, rect);
    if (pProp->IsSelected()) {
      CRect r = rect;
      r.left = rectExpand.right;
      pDC->FillSolidRect(r, m_bFocused ? afxGlobalData.clrHilite : afxGlobalData.clrBtnShadow);
      clrText = m_bFocused ? afxGlobalData.clrTextHilite : afxGlobalData.clrBtnHilite;
    }
    if (!pProp->m_lstSubItems.IsEmpty()) pProp->OnDrawExpandBox(pDC, rectExpand);
    CFont* pOld = pDC->SelectObject((CFont*)&m_fontBold);
    COLORREF clrOld = pDC->SetTextColor(clrText);
    CRect r = rect;
    r.left = rectExpand.right + 2;
    pDC->DrawText(pProp->m_strName, r, DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_NOPREFIX | DT_END_ELLIPSIS);
    pDC->SetTextColor(clrOld);
    pDC->SelectObject(pOld);
    pDC->FillSolidRect(rect.left, rect.bottom - 1, rect.Width(), 1, clrLine);
    return 1;
  }
  CRect rectName(rectExpand.right, rect.top, rect.left + m_nLeftColumnWidth, rect.bottom - 1);
  CRect rectValue(rect.left + m_nLeftColumnWidth + 1, rect.top, rect.right, rect.bottom - 1);
  if (!pProp->m_lstSubItems.IsEmpty()) pProp->OnDrawExpandBox(pDC, rectExpand);
  pProp->OnDrawName(pDC, rectName);
  if (!pProp->m_bInPlaceEdit || pProp->m_pWndCombo == NULL) {
    CRect rv = rectValue;
    if (pProp->IsSelected() && pProp->HasButton()) rv.right -= m_nRowHeight;
    if (!pProp->m_bInPlaceEdit) pProp->OnDrawValue(pDC, rv);
  }
  if (pProp->IsSelected() && pProp->HasButton() && pProp->m_pWndCombo == NULL) {
    pProp->AdjustButtonRect();
    pProp->OnDrawButton(pDC, pProp->m_rectButton);
  }
  // The grid lines.
  pDC->FillSolidRect(rectExpand.right, rect.bottom - 1, rect.right - rectExpand.right, 1, clrLine);
  pDC->FillSolidRect(rect.left + m_nLeftColumnWidth, rect.top, 1, rect.Height(), clrLine);
  return 1;
}

void CMFCPropertyGridCtrl::OnDrawDescription(CDC* pDC, CRect rect) {
  COLORREF clrBk = m_clrDescriptionBackground != (COLORREF)-1 ? m_clrDescriptionBackground : afxGlobalData.clrBtnFace;
  pDC->FillSolidRect(rect, clrBk);
  pDC->FillSolidRect(rect.left, rect.top, rect.Width(), 1, afxGlobalData.clrBtnShadow);
  if (m_pSel == NULL) return;
  COLORREF clrOld = pDC->SetTextColor(m_clrDescriptionText != (COLORREF)-1 ? m_clrDescriptionText : afxGlobalData.clrBtnText);
  CRect r = rect;
  r.top += 2;
  m_pSel->OnDrawDescription(pDC, r);
  pDC->SetTextColor(clrOld);
}

void CMFCPropertyGridCtrl::SetCurSel(CMFCPropertyGridProperty* pProp, BOOL bRedraw) {
  if (m_pSel == pProp) return;
  if (m_pSel != NULL && m_pSel->m_bInPlaceEdit) EndEditItem(TRUE);
  CMFCPropertyGridProperty* pOld = m_pSel;
  m_pSel = pProp;
  if (pOld != NULL) pOld->m_bButtonIsDown = FALSE;
  if (bRedraw && m_hWnd != NULL) {
    if (pOld != NULL) pOld->Redraw();
    if (pProp != NULL) pProp->Redraw();
    if (m_bDescriptionArea) InvalidateRect(m_rectDescr);
  }
  if (pProp != NULL) EnsureVisible(pProp);
}

void CMFCPropertyGridCtrl::EnsureVisible(CMFCPropertyGridProperty* pProp, BOOL bExpandParents) {
  if (pProp == NULL || m_hWnd == NULL) return;
  if (bExpandParents) {
    BOOL bChanged = FALSE;
    for (CMFCPropertyGridProperty* p = pProp->m_pParent; p != NULL; p = p->m_pParent)
      if (!p->m_bExpanded) {
        p->m_bExpanded = TRUE;
        bChanged = TRUE;
      }
    if (bChanged) AdjustLayout();
  }
  int nIndex = -1;
  for (int i = 0; i < m_arRows.GetSize(); i++)
    if (m_arRows[i] == pProp) nIndex = i;
  if (nIndex < 0) return;
  int nPage = _VisibleRows();
  int nOffset = m_nVertScrollOffset;
  if (nIndex < nOffset)
    nOffset = nIndex;
  else if (nPage > 0 && nIndex >= nOffset + nPage)
    nOffset = nIndex - nPage + 1;
  if (nOffset != m_nVertScrollOffset) {
    m_nVertScrollOffset = nOffset;
    AdjustLayout();
  }
}

void CMFCPropertyGridCtrl::ExpandAll(BOOL bExpand) {
  CList<CMFCPropertyGridProperty*, CMFCPropertyGridProperty*> lst;
  for (POSITION pos = m_lstProps.GetHeadPosition(); pos != NULL;) lst.AddTail(m_lstProps.GetNext(pos));
  while (!lst.IsEmpty()) {
    CMFCPropertyGridProperty* p = lst.RemoveHead();
    if (!p->m_lstSubItems.IsEmpty()) p->m_bExpanded = bExpand;
    for (POSITION pos = p->m_lstSubItems.GetHeadPosition(); pos != NULL;) lst.AddTail(p->m_lstSubItems.GetNext(pos));
  }
  if (!bExpand && m_pSel != NULL && !m_pSel->IsParentExpanded()) SetCurSel(NULL, FALSE);
  AdjustLayout();
}

BOOL CMFCPropertyGridCtrl::EditItem(CMFCPropertyGridProperty* pProp, LPPOINT lptClick) {
  if (pProp == NULL || !pProp->m_bEnabled) return FALSE;
  if (m_pSel != pProp) SetCurSel(pProp);
  EndEditItem(TRUE);
  EnsureVisible(pProp);
  if (pProp->m_Rect.IsRectEmpty()) return FALSE;
  BOOL bOK = pProp->OnEdit(lptClick);
  pProp->Redraw();
  return bOK;
}

BOOL CMFCPropertyGridCtrl::EndEditItem(BOOL bUpdateData) {
  if (m_pSel == NULL || !m_pSel->m_bInPlaceEdit) return TRUE;
  CMFCPropertyGridProperty* pProp = m_pSel;
  if (bUpdateData && !pProp->OnUpdateValue()) {
    ::MessageBeep((UINT)-1);
    if (pProp->m_pWndInPlace != NULL) pProp->m_pWndInPlace->SetFocus();
    return FALSE;
  }
  BOOL bHadFocus = FALSE;
  HWND hFocus = ::GetFocus();
  if (hFocus != NULL && ((pProp->m_pWndInPlace != NULL && (hFocus == pProp->m_pWndInPlace->m_hWnd ||
                                                           ::IsChild(pProp->m_pWndInPlace->m_hWnd, hFocus))) ||
                         (pProp->m_pWndCombo != NULL && (hFocus == pProp->m_pWndCombo->m_hWnd ||
                                                         ::IsChild(pProp->m_pWndCombo->m_hWnd, hFocus)))))
    bHadFocus = TRUE;
  pProp->OnEndEdit();
  if (bHadFocus) SetFocus();
  pProp->Redraw();
  return TRUE;
}

void CMFCPropertyGridCtrl::OnPropertyChanged(CMFCPropertyGridProperty* pProp) const {
  pProp->Redraw();
  // A value list's parent shows its sub-items: redraw it too.
  if (pProp->m_pParent != NULL && pProp->m_pParent->m_bIsValueList) {
    pProp->m_pParent->m_bIsModified = TRUE;
    pProp->m_pParent->Redraw();
  }
  CWnd* pOwner = GetOwner();
  if (pOwner != NULL) pOwner->SendMessage(AFX_WM_PROPERTY_CHANGED, (WPARAM)GetDlgCtrlID(), (LPARAM)pProp);
}

void CMFCPropertyGridCtrl::EnableHeaderCtrl(BOOL bEnable, LPCTSTR lpszLeftColumn, LPCTSTR lpszRightColumn) {
  m_bHeaderCtrl = bEnable;
  m_strLeftColumn = lpszLeftColumn != NULL ? lpszLeftColumn : _T("");
  m_strRightColumn = lpszRightColumn != NULL ? lpszRightColumn : _T("");
  if (m_hWnd == NULL) return;
  if (bEnable && m_wndHeader.m_hWnd == NULL) {
    m_bInitialized = FALSE;
    Init();
  } else if (m_wndHeader.m_hWnd != NULL) {
    HDITEM hdi;
    memset(&hdi, 0, sizeof(hdi));
    hdi.mask = HDI_TEXT;
    hdi.pszText = (LPTSTR)(LPCTSTR)m_strLeftColumn;
    m_wndHeader.SetItem(0, &hdi);
    hdi.pszText = (LPTSTR)(LPCTSTR)m_strRightColumn;
    m_wndHeader.SetItem(1, &hdi);
  }
  AdjustLayout();
}

void CMFCPropertyGridCtrl::EnableDescriptionArea(BOOL bEnable) {
  m_bDescriptionArea = bEnable;
  AdjustLayout();
}

void CMFCPropertyGridCtrl::SetDescriptionRows(int nDescRows) {
  m_nDescrRows = nDescRows > 0 ? nDescRows : 1;
  AdjustLayout();
}

void CMFCPropertyGridCtrl::SetAlphabeticMode(BOOL bSet) {
  if (m_bAlphabeticMode == bSet) return;
  EndEditItem(TRUE);
  m_bAlphabeticMode = bSet;
  if (m_pSel != NULL && bSet && m_pSel->m_bGroup && !m_pSel->m_bIsValueList) m_pSel = NULL;
  m_nVertScrollOffset = 0;
  AdjustLayout();
}

void CMFCPropertyGridCtrl::SetVSDotNetLook(BOOL bSet) {
  m_bVSDotNetLook = bSet;
  if (m_hWnd != NULL) Invalidate();
}

void CMFCPropertyGridCtrl::MarkModifiedProperties(BOOL bMark, BOOL bRedraw) {
  m_bMarkModifiedProperties = bMark;
  if (bRedraw && m_hWnd != NULL) Invalidate();
}

void CMFCPropertyGridCtrl::ResetOriginalValues(BOOL bRedraw) {
  for (POSITION pos = m_lstProps.GetHeadPosition(); pos != NULL;) m_lstProps.GetNext(pos)->ResetOriginalValue();
  if (bRedraw && m_hWnd != NULL) Invalidate();
}

void CMFCPropertyGridCtrl::SetBoolLabels(LPCTSTR lpszTrue, LPCTSTR lpszFalse) {
  m_strTrue = lpszTrue;
  m_strFalse = lpszFalse;
  if (m_hWnd != NULL) Invalidate();
}

void CMFCPropertyGridCtrl::SetCustomColors(COLORREF clrBackground, COLORREF clrText, COLORREF clrGroupBackground,
                                           COLORREF clrGroupText, COLORREF clrDescriptionBackground,
                                           COLORREF clrDescriptionText, COLORREF clrLine) {
  m_clrBackground = clrBackground;
  m_clrText = clrText;
  m_clrGroupBackground = clrGroupBackground;
  m_clrGroupText = clrGroupText;
  m_clrDescriptionBackground = clrDescriptionBackground;
  m_clrDescriptionText = clrDescriptionText;
  m_clrLine = clrLine;
  if (m_hWnd != NULL) Invalidate();
}

void CMFCPropertyGridCtrl::GetCustomColors(COLORREF& clrBackground, COLORREF& clrText, COLORREF& clrGroupBackground,
                                           COLORREF& clrGroupText, COLORREF& clrDescriptionBackground,
                                           COLORREF& clrDescriptionText, COLORREF& clrLine) {
  clrBackground = m_clrBackground;
  clrText = m_clrText;
  clrGroupBackground = m_clrGroupBackground;
  clrGroupText = m_clrGroupText;
  clrDescriptionBackground = m_clrDescriptionBackground;
  clrDescriptionText = m_clrDescriptionText;
  clrLine = m_clrLine;
}

void CMFCPropertyGridCtrl::OnLButtonDown(UINT nFlags, CPoint point) {
  CWnd::OnLButtonDown(nFlags, point);
  if (GetFocus() != this) SetFocus();
  // The divider between the columns.
  if (m_rectList.PtInRect(point) && abs(point.x - (m_rectList.left + m_nLeftColumnWidth)) <= 2) {
    EndEditItem(TRUE);
    m_bTracking = TRUE;
    m_nTrackOffset = point.x - m_nLeftColumnWidth;
    SetCapture();
    return;
  }
  CMFCPropertyGridProperty::ClickArea area = CMFCPropertyGridProperty::ClickName;
  CMFCPropertyGridProperty* pProp = HitTest(point, &area, TRUE);
  if (pProp == NULL) {
    EndEditItem(TRUE);
    return;
  }
  if (area == CMFCPropertyGridProperty::ClickExpandBox) {
    pProp->Expand(!pProp->m_bExpanded);
    return;
  }
  SetCurSel(pProp);
  if (area == CMFCPropertyGridProperty::ClickValue && pProp->m_bEnabled) {
    if (pProp->HasButton()) {
      pProp->AdjustButtonRect();
      if (pProp->m_rectButton.PtInRect(point)) {
        if (!pProp->m_lstOptions.IsEmpty() || pProp->m_varValue.vt == VT_BOOL) {
          EditItem(pProp, &point);
          pProp->OnClickButton(point);
        } else {
          pProp->m_bButtonIsDown = TRUE;
          pProp->Redraw();
          UpdateWindow();
          pProp->OnClickButton(point);
          pProp->m_bButtonIsDown = FALSE;
          pProp->Redraw();
        }
        return;
      }
    }
    EditItem(pProp, &point);
  }
}

void CMFCPropertyGridCtrl::OnLButtonUp(UINT nFlags, CPoint point) {
  if (m_bTracking) {
    m_bTracking = FALSE;
    ReleaseCapture();
    int cx = point.x - m_nTrackOffset;
    if (cx < m_nRowHeight * 2) cx = m_nRowHeight * 2;
    if (cx > m_rectList.Width() - 20) cx = m_rectList.Width() - 20;
    SetLeftColumnWidth(cx);
    return;
  }
  CWnd::OnLButtonUp(nFlags, point);
}

void CMFCPropertyGridCtrl::OnLButtonDblClk(UINT nFlags, CPoint point) {
  CWnd::OnLButtonDblClk(nFlags, point);
  CMFCPropertyGridProperty::ClickArea area = CMFCPropertyGridProperty::ClickName;
  CMFCPropertyGridProperty* pProp = HitTest(point, &area, TRUE);
  if (pProp == NULL) return;
  if (!pProp->m_lstSubItems.IsEmpty() && area != CMFCPropertyGridProperty::ClickValue) {
    pProp->Expand(!pProp->m_bExpanded);
    return;
  }
  pProp->OnDblClk(point);
}

void CMFCPropertyGridCtrl::OnMouseMove(UINT nFlags, CPoint point) {
  if (m_bTracking) {
    int cx = point.x - m_nTrackOffset;
    if (cx >= m_nRowHeight * 2 && cx <= m_rectList.Width() - 20) SetLeftColumnWidth(cx);
    return;
  }
  CWnd::OnMouseMove(nFlags, point);
}

void CMFCPropertyGridCtrl::OnCancelMode() {
  CWnd::OnCancelMode();
  if (m_bTracking) {
    m_bTracking = FALSE;
    ReleaseCapture();
  }
}

BOOL CMFCPropertyGridCtrl::OnSetCursor(CWnd* pWnd, UINT nHitTest, UINT message) {
  if (nHitTest == HTCLIENT) {
    CPoint pt;
    ::GetCursorPos(&pt);
    ScreenToClient(&pt);
    if (m_rectList.PtInRect(pt) && abs(pt.x - (m_rectList.left + m_nLeftColumnWidth)) <= 2) {
      ::SetCursor(::LoadCursor(NULL, IDC_SIZEWE));
      return TRUE;
    }
  }
  return CWnd::OnSetCursor(pWnd, nHitTest, message);
}

void CMFCPropertyGridCtrl::OnVScroll(UINT nSBCode, UINT, CScrollBar*) {
  int nOffset = m_nVertScrollOffset, nPage = _VisibleRows();
  SCROLLINFO si;
  memset(&si, 0, sizeof(si));
  si.cbSize = sizeof(si);
  si.fMask = SIF_TRACKPOS;
  ::GetScrollInfo(m_hWnd, SB_VERT, &si);
  switch (nSBCode) {
    case SB_LINEUP:
      nOffset--;
      break;
    case SB_LINEDOWN:
      nOffset++;
      break;
    case SB_PAGEUP:
      nOffset -= nPage;
      break;
    case SB_PAGEDOWN:
      nOffset += nPage;
      break;
    case SB_THUMBTRACK:
    case SB_THUMBPOSITION:
      nOffset = si.nTrackPos;
      break;
    case SB_TOP:
      nOffset = 0;
      break;
    case SB_BOTTOM:
      nOffset = (int)m_arRows.GetSize();
      break;
    default:
      return;
  }
  int nMax = (int)m_arRows.GetSize() - nPage;
  if (nOffset > nMax) nOffset = nMax;
  if (nOffset < 0) nOffset = 0;
  if (nOffset == m_nVertScrollOffset) return;
  m_nVertScrollOffset = nOffset;
  AdjustLayout();
}

BOOL CMFCPropertyGridCtrl::OnMouseWheel(UINT, short zDelta, CPoint) {
  int nSteps = -zDelta / WHEEL_DELTA * 3;
  for (int i = 0; i < abs(nSteps); i++) OnVScroll(nSteps > 0 ? SB_LINEDOWN : SB_LINEUP, 0, NULL);
  return TRUE;
}

UINT CMFCPropertyGridCtrl::OnGetDlgCode() { return DLGC_WANTARROWS | DLGC_WANTCHARS; }

void CMFCPropertyGridCtrl::OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags) {
  if (m_arRows.GetSize() == 0) {
    CWnd::OnKeyDown(nChar, nRepCnt, nFlags);
    return;
  }
  int nIndex = -1;
  for (int i = 0; i < m_arRows.GetSize(); i++)
    if (m_arRows[i] == m_pSel) nIndex = i;
  int nNew = nIndex;
  switch (nChar) {
    case VK_UP:
      nNew = nIndex <= 0 ? 0 : nIndex - 1;
      break;
    case VK_DOWN:
      nNew = nIndex < 0 ? 0 : (nIndex + 1 < m_arRows.GetSize() ? nIndex + 1 : nIndex);
      break;
    case VK_HOME:
      nNew = 0;
      break;
    case VK_END:
      nNew = (int)m_arRows.GetSize() - 1;
      break;
    case VK_PRIOR:
      nNew = nIndex - _VisibleRows();
      if (nNew < 0) nNew = 0;
      break;
    case VK_NEXT:
      nNew = nIndex + _VisibleRows();
      if (nNew >= m_arRows.GetSize()) nNew = (int)m_arRows.GetSize() - 1;
      break;
    case VK_LEFT:
    case VK_SUBTRACT:
      if (m_pSel != NULL && !m_pSel->m_lstSubItems.IsEmpty() && m_pSel->m_bExpanded) {
        m_pSel->Expand(FALSE);
        return;
      }
      if (nChar == VK_LEFT && m_pSel != NULL && m_pSel->m_pParent != NULL) {
        SetCurSel(m_pSel->m_pParent);
        return;
      }
      return;
    case VK_RIGHT:
    case VK_ADD:
      if (m_pSel != NULL && !m_pSel->m_lstSubItems.IsEmpty() && !m_pSel->m_bExpanded) m_pSel->Expand(TRUE);
      return;
    case VK_RETURN:
    case VK_F2:
      if (m_pSel != NULL) {
        if (m_pSel->m_bGroup && !m_pSel->m_bIsValueList)
          m_pSel->Expand(!m_pSel->m_bExpanded);
        else
          EditItem(m_pSel);
      }
      return;
    case VK_F4:
      if (m_pSel != NULL && m_pSel->HasButton()) {
        EditItem(m_pSel);
        m_pSel->OnClickButton(CPoint(-1, -1));
      }
      return;
    default:
      CWnd::OnKeyDown(nChar, nRepCnt, nFlags);
      return;
  }
  if (nNew >= 0 && nNew < m_arRows.GetSize()) SetCurSel(m_arRows[nNew]);
}

void CMFCPropertyGridCtrl::OnSetFocus(CWnd* pOldWnd) {
  CWnd::OnSetFocus(pOldWnd);
  m_bFocused = TRUE;
  if (m_pSel != NULL) m_pSel->Redraw();
}

void CMFCPropertyGridCtrl::OnKillFocus(CWnd* pNewWnd) {
  CWnd::OnKillFocus(pNewWnd);
  // Focus going to the editor keeps the selection's look.
  if (pNewWnd != NULL && ::IsChild(m_hWnd, pNewWnd->m_hWnd)) return;
  m_bFocused = FALSE;
  if (m_pSel != NULL) m_pSel->Redraw();
}

BOOL CMFCPropertyGridCtrl::PreTranslateMessage(MSG* pMsg) {
  if (pMsg->message == WM_KEYDOWN && m_pSel != NULL && m_pSel->m_bInPlaceEdit &&
      ::IsChild(m_hWnd, pMsg->hwnd)) {
    switch (pMsg->wParam) {
      case VK_RETURN:
        if (m_pSel->m_pWndCombo != NULL && m_pSel->m_pWndCombo->GetDroppedState()) {
          m_pSel->m_pWndCombo->ShowDropDown(FALSE);
        }
        EndEditItem(TRUE);
        return TRUE;
      case VK_ESCAPE:
        EndEditItem(FALSE);
        return TRUE;
      case VK_TAB:
        EndEditItem(TRUE);
        return TRUE;
      case VK_UP:
      case VK_DOWN:
        if (m_pSel->m_pWndInPlace != NULL) {
          EndEditItem(TRUE);
          SendMessage(WM_KEYDOWN, pMsg->wParam, pMsg->lParam);
          return TRUE;
        }
        break;
    }
  }
  return CWnd::PreTranslateMessage(pMsg);
}

BOOL CMFCPropertyGridCtrl::OnCommand(WPARAM wParam, LPARAM lParam) {
  UINT nCode = HIWORD(wParam), nID = LOWORD(wParam);
  if (m_pSel != NULL && m_pSel->m_bInPlaceEdit) {
    if (nID == kComboID && nCode == CBN_SELENDOK) {
      m_pSel->OnSelectCombo();
      return TRUE;
    }
    if ((nID == kInPlaceID && nCode == EN_KILLFOCUS) || (nID == kComboID && nCode == CBN_KILLFOCUS)) {
      // Focus left the editor (and not for the list inside it): done.
      HWND hFocus = ::GetFocus();
      BOOL bInside = hFocus != NULL && (hFocus == (HWND)lParam || ::IsChild((HWND)lParam, hFocus));
      if (!bInside) EndEditItem(TRUE);
      return TRUE;
    }
  }
  return CWnd::OnCommand(wParam, lParam);
}

BOOL CMFCPropertyGridCtrl::OnNotify(WPARAM wParam, LPARAM lParam, LRESULT* pResult) {
  NMHDR* pNMHDR = (NMHDR*)lParam;
  if (pNMHDR->hwndFrom == m_wndHeader.m_hWnd && m_wndHeader.m_hWnd != NULL) {
    UINT code = pNMHDR->code;
    if (code == HDN_ITEMCHANGINGA || code == HDN_ITEMCHANGINGW || code == HDN_TRACKA || code == HDN_TRACKW ||
        code == HDN_ENDTRACKA || code == HDN_ENDTRACKW) {
      NMHEADER* pHdr = (NMHEADER*)lParam;
      if (pHdr->iItem == 0 && pHdr->pitem != NULL && (pHdr->pitem->mask & HDI_WIDTH)) {
        int cx = pHdr->pitem->cxy - 1;
        if (cx < m_nRowHeight * 2) cx = m_nRowHeight * 2;
        if (cx != m_nLeftColumnWidth) {
          m_nLeftColumnWidth = cx;
          EndEditItem(TRUE);
          AdjustLayout();
        }
      }
      *pResult = 0;
      return TRUE;
    }
  }
  return CWnd::OnNotify(wParam, lParam, pResult);
}
