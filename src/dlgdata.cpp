// dlgdata.cpp - dialog data exchange (DDX_) and validation (DDV_).
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
#include "afximpl.h"
#include <errno.h>
#include <float.h>
#include <stdlib.h>

CDataExchange::CDataExchange(CWnd* pDlgWnd, BOOL bSaveAndValidate)
    : m_bSaveAndValidate(bSaveAndValidate),
      m_pDlgWnd(pDlgWnd),
      m_hWndLastControl(NULL),
      m_bEditLastControl(FALSE) {}

HWND CDataExchange::PrepareCtrl(int nIDC) {
  HWND hWndCtrl;
  m_pDlgWnd->GetDlgItem(nIDC, &hWndCtrl);
  if (hWndCtrl == NULL) {
    TRACE(_T("Error: no control with ID %d in the dialog.\n"), nIDC);
    AfxThrowNotSupportedException();
  }
  m_hWndLastControl = hWndCtrl;
  m_bEditLastControl = FALSE;
  return hWndCtrl;
}

HWND CDataExchange::PrepareEditCtrl(int nIDC) {
  HWND hWndCtrl = PrepareCtrl(nIDC);
  m_bEditLastControl = TRUE;
  return hWndCtrl;
}

CWnd* CDataExchange::PrepareOleCtrl(int nIDC) { return CWnd::FromHandle(PrepareCtrl(nIDC)); }

void CDataExchange::Fail() {
  if (m_bSaveAndValidate && m_hWndLastControl != NULL) {
    // Put the user back on the control that failed.
    ::SetFocus(m_hWndLastControl);
    if (m_bEditLastControl) ::SendMessage(m_hWndLastControl, EM_SETSEL, 0, -1);
  }
  AfxThrowUserException();
}

// ---------------------------------------------------------------------
// Helpers

static void _AfxSetWindowText(HWND hWnd, LPCTSTR lpszNew) {
  // Only set text that changed, to avoid flicker and EN_CHANGE storms.
  int nNewLen = lstrlen(lpszNew);
  TCHAR szOld[256];
  if (nNewLen > (int)(sizeof(szOld) / sizeof(TCHAR)) - 1 ||
      ::GetWindowText(hWnd, szOld, sizeof(szOld) / sizeof(TCHAR)) != nNewLen ||
      lstrcmp(szOld, lpszNew) != 0)
    ::SetWindowText(hWnd, lpszNew);
}

static CString _AfxGetWindowText(HWND hWnd) {
  CString str;
  int nLen = ::GetWindowTextLength(hWnd);
  ::GetWindowText(hWnd, str.GetBufferSetLength(nLen), nLen + 1);
  str.ReleaseBuffer();
  return str;
}

// Parses a whole field as a number. Leading and trailing blanks are
// allowed; anything else is an error.
static BOOL _AfxParseLongLong(HWND hWnd, LONGLONG* pValue, BOOL bUnsigned) {
  CStringA str(_AfxGetWindowText(hWnd));
  str.Trim();
  if (str.IsEmpty()) return FALSE;
  if (bUnsigned && str[0] == '-') return FALSE;
  char* pEnd;
  errno = 0;
  LONGLONG value = bUnsigned ? (LONGLONG)strtoull(str, &pEnd, 10) : strtoll(str, &pEnd, 10);
  if (*pEnd != 0 || errno == ERANGE) return FALSE;
  *pValue = value;
  return TRUE;
}

static BOOL _AfxParseDouble(HWND hWnd, double* pValue) {
  CStringA str(_AfxGetWindowText(hWnd));
  str.Trim();
  if (str.IsEmpty()) return FALSE;
  char* pEnd;
  double value = strtod(str, &pEnd);
  if (*pEnd != 0) return FALSE;
  *pValue = value;
  return TRUE;
}

static void _AfxFailMessage(CDataExchange* pDX, UINT nIDPrompt) {
  AfxMessageBox(nIDPrompt, MB_ICONEXCLAMATION);
  pDX->Fail();
}

template <class T>
static void _AfxDDXInteger(CDataExchange* pDX, int nIDC, T& value, LONGLONG nMin, LONGLONG nMax,
                           BOOL bUnsigned, UINT nIDPrompt, LPCTSTR lpszFormat) {
  HWND hWndCtrl = pDX->PrepareEditCtrl(nIDC);
  if (pDX->m_bSaveAndValidate) {
    LONGLONG v;
    if (!_AfxParseLongLong(hWndCtrl, &v, bUnsigned) || v < nMin || v > nMax)
      _AfxFailMessage(pDX, nIDPrompt);
    value = (T)v;
  } else {
    CString str;
    str.Format(lpszFormat, value);
    _AfxSetWindowText(hWndCtrl, str);
  }
}

