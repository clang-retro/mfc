// ddxfield.h - the DDX_Field functions' work, shared by the ODBC classes'
// (CRecordset) and the DAO classes' (CDaoRecordset).
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <afxwin.h>

template <class SET>
inline BOOL _AfxDdxHasField(SET* pSet) {
  return pSet != NULL && pSet->IsOpen();
}

// Shows a null field as an empty edit control, and stores an empty
// control as null when the field allows it. DDX is DDX_Text's call.
template <class SET, class T, class DDX>
void _AfxDdxFieldText(CDataExchange* pDX, int nIDC, T* pv, SET* pSet, DDX ddx) {
  HWND hWnd = pDX->PrepareEditCtrl(nIDC);
  BOOL bHasField = _AfxDdxHasField(pSet);
  if (!pDX->m_bSaveAndValidate) {
    if (bHasField && pSet->IsFieldNull(pv)) {
      ::SetWindowText(hWnd, _T(""));
      return;
    }
    ddx();
    return;
  }
  if (bHasField && ::GetWindowTextLength(hWnd) == 0 && pSet->IsFieldNullable(pv)) {
    if (!pSet->IsFieldNull(pv)) pSet->SetFieldNull(pv);
    return;
  }
  ddx();
  if (bHasField && pSet->IsFieldNull(pv)) pSet->SetFieldNull(pv, FALSE);
}

template <class SET, class T>
void _AfxDdxFieldText(CDataExchange* pDX, int nIDC, T& value, SET* pSet) {
  _AfxDdxFieldText(pDX, nIDC, &value, pSet, [&] { DDX_Text(pDX, nIDC, value); });
}

// The others show a null field as no selection (or an indeterminate
// check box) and store a value as usual.
template <class SET, class T, class F>
void _AfxDdxFieldOther(CDataExchange* pDX, int nIDC, T& value, SET* pSet, T nullValue, F ddx) {
  if (!pDX->m_bSaveAndValidate && _AfxDdxHasField(pSet) && pSet->IsFieldNull(&value)) {
    T temp = nullValue;
    ddx(pDX, nIDC, temp);
    return;
  }
  BOOL bWasNull = pDX->m_bSaveAndValidate && _AfxDdxHasField(pSet) && pSet->IsFieldNull(&value);
  ddx(pDX, nIDC, value);
  if (bWasNull) pSet->SetFieldNull(&value, FALSE);
}

// The DDX_Field functions for a recordset class: DDX_FIELD_FUNCTIONS(CRecordset).
#define _AFX_DDX_FIELD_TEXT(SET, T)                                                   \
  void AFXAPI DDX_FieldText(CDataExchange* pDX, int nIDC, T& value, SET* pSet) {    \
    _AfxDdxFieldText(pDX, nIDC, value, pSet);                                        \
  }
#define _AFX_DDX_FIELD_FUNCTIONS(SET)                                                 \
  _AFX_DDX_FIELD_TEXT(SET, BYTE)                                                      \
  _AFX_DDX_FIELD_TEXT(SET, int)                                                       \
  _AFX_DDX_FIELD_TEXT(SET, UINT)                                                      \
  _AFX_DDX_FIELD_TEXT(SET, long)                                                      \
  _AFX_DDX_FIELD_TEXT(SET, DWORD)                                                     \
  _AFX_DDX_FIELD_TEXT(SET, CString)                                                   \
  _AFX_DDX_FIELD_TEXT(SET, float)                                                     \
  _AFX_DDX_FIELD_TEXT(SET, double)                                                    \
  _AFX_DDX_FIELD_TEXT(SET, short)                                                     \
  void AFXAPI DDX_FieldText(CDataExchange* pDX, int nIDC, LPTSTR pstrValue, int nMaxLen, \
                            SET* pSet) {                                              \
    _AfxDdxFieldText(pDX, nIDC, pstrValue, pSet,                                      \
                     [&] { DDX_Text(pDX, nIDC, pstrValue, nMaxLen); });               \
  }                                                                                   \
  void AFXAPI DDX_FieldCheck(CDataExchange* pDX, int nIDC, int& value, SET* pSet) {  \
    _AfxDdxFieldOther(pDX, nIDC, value, pSet, (int)BST_INDETERMINATE, DDX_Check);     \
  }                                                                                   \
  void AFXAPI DDX_FieldRadio(CDataExchange* pDX, int nIDC, int& value, SET* pSet) {  \
    _AfxDdxFieldOther(pDX, nIDC, value, pSet, -1, DDX_Radio);                         \
  }                                                                                   \
  void AFXAPI DDX_FieldLBString(CDataExchange* pDX, int nIDC, CString& value, SET* pSet) { \
    _AfxDdxFieldOther(pDX, nIDC, value, pSet, CString(), DDX_LBString);               \
  }                                                                                   \
  void AFXAPI DDX_FieldCBString(CDataExchange* pDX, int nIDC, CString& value, SET* pSet) { \
    _AfxDdxFieldOther(pDX, nIDC, value, pSet, CString(), DDX_CBString);               \
  }                                                                                   \
  void AFXAPI DDX_FieldLBStringExact(CDataExchange* pDX, int nIDC, CString& value,    \
                                     SET* pSet) {                                     \
    _AfxDdxFieldOther(pDX, nIDC, value, pSet, CString(), DDX_LBStringExact);          \
  }                                                                                   \
  void AFXAPI DDX_FieldCBStringExact(CDataExchange* pDX, int nIDC, CString& value,    \
                                     SET* pSet) {                                     \
    _AfxDdxFieldOther(pDX, nIDC, value, pSet, CString(), DDX_CBStringExact);          \
  }                                                                                   \
  void AFXAPI DDX_FieldLBIndex(CDataExchange* pDX, int nIDC, int& index, SET* pSet) { \
    _AfxDdxFieldOther(pDX, nIDC, index, pSet, -1, DDX_LBIndex);                       \
  }                                                                                   \
  void AFXAPI DDX_FieldCBIndex(CDataExchange* pDX, int nIDC, int& index, SET* pSet) { \
    _AfxDdxFieldOther(pDX, nIDC, index, pSet, -1, DDX_CBIndex);                       \
  }                                                                                   \
  void AFXAPI DDX_FieldScroll(CDataExchange* pDX, int nIDC, int& value, SET* pSet) { \
    _AfxDdxFieldOther(pDX, nIDC, value, pSet, 0, DDX_Scroll);                         \
  }                                                                                   \
  void AFXAPI DDX_FieldSlider(CDataExchange* pDX, int nIDC, int& value, SET* pSet) { \
    _AfxDdxFieldOther(pDX, nIDC, value, pSet, 0, DDX_Slider);                         \
  }