// ---------------------------------------------------------------------
// DDX_Text

void AFXAPI DDX_Text(CDataExchange* pDX, int nIDC, BYTE& value) {
  int n = value;
  _AfxDDXInteger(pDX, nIDC, n, 0, 255, TRUE, AFX_IDP_PARSE_BYTE, _T("%d"));
  value = (BYTE)n;
}

void AFXAPI DDX_Text(CDataExchange* pDX, int nIDC, short& value) {
  int n = value;
  _AfxDDXInteger(pDX, nIDC, n, -32768, 32767, FALSE, AFX_IDP_PARSE_INT, _T("%d"));
  value = (short)n;
}

void AFXAPI DDX_Text(CDataExchange* pDX, int nIDC, int& value) {
  _AfxDDXInteger(pDX, nIDC, value, INT_MIN, INT_MAX, FALSE, AFX_IDP_PARSE_INT, _T("%d"));
}

void AFXAPI DDX_Text(CDataExchange* pDX, int nIDC, UINT& value) {
  _AfxDDXInteger(pDX, nIDC, value, 0, UINT_MAX, TRUE, AFX_IDP_PARSE_UINT, _T("%u"));
}

void AFXAPI DDX_Text(CDataExchange* pDX, int nIDC, long& value) {
  _AfxDDXInteger(pDX, nIDC, value, LONG_MIN, LONG_MAX, FALSE, AFX_IDP_PARSE_INT, _T("%ld"));
}

void AFXAPI DDX_Text(CDataExchange* pDX, int nIDC, DWORD& value) {
  _AfxDDXInteger(pDX, nIDC, value, 0, 0xFFFFFFFFLL, TRUE, AFX_IDP_PARSE_UINT, _T("%lu"));
}

void AFXAPI DDX_Text(CDataExchange* pDX, int nIDC, LONGLONG& value) {
  _AfxDDXInteger(pDX, nIDC, value, LLONG_MIN, LLONG_MAX, FALSE, AFX_IDP_PARSE_INT, _T("%I64d"));
}

void AFXAPI DDX_Text(CDataExchange* pDX, int nIDC, ULONGLONG& value) {
  HWND hWndCtrl = pDX->PrepareEditCtrl(nIDC);
  if (pDX->m_bSaveAndValidate) {
    LONGLONG v;
    if (!_AfxParseLongLong(hWndCtrl, &v, TRUE)) _AfxFailMessage(pDX, AFX_IDP_PARSE_UINT);
    value = (ULONGLONG)v;
  } else {
    CString str;
    str.Format(_T("%I64u"), value);
    _AfxSetWindowText(hWndCtrl, str);
  }
}

void AFXAPI DDX_Text(CDataExchange* pDX, int nIDC, CString& value) {
  HWND hWndCtrl = pDX->PrepareEditCtrl(nIDC);
  if (pDX->m_bSaveAndValidate)
    value = _AfxGetWindowText(hWndCtrl);
  else
    _AfxSetWindowText(hWndCtrl, value);
}

void AFXAPI DDX_Text(CDataExchange* pDX, int nIDC, LPTSTR value, int nMaxLen) {
  HWND hWndCtrl = pDX->PrepareEditCtrl(nIDC);
  if (pDX->m_bSaveAndValidate) {
    ::GetWindowText(hWndCtrl, value, nMaxLen);
  } else {
    ::SendMessage(hWndCtrl, EM_LIMITTEXT, (WPARAM)(nMaxLen - 1), 0);
    _AfxSetWindowText(hWndCtrl, value);
  }
}

void AFXAPI DDX_Text(CDataExchange* pDX, int nIDC, float& value) {
  HWND hWndCtrl = pDX->PrepareEditCtrl(nIDC);
  if (pDX->m_bSaveAndValidate) {
    double d;
    if (!_AfxParseDouble(hWndCtrl, &d) || d > FLT_MAX || d < -FLT_MAX)
      _AfxFailMessage(pDX, AFX_IDP_PARSE_REAL);
    value = (float)d;
  } else {
    CString str;
    str.Format(_T("%.*g"), FLT_DIG, (double)value);
    _AfxSetWindowText(hWndCtrl, str);
  }
}

void AFXAPI DDX_Text(CDataExchange* pDX, int nIDC, double& value) {
  HWND hWndCtrl = pDX->PrepareEditCtrl(nIDC);
  if (pDX->m_bSaveAndValidate) {
    double d;
    if (!_AfxParseDouble(hWndCtrl, &d)) _AfxFailMessage(pDX, AFX_IDP_PARSE_REAL);
    value = d;
  } else {
    CString str;
    str.Format(_T("%.*g"), DBL_DIG, value);
    _AfxSetWindowText(hWndCtrl, str);
  }
}

// ---------------------------------------------------------------------
// Buttons, list boxes and combo boxes

void AFXAPI DDX_Check(CDataExchange* pDX, int nIDC, int& value) {
  HWND hWndCtrl = pDX->PrepareCtrl(nIDC);
  if (pDX->m_bSaveAndValidate) {
    value = (int)::SendMessage(hWndCtrl, BM_GETCHECK, 0, 0);
  } else {
    if (value < 0 || value > 2) value = 0;
    ::SendMessage(hWndCtrl, BM_SETCHECK, (WPARAM)value, 0);
  }
}

void AFXAPI DDX_Radio(CDataExchange* pDX, int nIDC, int& value) {
  // nIDC is the first button of a group; the group runs until the next
  // control with WS_GROUP.
  HWND hWndCtrl = pDX->PrepareCtrl(nIDC);
  if (pDX->m_bSaveAndValidate) value = -1;
  int iButton = 0;
  do {
    if (::SendMessage(hWndCtrl, WM_GETDLGCODE, 0, 0) & DLGC_RADIOBUTTON) {
      if (pDX->m_bSaveAndValidate) {
        if (::SendMessage(hWndCtrl, BM_GETCHECK, 0, 0) != 0) value = iButton;
      } else {
        ::SendMessage(hWndCtrl, BM_SETCHECK, (WPARAM)(iButton == value), 0);
      }
      iButton++;
    }
    hWndCtrl = ::GetWindow(hWndCtrl, GW_HWNDNEXT);
  } while (hWndCtrl != NULL && !(::GetWindowLong(hWndCtrl, GWL_STYLE) & WS_GROUP));
}

void AFXAPI DDX_LBString(CDataExchange* pDX, int nIDC, CString& value) {
  HWND hWndCtrl = pDX->PrepareCtrl(nIDC);
  if (pDX->m_bSaveAndValidate) {
    int nIndex = (int)::SendMessage(hWndCtrl, LB_GETCURSEL, 0, 0);
    if (nIndex != LB_ERR) {
      int nLen = (int)::SendMessage(hWndCtrl, LB_GETTEXTLEN, (WPARAM)nIndex, 0);
      ::SendMessage(hWndCtrl, LB_GETTEXT, (WPARAM)nIndex, (LPARAM)value.GetBufferSetLength(nLen));
      value.ReleaseBuffer();
    } else {
      value.Empty();
    }
  } else {
    if (::SendMessage(hWndCtrl, LB_SELECTSTRING, (WPARAM)-1, (LPARAM)(LPCTSTR)value) == LB_ERR)
      ::SendMessage(hWndCtrl, LB_SETCURSEL, (WPARAM)-1, 0);
  }
}

void AFXAPI DDX_LBStringExact(CDataExchange* pDX, int nIDC, CString& value) {
  HWND hWndCtrl = pDX->PrepareCtrl(nIDC);
  if (pDX->m_bSaveAndValidate) {
    DDX_LBString(pDX, nIDC, value);
  } else {
    int i = (int)::SendMessage(hWndCtrl, LB_FINDSTRINGEXACT, (WPARAM)-1, (LPARAM)(LPCTSTR)value);
    ::SendMessage(hWndCtrl, LB_SETCURSEL, (WPARAM)i, 0);
  }
}

void AFXAPI DDX_CBString(CDataExchange* pDX, int nIDC, CString& value) {
  HWND hWndCtrl = pDX->PrepareCtrl(nIDC);
  if ((::GetWindowLong(hWndCtrl, GWL_STYLE) & CBS_DROPDOWNLIST) != CBS_DROPDOWNLIST)
    pDX->m_bEditLastControl = TRUE;
  if (pDX->m_bSaveAndValidate) {
    int nLen = ::GetWindowTextLength(hWndCtrl);
    if (nLen > 0) {
      ::GetWindowText(hWndCtrl, value.GetBufferSetLength(nLen), nLen + 1);
      value.ReleaseBuffer();
    } else {
      // Drop-down lists may report no window text; use the selection.
      int nIndex = (int)::SendMessage(hWndCtrl, CB_GETCURSEL, 0, 0);
      if (nIndex != CB_ERR) {
        nLen = (int)::SendMessage(hWndCtrl, CB_GETLBTEXTLEN, (WPARAM)nIndex, 0);
        ::SendMessage(hWndCtrl, CB_GETLBTEXT, (WPARAM)nIndex,
                      (LPARAM)value.GetBufferSetLength(nLen));
        value.ReleaseBuffer();
      } else {
        value.Empty();
      }
    }
  } else {
    if (::SendMessage(hWndCtrl, CB_SELECTSTRING, (WPARAM)-1, (LPARAM)(LPCTSTR)value) == CB_ERR)
      _AfxSetWindowText(hWndCtrl, value);  // ignored by drop-down lists
  }
}

void AFXAPI DDX_CBStringExact(CDataExchange* pDX, int nIDC, CString& value) {
  HWND hWndCtrl = pDX->PrepareCtrl(nIDC);
  if (pDX->m_bSaveAndValidate) {
    DDX_CBString(pDX, nIDC, value);
  } else {
    int i = (int)::SendMessage(hWndCtrl, CB_FINDSTRINGEXACT, (WPARAM)-1, (LPARAM)(LPCTSTR)value);
    if (i < 0)
      _AfxSetWindowText(hWndCtrl, value);
    else
      ::SendMessage(hWndCtrl, CB_SETCURSEL, (WPARAM)i, 0);
  }
}

void AFXAPI DDX_LBIndex(CDataExchange* pDX, int nIDC, int& index) {
  HWND hWndCtrl = pDX->PrepareCtrl(nIDC);
  if (pDX->m_bSaveAndValidate)
    index = (int)::SendMessage(hWndCtrl, LB_GETCURSEL, 0, 0);
  else
    ::SendMessage(hWndCtrl, LB_SETCURSEL, (WPARAM)index, 0);
}

void AFXAPI DDX_CBIndex(CDataExchange* pDX, int nIDC, int& index) {
  HWND hWndCtrl = pDX->PrepareCtrl(nIDC);
  if (pDX->m_bSaveAndValidate)
    index = (int)::SendMessage(hWndCtrl, CB_GETCURSEL, 0, 0);
  else
    ::SendMessage(hWndCtrl, CB_SETCURSEL, (WPARAM)index, 0);
}

void AFXAPI DDX_Scroll(CDataExchange* pDX, int nIDC, int& value) {
  HWND hWndCtrl = pDX->PrepareCtrl(nIDC);
  if (pDX->m_bSaveAndValidate)
    value = ::GetScrollPos(hWndCtrl, SB_CTL);
  else
    ::SetScrollPos(hWndCtrl, SB_CTL, value, TRUE);
}

void AFXAPI DDX_Slider(CDataExchange* pDX, int nIDC, int& value) {
  HWND hWndCtrl = pDX->PrepareCtrl(nIDC);
  if (pDX->m_bSaveAndValidate)
    value = (int)::SendMessage(hWndCtrl, TBM_GETPOS, 0, 0);
  else
    ::SendMessage(hWndCtrl, TBM_SETPOS, TRUE, value);
}

void AFXAPI DDX_Control(CDataExchange* pDX, int nIDC, CWnd& rControl) {
  if (rControl.m_hWnd == NULL) {  // only subclass once
    HWND hWndCtrl = pDX->PrepareCtrl(nIDC);
    if (!rControl.SubclassWindow(hWndCtrl)) AfxThrowNotSupportedException();
  }
}

// ---------------------------------------------------------------------
// DDV_ range checks

static void _AfxFailRange(CDataExchange* pDX, UINT nIDPrompt, LPCTSTR lpszMin, LPCTSTR lpszMax) {
  CString prompt;
  AfxFormatString2(prompt, nIDPrompt, lpszMin, lpszMax);
  AfxMessageBox(prompt, MB_ICONEXCLAMATION, nIDPrompt);
  pDX->Fail();
}

static void _AfxCheckRange(CDataExchange* pDX, LONGLONG value, LONGLONG minVal, LONGLONG maxVal,
                           LPCTSTR lpszFormat) {
  if (!pDX->m_bSaveAndValidate) return;
  if (value < minVal || value > maxVal) {
    CString strMin;
    CString strMax;
    strMin.Format(lpszFormat, minVal);
    strMax.Format(lpszFormat, maxVal);
    _AfxFailRange(pDX, AFX_IDP_PARSE_INT_RANGE, strMin, strMax);
  }
}

void AFXAPI DDV_MinMaxByte(CDataExchange* pDX, BYTE value, BYTE minVal, BYTE maxVal) {
  _AfxCheckRange(pDX, value, minVal, maxVal, _T("%I64d"));
}
void AFXAPI DDV_MinMaxShort(CDataExchange* pDX, short value, short minVal, short maxVal) {
  _AfxCheckRange(pDX, value, minVal, maxVal, _T("%I64d"));
}
void AFXAPI DDV_MinMaxInt(CDataExchange* pDX, int value, int minVal, int maxVal) {
  _AfxCheckRange(pDX, value, minVal, maxVal, _T("%I64d"));
}
void AFXAPI DDV_MinMaxLong(CDataExchange* pDX, long value, long minVal, long maxVal) {
  _AfxCheckRange(pDX, value, minVal, maxVal, _T("%I64d"));
}
void AFXAPI DDV_MinMaxUInt(CDataExchange* pDX, UINT value, UINT minVal, UINT maxVal) {
  _AfxCheckRange(pDX, value, minVal, maxVal, _T("%I64d"));
}
void AFXAPI DDV_MinMaxDWord(CDataExchange* pDX, DWORD value, DWORD minVal, DWORD maxVal) {
  _AfxCheckRange(pDX, value, minVal, maxVal, _T("%I64d"));
}
void AFXAPI DDV_MinMaxLongLong(CDataExchange* pDX, LONGLONG value, LONGLONG minVal,
                               LONGLONG maxVal) {
  _AfxCheckRange(pDX, value, minVal, maxVal, _T("%I64d"));
}

void AFXAPI DDV_MinMaxULongLong(CDataExchange* pDX, ULONGLONG value, ULONGLONG minVal,
                                ULONGLONG maxVal) {
  if (!pDX->m_bSaveAndValidate) return;
  if (value < minVal || value > maxVal) {
    CString strMin;
    CString strMax;
    strMin.Format(_T("%I64u"), minVal);
    strMax.Format(_T("%I64u"), maxVal);
    _AfxFailRange(pDX, AFX_IDP_PARSE_INT_RANGE, strMin, strMax);
  }
}

static void _AfxCheckRealRange(CDataExchange* pDX, double value, double minVal, double maxVal,
                               int nPrecision) {
  if (!pDX->m_bSaveAndValidate) return;
  if (value < minVal || value > maxVal) {
    CString strMin;
    CString strMax;
    strMin.Format(_T("%.*g"), nPrecision, minVal);
    strMax.Format(_T("%.*g"), nPrecision, maxVal);
    _AfxFailRange(pDX, AFX_IDP_PARSE_REAL_RANGE, strMin, strMax);
  }
}

void AFXAPI DDV_MinMaxFloat(CDataExchange* pDX, float const& value, float minVal, float maxVal) {
  _AfxCheckRealRange(pDX, value, minVal, maxVal, FLT_DIG);
}

void AFXAPI DDV_MinMaxDouble(CDataExchange* pDX, double const& value, double minVal,
                             double maxVal) {
  _AfxCheckRealRange(pDX, value, minVal, maxVal, DBL_DIG);
}

void AFXAPI DDV_MaxChars(CDataExchange* pDX, CString const& value, int nChars) {
  if (pDX->m_bSaveAndValidate) {
    if (value.GetLength() > nChars) {
      CString prompt;
      CString strChars;
      strChars.Format(_T("%d"), nChars);
      AfxFormatString1(prompt, AFX_IDP_PARSE_STRING_SIZE, strChars);
      AfxMessageBox(prompt, MB_ICONEXCLAMATION, AFX_IDP_PARSE_STRING_SIZE);
      pDX->Fail();
    }
  } else if (pDX->m_hWndLastControl != NULL && pDX->m_bEditLastControl) {
    ::SendMessage(pDX->m_hWndLastControl, EM_LIMITTEXT, (WPARAM)nChars, 0);
  }
}

void AFXAPI DDV_MinMaxSlider(CDataExchange* pDX, DWORD value, DWORD minVal, DWORD maxVal) {
  if (pDX->m_bSaveAndValidate) {
    _AfxCheckRange(pDX, value, minVal, maxVal, _T("%I64d"));
  } else if (pDX->m_hWndLastControl != NULL) {
    ::SendMessage(pDX->m_hWndLastControl, TBM_SETRANGEMIN, FALSE, (LPARAM)minVal);
    ::SendMessage(pDX->m_hWndLastControl, TBM_SETRANGEMAX, TRUE, (LPARAM)maxVal);
  }
}
