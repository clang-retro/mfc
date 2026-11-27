// fpedit.cpp - the Feature Pack's edit controls and their kin:
// CMFCEditBrowseCtrl (an edit with a browse button in its non-client
// area), CMFCMaskedEdit (input to a mask and template), CMFCFontComboBox
// and CMFCSpinButtonCtrl (drawn by the visual manager).
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include "afximpl.h"
#include <afxeditbrowsectrl.h>
#include <afxmaskededit.h>
#include <afxfontcombobox.h>
#include <afxspinbuttonctrl.h>
#include <afxwinappex.h>
#include <afxdlgs.h>

#ifndef WM_MOUSELEAVE
#define WM_MOUSELEAVE 0x02A3
#endif
#ifndef WM_NCMOUSELEAVE
#define WM_NCMOUSELEAVE 0x02A2
#endif

// ---------------------------------------------------------------------
// CMFCEditBrowseCtrl

IMPLEMENT_DYNAMIC(CMFCEditBrowseCtrl, CEdit)

BEGIN_MESSAGE_MAP(CMFCEditBrowseCtrl, CEdit)
  ON_WM_NCCALCSIZE()
  ON_WM_NCPAINT()
  ON_WM_NCHITTEST()
  ON_WM_NCLBUTTONDOWN()
  ON_WM_NCMOUSEMOVE()
  ON_MESSAGE(WM_NCMOUSELEAVE, OnNcMouseLeave)
  ON_WM_LBUTTONUP()
  ON_WM_MOUSEMOVE()
  ON_WM_CANCELMODE()
  ON_WM_KEYDOWN()
  ON_WM_SETCURSOR()
END_MESSAGE_MAP()

CMFCEditBrowseCtrl::CMFCEditBrowseCtrl()
    : m_Mode(BrowseMode_None), m_dwFileDialogFlags(OFN_HIDEREADONLY | OFN_OVERWRITEPROMPT),
      m_nBrowseButtonWidth(20), m_bIsButtonPressed(FALSE), m_bIsButtonHighlighted(FALSE),
      m_bIsButtonCaptured(FALSE), m_bDefaultImage(TRUE), m_hIcon(NULL), m_hBitmap(NULL),
      m_bAutoDestroyImage(FALSE), m_sizeImage(0, 0) {}

CMFCEditBrowseCtrl::~CMFCEditBrowseCtrl() { _FreeImage(); }

void CMFCEditBrowseCtrl::_FreeImage() {
  if (m_bAutoDestroyImage) {
    if (m_hIcon != NULL) ::DestroyIcon(m_hIcon);
    if (m_hBitmap != NULL) ::DeleteObject(m_hBitmap);
  }
  m_hIcon = NULL;
  m_hBitmap = NULL;
  m_bAutoDestroyImage = FALSE;
  m_sizeImage = CSize(0, 0);
}

void CMFCEditBrowseCtrl::EnableBrowseButton(BOOL bEnable, LPCTSTR szLabel) {
  m_Mode = bEnable ? BrowseMode_Default : BrowseMode_None;
  m_strLabel = szLabel != NULL ? szLabel : _T("");
  OnChangeLayout();
}

void CMFCEditBrowseCtrl::EnableFileBrowseButton(LPCTSTR lpszDefExt, LPCTSTR lpszFilter, DWORD dwFlags) {
  m_Mode = BrowseMode_File;
  m_strDefFileExt = lpszDefExt != NULL ? lpszDefExt : _T("");
  m_strFileFilter = lpszFilter != NULL ? lpszFilter : _T("");
  m_dwFileDialogFlags = dwFlags;
  OnChangeLayout();
}

void CMFCEditBrowseCtrl::EnableFolderBrowseButton() {
  m_Mode = BrowseMode_Folder;
  OnChangeLayout();
}

void CMFCEditBrowseCtrl::SetBrowseButtonImage(HICON hIcon, BOOL bAutoDestroy) {
  _FreeImage();
  m_hIcon = hIcon;
  m_bAutoDestroyImage = bAutoDestroy;
  m_bDefaultImage = hIcon == NULL;
  if (hIcon != NULL) {
    ICONINFO ii;
    if (::GetIconInfo(hIcon, &ii)) {
      BITMAP bm;
      if (ii.hbmColor != NULL && ::GetObject(ii.hbmColor, sizeof(bm), &bm))
        m_sizeImage = CSize(bm.bmWidth, bm.bmHeight);
      else if (ii.hbmMask != NULL && ::GetObject(ii.hbmMask, sizeof(bm), &bm))
        m_sizeImage = CSize(bm.bmWidth, bm.bmHeight / 2);
      if (ii.hbmColor != NULL) ::DeleteObject(ii.hbmColor);
      if (ii.hbmMask != NULL) ::DeleteObject(ii.hbmMask);
    }
    if (m_sizeImage.cx == 0) m_sizeImage = CSize(::GetSystemMetrics(SM_CXSMICON), ::GetSystemMetrics(SM_CYSMICON));
  }
  OnChangeLayout();
}

void CMFCEditBrowseCtrl::SetBrowseButtonImage(HBITMAP hBitmap, BOOL bAutoDestroy) {
  _FreeImage();
  m_hBitmap = hBitmap;
  m_bAutoDestroyImage = bAutoDestroy;
  m_bDefaultImage = hBitmap == NULL;
  BITMAP bm;
  if (hBitmap != NULL && ::GetObject(hBitmap, sizeof(bm), &bm)) m_sizeImage = CSize(bm.bmWidth, bm.bmHeight);
  OnChangeLayout();
}

void CMFCEditBrowseCtrl::SetBrowseButtonImage(UINT uiBmpResId) {
  HBITMAP hBitmap = uiBmpResId != 0 ? ::LoadBitmap(AfxGetResourceHandle(), MAKEINTRESOURCE(uiBmpResId)) : NULL;
  SetBrowseButtonImage(hBitmap, TRUE);
}

void CMFCEditBrowseCtrl::OnChangeLayout() {
  if (m_hWnd == NULL) return;
  if (m_Mode == BrowseMode_None) {
    m_nBrowseButtonWidth = 0;
  } else if (m_sizeImage.cx > 0) {
    m_nBrowseButtonWidth = m_sizeImage.cx + 8;
  } else {
    // Wide enough for the label (or the folder and file buttons' "...").
    CClientDC dc(this);
    CFont* pOld = dc.SelectObject(GetFont());
    CString str = m_strLabel.IsEmpty() ? CString(_T("...")) : m_strLabel;
    m_nBrowseButtonWidth = dc.GetTextExtent(str).cx + 10;
    if (m_nBrowseButtonWidth < ::GetSystemMetrics(SM_CXVSCROLL) + 2)
      m_nBrowseButtonWidth = ::GetSystemMetrics(SM_CXVSCROLL) + 2;
    dc.SelectObject(pOld);
  }
  SetWindowPos(NULL, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

CRect CMFCEditBrowseCtrl::GetBrowseButtonRect() const {
  if (m_hWnd == NULL || m_Mode == BrowseMode_None) return CRect(0, 0, 0, 0);
  CRect rcWnd, rcClient;
  ::GetWindowRect(m_hWnd, &rcWnd);
  ::GetClientRect(m_hWnd, &rcClient);
  ::MapWindowPoints(m_hWnd, NULL, (LPPOINT)&rcClient, 2);
  rcClient.OffsetRect(-rcWnd.left, -rcWnd.top);
  return CRect(rcClient.right, rcClient.top, rcClient.right + m_nBrowseButtonWidth, rcClient.bottom);
}

BOOL CMFCEditBrowseCtrl::_InButton(CPoint ptScreen) const {
  CRect rcWnd;
  ::GetWindowRect(m_hWnd, &rcWnd);
  CRect rcButton = GetBrowseButtonRect();
  rcButton.OffsetRect(rcWnd.left, rcWnd.top);
  return rcButton.PtInRect(ptScreen);
}

void CMFCEditBrowseCtrl::OnNcCalcSize(BOOL bCalcValidRects, NCCALCSIZE_PARAMS* lpncsp) {
  CEdit::OnNcCalcSize(bCalcValidRects, lpncsp);
  if (m_Mode != BrowseMode_None) lpncsp->rgrc[0].right -= m_nBrowseButtonWidth;
}

void CMFCEditBrowseCtrl::OnNcPaint() {
  CEdit::OnNcPaint();
  if (m_Mode == BrowseMode_None) return;
  CWindowDC dc(this);
  OnDrawBrowseButton(&dc, GetBrowseButtonRect(), m_bIsButtonPressed && m_bIsButtonHighlighted,
                     m_bIsButtonHighlighted);
}

void CMFCEditBrowseCtrl::OnDrawBrowseButton(CDC* pDC, CRect rect, BOOL bIsButtonPressed, BOOL bIsButtonHot) {
  CMFCVisualManager::AFX_BUTTON_STATE state = bIsButtonPressed ? CMFCVisualManager::ButtonsIsPressed
                                              : bIsButtonHot   ? CMFCVisualManager::ButtonsIsHighlighted
                                                               : CMFCVisualManager::ButtonsIsRegular;
  COLORREF clrText = ::GetSysColor(COLOR_BTNTEXT);
  if (!CMFCVisualManager::GetInstance()->OnDrawBrowseButton(pDC, rect, this, state, clrText))
    pDC->DrawFrameControl(rect, DFC_BUTTON, DFCS_BUTTONPUSH | (bIsButtonPressed ? DFCS_PUSHED : 0));
  CRect rcContent = rect;
  if (bIsButtonPressed) rcContent.OffsetRect(1, 1);
  BOOL bDisabled = !IsWindowEnabled();
  if (m_hIcon != NULL || m_hBitmap != NULL) {
    CPoint pt(rcContent.left + (rcContent.Width() - m_sizeImage.cx) / 2,
              rcContent.top + (rcContent.Height() - m_sizeImage.cy) / 2);
    if (m_hIcon != NULL)
      pDC->DrawState(pt, m_sizeImage, m_hIcon, bDisabled ? DSS_DISABLED : DSS_NORMAL, (HBRUSH)NULL);
    else
      pDC->DrawState(pt, m_sizeImage, m_hBitmap, bDisabled ? DSS_DISABLED : DSS_NORMAL, (HBRUSH)NULL);
    return;
  }
  CString str = m_strLabel.IsEmpty() ? CString(_T("...")) : m_strLabel;
  CFont* pOld = pDC->SelectObject(GetFont());
  int nOldMode = pDC->SetBkMode(TRANSPARENT);
  COLORREF clrOld = pDC->SetTextColor(bDisabled ? ::GetSysColor(COLOR_GRAYTEXT) : clrText);
  pDC->DrawText(str, rcContent, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
  pDC->SetTextColor(clrOld);
  pDC->SetBkMode(nOldMode);
  pDC->SelectObject(pOld);
}

LRESULT CMFCEditBrowseCtrl::OnNcHitTest(CPoint point) {
  if (m_Mode != BrowseMode_None && _InButton(point)) return HTBORDER;
  return CEdit::OnNcHitTest(point);
}

void CMFCEditBrowseCtrl::OnNcLButtonDown(UINT nHitTest, CPoint point) {
  if (m_Mode != BrowseMode_None && _InButton(point)) {
    if (GetFocus() != this) SetFocus();
    m_bIsButtonPressed = TRUE;
    m_bIsButtonHighlighted = TRUE;
    m_bIsButtonCaptured = TRUE;
    SetCapture();
    RedrawWindow(NULL, NULL, RDW_FRAME | RDW_INVALIDATE | RDW_UPDATENOW);
    return;
  }
  CEdit::OnNcLButtonDown(nHitTest, point);
}

void CMFCEditBrowseCtrl::OnNcMouseMove(UINT nHitTest, CPoint point) {
  if (m_Mode != BrowseMode_None && !m_bIsButtonCaptured) {
    BOOL bIn = _InButton(point);
    if (bIn != m_bIsButtonHighlighted) {
      m_bIsButtonHighlighted = bIn;
      if (bIn) {
        TRACKMOUSEEVENT tme;
        tme.cbSize = sizeof(tme);
        tme.dwFlags = TME_LEAVE | TME_NONCLIENT;
        tme.hwndTrack = m_hWnd;
        tme.dwHoverTime = 0;
        ::TrackMouseEvent(&tme);
      }
      RedrawWindow(NULL, NULL, RDW_FRAME | RDW_INVALIDATE | RDW_UPDATENOW);
    }
  }
  CEdit::OnNcMouseMove(nHitTest, point);
}

LRESULT CMFCEditBrowseCtrl::OnNcMouseLeave(WPARAM, LPARAM) {
  if (m_bIsButtonHighlighted && !m_bIsButtonCaptured) {
    m_bIsButtonHighlighted = FALSE;
    RedrawWindow(NULL, NULL, RDW_FRAME | RDW_INVALIDATE | RDW_UPDATENOW);
  }
  return 0;
}

void CMFCEditBrowseCtrl::OnMouseMove(UINT nFlags, CPoint point) {
  if (m_bIsButtonCaptured) {
    ClientToScreen(&point);
    BOOL bIn = _InButton(point);
    if (bIn != m_bIsButtonHighlighted) {
      m_bIsButtonHighlighted = bIn;
      RedrawWindow(NULL, NULL, RDW_FRAME | RDW_INVALIDATE | RDW_UPDATENOW);
    }
    return;
  }
  CEdit::OnMouseMove(nFlags, point);
}

void CMFCEditBrowseCtrl::OnLButtonUp(UINT nFlags, CPoint point) {
  if (m_bIsButtonCaptured) {
    ClientToScreen(&point);
    BOOL bClick = m_bIsButtonPressed && _InButton(point);
    m_bIsButtonCaptured = FALSE;
    m_bIsButtonPressed = FALSE;
    m_bIsButtonHighlighted = FALSE;
    ReleaseCapture();
    RedrawWindow(NULL, NULL, RDW_FRAME | RDW_INVALIDATE | RDW_UPDATENOW);
    if (bClick) OnBrowse();
    return;
  }
  CEdit::OnLButtonUp(nFlags, point);
}

void CMFCEditBrowseCtrl::OnCancelMode() {
  CEdit::OnCancelMode();
  if (m_bIsButtonCaptured) {
    m_bIsButtonCaptured = FALSE;
    m_bIsButtonPressed = FALSE;
    m_bIsButtonHighlighted = FALSE;
    ReleaseCapture();
    RedrawWindow(NULL, NULL, RDW_FRAME | RDW_INVALIDATE | RDW_UPDATENOW);
  }
}

void CMFCEditBrowseCtrl::OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags) {
  // F4, or Alt+Down, opens the browser, as a combo box's list.
  if (m_Mode != BrowseMode_None && (nChar == VK_F4 || (nChar == VK_DOWN && ::GetKeyState(VK_MENU) < 0))) {
    OnBrowse();
    return;
  }
  CEdit::OnKeyDown(nChar, nRepCnt, nFlags);
}

BOOL CMFCEditBrowseCtrl::OnSetCursor(CWnd* pWnd, UINT nHitTest, UINT message) {
  if (m_Mode != BrowseMode_None && nHitTest == HTBORDER) {
    CPoint pt;
    ::GetCursorPos(&pt);
    if (_InButton(pt)) {
      ::SetCursor(::LoadCursor(NULL, IDC_ARROW));
      return TRUE;
    }
  }
  return CEdit::OnSetCursor(pWnd, nHitTest, message);
}

BOOL CMFCEditBrowseCtrl::OnIllegalFileName(CString& strFileName) {
  // The file dialog starts without the name.
  strFileName.Empty();
  return FALSE;
}

void CMFCEditBrowseCtrl::_SetText(const CString& str) {
  SetWindowText(str);
  SetModify(TRUE);
  OnAfterUpdate();
}

void CMFCEditBrowseCtrl::OnBrowse() {
  CString strText;
  GetWindowText(strText);
  switch (m_Mode) {
    case BrowseMode_Folder: {
      CString strFolder;
      CShellManager* pShell = afxShellManager;
      CShellManager shellTemp;
      if (pShell == NULL) pShell = &shellTemp;
      if (pShell->BrowseForFolder(strFolder, this, strText)) _SetText(strFolder);
      break;
    }
    case BrowseMode_File: {
      static const TCHAR szIllegal[] = _T("<>\"|?*");
      if (strText.FindOneOf(szIllegal) >= 0 && !OnIllegalFileName(strText)) strText.Empty();
      CFileDialog dlg(TRUE, m_strDefFileExt.IsEmpty() ? NULL : (LPCTSTR)m_strDefFileExt,
                      strText.IsEmpty() ? NULL : (LPCTSTR)strText, m_dwFileDialogFlags,
                      m_strFileFilter.IsEmpty() ? NULL : (LPCTSTR)m_strFileFilter, GetParent());
      if (dlg.DoModal() == IDOK) _SetText(dlg.GetPathName());
      break;
    }
    default:
      // BrowseMode_Default: the application's override does the browsing.
      break;
  }
  if (GetSafeHwnd() != NULL && GetFocus() != this) SetFocus();
}

// ---------------------------------------------------------------------
// CMFCMaskedEdit

IMPLEMENT_DYNAMIC(CMFCMaskedEdit, CEdit)

BEGIN_MESSAGE_MAP(CMFCMaskedEdit, CEdit)
  ON_WM_CHAR()
  ON_WM_KEYDOWN()
  ON_WM_LBUTTONUP()
  ON_WM_SETFOCUS()
  ON_MESSAGE(WM_CUT, OnCut)
  ON_MESSAGE(WM_CLEAR, OnClear)
  ON_MESSAGE(WM_PASTE, OnPaste)
  ON_MESSAGE(WM_SETTEXT, OnSetTextMsg)
  ON_MESSAGE(WM_GETTEXT, OnGetTextMsg)
  ON_MESSAGE(WM_GETTEXTLENGTH, OnGetTextLength)
END_MESSAGE_MAP()

CMFCMaskedEdit::CMFCMaskedEdit()
    : m_chMaskInputTemplate(_T('_')), m_bGetMaskedCharsOnly(TRUE), m_bSetMaskedCharsOnly(FALSE),
      m_bSelectByGroup(TRUE), m_bInternal(FALSE) {}

CMFCMaskedEdit::~CMFCMaskedEdit() {}

void CMFCMaskedEdit::EnableMask(LPCTSTR lpszMask, LPCTSTR lpszInputTemplate, TCHAR chMaskInputTemplate,
                                LPCTSTR lpszValid) {
  m_strMask = lpszMask != NULL ? lpszMask : _T("");
  m_strInputTemplate = lpszInputTemplate != NULL ? lpszInputTemplate : _T("");
  ASSERT(m_strMask.GetLength() == m_strInputTemplate.GetLength());
  m_chMaskInputTemplate = chMaskInputTemplate;
  SetValidChars(lpszValid);
  m_str = m_strInputTemplate;
  if (m_hWnd != NULL) _Show(_NextEditable(0, 1));
}

void CMFCMaskedEdit::DisableMask() {
  m_strMask.Empty();
  m_strInputTemplate.Empty();
}

void CMFCMaskedEdit::SetValidChars(LPCTSTR lpszValid) { m_strValid = lpszValid != NULL ? lpszValid : _T(""); }

BOOL CMFCMaskedEdit::IsMaskedChar(TCHAR chChar, TCHAR chMaskChar) const {
#ifdef _UNICODE
  wint_t c = (wint_t)chChar;
#else
  int c = (unsigned char)chChar;
#endif
  switch (chMaskChar) {
    case _T('D'): return _istdigit(c) != 0;
    case _T('d'): return _istdigit(c) != 0 || chChar == _T(' ');
    case _T('+'): return chChar == _T('+') || chChar == _T('-') || chChar == _T(' ');
    case _T('C'): return _istalpha(c) != 0;
    case _T('c'): return _istalpha(c) != 0 || chChar == _T(' ');
    case _T('A'): return _istalnum(c) != 0;
    case _T('a'): return _istalnum(c) != 0 || chChar == _T(' ');
    case _T('*'): return chChar >= _T(' ');
    default: return chChar == chMaskChar;
  }
}

BOOL CMFCMaskedEdit::_IsEditable(int nPos) const {
  return nPos >= 0 && nPos < m_strInputTemplate.GetLength() && m_strInputTemplate[nPos] == m_chMaskInputTemplate;
}

// The first editable position from nPos on, going by nStep; -1 for none.
int CMFCMaskedEdit::_NextEditable(int nPos, int nStep) const {
  for (int i = nPos; i >= 0 && i < m_strInputTemplate.GetLength(); i += nStep)
    if (_IsEditable(i)) return i;
  return -1;
}

CString CMFCMaskedEdit::_GetMaskedValue(const CString& strFull) const {
  CString str;
  for (int i = 0; i < strFull.GetLength() && i < m_strInputTemplate.GetLength(); i++)
    if (_IsEditable(i) && strFull[i] != m_chMaskInputTemplate) str += strFull[i];
  return str;
}

BOOL CMFCMaskedEdit::_SetValue(LPCTSTR lpszString, BOOL bMaskedOnly) {
  CString strIn = lpszString != NULL ? lpszString : _T("");
  CString str = m_strInputTemplate;
  if (bMaskedOnly) {
    int j = 0;
    for (int i = 0; i < str.GetLength() && j < strIn.GetLength(); i++) {
      if (!_IsEditable(i)) continue;
      TCHAR ch = strIn[j++];
      if (!IsMaskedChar(ch, m_strMask[i]) || (!m_strValid.IsEmpty() && m_strValid.Find(ch) < 0)) return FALSE;
      str.SetAt(i, ch);
    }
    if (j < strIn.GetLength()) return FALSE;
  } else {
    if (strIn.GetLength() != str.GetLength()) return FALSE;
    for (int i = 0; i < str.GetLength(); i++) {
      TCHAR ch = strIn[i];
      if (!_IsEditable(i)) {
        if (ch != str[i]) return FALSE;
      } else if (ch != m_chMaskInputTemplate) {
        if (!IsMaskedChar(ch, m_strMask[i]) || (!m_strValid.IsEmpty() && m_strValid.Find(ch) < 0)) return FALSE;
        str.SetAt(i, ch);
      }
    }
  }
  m_str = str;
  if (m_hWnd != NULL) _Show(_NextEditable(0, 1));
  return TRUE;
}

void CMFCMaskedEdit::_Show(int nCaret) {
  int nStart, nEnd;
  GetSel(nStart, nEnd);
  m_bInternal = TRUE;
  DefWindowProc(WM_SETTEXT, 0, (LPARAM)(LPCTSTR)m_str);
  m_bInternal = FALSE;
  if (nCaret < 0) nCaret = m_str.GetLength();
  SetSel(nCaret, nCaret);
}

void CMFCMaskedEdit::_Clear(int nStart, int nEnd) {
  for (int i = nStart; i < nEnd && i < m_str.GetLength(); i++)
    if (_IsEditable(i)) m_str.SetAt(i, m_chMaskInputTemplate);
}

void CMFCMaskedEdit::GetWindowText(CString& rString) const {
  if (m_strMask.IsEmpty()) {
    CEdit::GetWindowText(rString);
    return;
  }
  rString = m_bGetMaskedCharsOnly ? _GetMaskedValue(m_str) : m_str;
}

int CMFCMaskedEdit::GetWindowText(LPTSTR lpszStringBuf, int nMaxCount) const {
  if (m_strMask.IsEmpty()) return CEdit::GetWindowText(lpszStringBuf, nMaxCount);
  CString str;
  GetWindowText(str);
  if (nMaxCount <= 0 || lpszStringBuf == NULL) return 0;
  int n = str.GetLength() < nMaxCount - 1 ? str.GetLength() : nMaxCount - 1;
  memcpy(lpszStringBuf, (LPCTSTR)str, n * sizeof(TCHAR));
  lpszStringBuf[n] = 0;
  return n;
}

BOOL CMFCMaskedEdit::SetWindowText(LPCTSTR lpszString) {
  if (m_strMask.IsEmpty()) {
    CEdit::SetWindowText(lpszString);
    return TRUE;
  }
  return _SetValue(lpszString, m_bSetMaskedCharsOnly);
}

LRESULT CMFCMaskedEdit::OnSetTextMsg(WPARAM, LPARAM lParam) {
  if (m_bInternal || m_strMask.IsEmpty()) return Default();
  return _SetValue((LPCTSTR)lParam, m_bSetMaskedCharsOnly);
}

LRESULT CMFCMaskedEdit::OnGetTextMsg(WPARAM wParam, LPARAM lParam) {
  if (m_bInternal || m_strMask.IsEmpty()) return Default();
  CString str = m_bGetMaskedCharsOnly ? _GetMaskedValue(m_str) : m_str;
  int nMax = (int)wParam;
  LPTSTR psz = (LPTSTR)lParam;
  if (nMax <= 0 || psz == NULL) return 0;
  int n = str.GetLength() < nMax - 1 ? str.GetLength() : nMax - 1;
  memcpy(psz, (LPCTSTR)str, n * sizeof(TCHAR));
  psz[n] = 0;
  return n;
}

LRESULT CMFCMaskedEdit::OnGetTextLength(WPARAM, LPARAM) {
  if (m_bInternal || m_strMask.IsEmpty()) return Default();
  return (m_bGetMaskedCharsOnly ? _GetMaskedValue(m_str) : m_str).GetLength();
}

void CMFCMaskedEdit::OnChar(UINT nChar, UINT nRepCnt, UINT nFlags) {
  if (m_strMask.IsEmpty() || (GetStyle() & ES_READONLY)) {
    CEdit::OnChar(nChar, nRepCnt, nFlags);
    return;
  }
  int nStart, nEnd;
  GetSel(nStart, nEnd);
  TCHAR ch = (TCHAR)nChar;
  if (ch == _T('\b')) {
    if (nStart != nEnd) {
      _Clear(nStart, nEnd);
      _Show(nStart);
    } else {
      int nPos = _NextEditable(nStart - 1, -1);
      if (nPos >= 0) {
        m_str.SetAt(nPos, m_chMaskInputTemplate);
        _Show(nPos);
      }
    }
    GetParent()->SendMessage(WM_COMMAND, MAKEWPARAM(GetDlgCtrlID(), EN_CHANGE), (LPARAM)m_hWnd);
    return;
  }
  if (ch < _T(' ')) {
    // Ctrl+C, Ctrl+V, Ctrl+X: the edit control sends WM_COPY and the rest.
    CEdit::OnChar(nChar, nRepCnt, nFlags);
    return;
  }
  int nPos = _NextEditable(nStart, 1);
  if (nPos < 0 || !IsMaskedChar(ch, m_strMask[nPos]) || (!m_strValid.IsEmpty() && m_strValid.Find(ch) < 0)) {
    ::MessageBeep((UINT)-1);
    return;
  }
  if (nEnd > nStart) _Clear(nStart, nEnd);
  m_str.SetAt(nPos, ch);
  int nNext = _NextEditable(nPos + 1, 1);
  _Show(nNext >= 0 ? nNext : nPos + 1);
  GetParent()->SendMessage(WM_COMMAND, MAKEWPARAM(GetDlgCtrlID(), EN_CHANGE), (LPARAM)m_hWnd);
}

void CMFCMaskedEdit::OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags) {
  if (!m_strMask.IsEmpty() && nChar == VK_DELETE && !(GetStyle() & ES_READONLY)) {
    int nStart, nEnd;
    GetSel(nStart, nEnd);
    if (nEnd > nStart) {
      _Clear(nStart, nEnd);
    } else {
      int nPos = _NextEditable(nStart, 1);
      if (nPos >= 0) m_str.SetAt(nPos, m_chMaskInputTemplate);
    }
    _Show(nStart);
    GetParent()->SendMessage(WM_COMMAND, MAKEWPARAM(GetDlgCtrlID(), EN_CHANGE), (LPARAM)m_hWnd);
    return;
  }
  CEdit::OnKeyDown(nChar, nRepCnt, nFlags);
}

void CMFCMaskedEdit::OnLButtonUp(UINT nFlags, CPoint point) {
  CEdit::OnLButtonUp(nFlags, point);
  if (m_strMask.IsEmpty()) return;
  int nStart, nEnd;
  GetSel(nStart, nEnd);
  if (nStart == nEnd && !_IsEditable(nStart)) {
    int nPos = _NextEditable(nStart, 1);
    if (nPos >= 0) SetSel(nPos, nPos);
  }
}

void CMFCMaskedEdit::OnSetFocus(CWnd* pOldWnd) {
  CEdit::OnSetFocus(pOldWnd);
  if (m_strMask.IsEmpty()) return;
  // The caret goes to the first unfilled position.
  int nPos = -1;
  for (int i = 0; i < m_str.GetLength(); i++)
    if (_IsEditable(i) && m_str[i] == m_chMaskInputTemplate) {
      nPos = i;
      break;
    }
  if (nPos < 0) nPos = m_str.GetLength();
  SetSel(nPos, nPos);
}

LRESULT CMFCMaskedEdit::OnCut(WPARAM, LPARAM) {
  if (m_strMask.IsEmpty()) return Default();
  int nStart, nEnd;
  GetSel(nStart, nEnd);
  SendMessage(WM_COPY);
  _Clear(nStart, nEnd);
  _Show(nStart);
  return 0;
}

LRESULT CMFCMaskedEdit::OnClear(WPARAM, LPARAM) {
  if (m_strMask.IsEmpty()) return Default();
  int nStart, nEnd;
  GetSel(nStart, nEnd);
  _Clear(nStart, nEnd);
  _Show(nStart);
  return 0;
}

LRESULT CMFCMaskedEdit::OnPaste(WPARAM, LPARAM) {
  if (m_strMask.IsEmpty()) return Default();
  CString strClip;
  if (::OpenClipboard(m_hWnd)) {
#ifdef _UNICODE
    HANDLE h = ::GetClipboardData(CF_UNICODETEXT);
#else
    HANDLE h = ::GetClipboardData(CF_TEXT);
#endif
    if (h != NULL) {
      LPCTSTR p = (LPCTSTR)::GlobalLock(h);
      if (p != NULL) strClip = p;
      ::GlobalUnlock(h);
    }
    ::CloseClipboard();
  }
  int nStart, nEnd;
  GetSel(nStart, nEnd);
  if (nEnd > nStart) _Clear(nStart, nEnd);
  int nPos = nStart;
  for (int j = 0; j < strClip.GetLength(); j++) {
    int n = _NextEditable(nPos, 1);
    if (n < 0) break;
    TCHAR ch = strClip[j];
    if (!IsMaskedChar(ch, m_strMask[n]) || (!m_strValid.IsEmpty() && m_strValid.Find(ch) < 0)) break;
    m_str.SetAt(n, ch);
    nPos = n + 1;
  }
  _Show(nPos);
  return 0;
}

// ---------------------------------------------------------------------
// CMFCFontComboBox

IMPLEMENT_DYNAMIC(CMFCFontComboBox, CComboBox)

BOOL CMFCFontComboBox::m_bDrawUsingFont = FALSE;

CMFCFontComboBox::CMFCFontComboBox()
    : m_nFontType(DEVICE_FONTTYPE | RASTER_FONTTYPE | TRUETYPE_FONTTYPE), m_nCharSet(DEFAULT_CHARSET),
      m_nPitchAndFamily(DEFAULT_PITCH), m_nImageHeight(16) {}

CMFCFontComboBox::~CMFCFontComboBox() {}

BOOL CMFCFontComboBox::PreCreateWindow(CREATESTRUCT& cs) {
  // Owner drawn, with the font descriptions as the items' data.
  cs.style &= ~(CBS_OWNERDRAWVARIABLE | CBS_SORT);
  cs.style |= CBS_OWNERDRAWFIXED | CBS_HASSTRINGS;
  return CComboBox::PreCreateWindow(cs);
}

void CMFCFontComboBox::PreSubclassWindow() { CComboBox::PreSubclassWindow(); }

void CMFCFontComboBox::_Clear() {
  // DeleteItem frees each description; without the owner-draw style it
  // is not called, so free them here.
  if (m_hWnd == NULL) return;
  if (!(GetStyle() & (CBS_OWNERDRAWFIXED | CBS_OWNERDRAWVARIABLE))) {
    for (int i = 0; i < GetCount(); i++) delete (CMFCFontInfo*)GetItemDataPtr(i);
  }
  ResetContent();
}

int CALLBACK CMFCFontComboBox::_EnumProc(const LOGFONT* plf, const TEXTMETRIC*, DWORD nType, LPARAM lParam) {
  CMFCFontComboBox* pThis = (CMFCFontComboBox*)lParam;
  int nFlag = (nType & TRUETYPE_FONTTYPE)  ? TRUETYPE_FONTTYPE
              : (nType & RASTER_FONTTYPE) ? RASTER_FONTTYPE
                                          : DEVICE_FONTTYPE;
  if (!(pThis->m_nFontType & nFlag)) return 1;
  if (pThis->m_nPitchAndFamily != DEFAULT_PITCH &&
      (plf->lfPitchAndFamily & 0x0F) != (pThis->m_nPitchAndFamily & 0x0F))
    return 1;
  // The script of an ENUMLOGFONTEX follows the LOGFONT's full name.
  const ENUMLOGFONTEX* pelf = (const ENUMLOGFONTEX*)plf;
  pThis->_AddFont(plf, (int)nType, (LPCTSTR)pelf->elfScript);
  return 1;
}

void CMFCFontComboBox::_AddFont(const LOGFONT* plf, int nType, LPCTSTR lpszScript) {
  if (plf->lfFaceName[0] == _T('@')) return;  // vertical fonts
  // One entry for each face (and character set, when one was asked for).
  for (int i = 0; i < GetCount(); i++) {
    CMFCFontInfo* p = (CMFCFontInfo*)GetItemDataPtr(i);
    if (p != NULL && p->m_strName == plf->lfFaceName &&
        (m_nCharSet == DEFAULT_CHARSET || p->m_nCharSet == plf->lfCharSet))
      return;
  }
  CMFCFontInfo* pInfo = new CMFCFontInfo(plf->lfFaceName, m_nCharSet == DEFAULT_CHARSET ? _T("") : lpszScript,
                                         plf->lfCharSet, plf->lfPitchAndFamily, nType);
  // Sorted by name.
  int nIndex = 0;
  while (nIndex < GetCount()) {
    CMFCFontInfo* p = (CMFCFontInfo*)GetItemDataPtr(nIndex);
    if (p != NULL && p->m_strName.CompareNoCase(pInfo->m_strName) > 0) break;
    nIndex++;
  }
  nIndex = InsertString(nIndex, pInfo->GetFullName());
  if (nIndex < 0) {
    delete pInfo;
    return;
  }
  SetItemDataPtr(nIndex, pInfo);
}

BOOL CMFCFontComboBox::Setup(int nFontType, BYTE nCharSet, BYTE nPitchAndFamily) {
  if (m_hWnd == NULL) return FALSE;
  _Clear();
  m_nFontType = nFontType;
  m_nCharSet = nCharSet;
  m_nPitchAndFamily = nPitchAndFamily;
  CClientDC dc(this);
  LOGFONT lf;
  memset(&lf, 0, sizeof(lf));
  lf.lfCharSet = nCharSet;
  ::EnumFontFamiliesEx(dc.GetSafeHdc(), &lf, (FONTENUMPROC)_EnumProc, (LPARAM)this, 0);
  return GetCount() > 0;
}

BOOL CMFCFontComboBox::SelectFont(CMFCFontInfo* pDesc) {
  if (pDesc == NULL) return FALSE;
  return SelectFont(pDesc->m_strName, pDesc->m_nCharSet);
}

BOOL CMFCFontComboBox::SelectFont(LPCTSTR lpszName, BYTE nCharSet) {
  for (int i = 0; i < GetCount(); i++) {
    CMFCFontInfo* p = (CMFCFontInfo*)GetItemDataPtr(i);
    if (p != NULL && p->m_strName.CompareNoCase(lpszName) == 0 &&
        (nCharSet == DEFAULT_CHARSET || p->m_nCharSet == nCharSet)) {
      SetCurSel(i);
      return TRUE;
    }
  }
  return FALSE;
}

CMFCFontInfo* CMFCFontComboBox::GetSelFont() const {
  int nSel = GetCurSel();
  if (nSel < 0) return NULL;
  void* p = GetItemDataPtr(nSel);
  return p == (void*)-1 ? NULL : (CMFCFontInfo*)p;
}

void CMFCFontComboBox::MeasureItem(LPMEASUREITEMSTRUCT lpMIS) {
  CClientDC dc(this);
  CFont* pOld = dc.SelectObject(GetFont());
  TEXTMETRIC tm;
  dc.GetTextMetrics(&tm);
  dc.SelectObject(pOld);
  int cy = tm.tmHeight + 2;
  lpMIS->itemHeight = cy > m_nImageHeight ? cy : m_nImageHeight;
}

void CMFCFontComboBox::DrawItem(LPDRAWITEMSTRUCT lpDIS) {
  CDC* pDC = CDC::FromHandle(lpDIS->hDC);
  CRect rect = lpDIS->rcItem;
  BOOL bSelected = (lpDIS->itemState & ODS_SELECTED) != 0;
  pDC->FillSolidRect(rect, ::GetSysColor(bSelected ? COLOR_HIGHLIGHT : COLOR_WINDOW));
  if ((int)lpDIS->itemID >= 0) {
    CMFCFontInfo* pInfo = (CMFCFontInfo*)lpDIS->itemData;
    // The type's mark: a small "T" glyph box for TrueType fonts.
    CRect rcImage(rect.left + 2, rect.top + 1, rect.left + 2 + m_nImageHeight - 2, rect.bottom - 1);
    if (pInfo != NULL && (pInfo->m_nType & TRUETYPE_FONTTYPE)) {
      pDC->SetBkMode(TRANSPARENT);
      pDC->SetTextColor(::GetSysColor(bSelected ? COLOR_HIGHLIGHTTEXT : COLOR_WINDOWTEXT));
      CFont* pOld = pDC->SelectObject(GetFont());
      pDC->DrawText(_T("T"), rcImage, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
      pDC->SelectObject(pOld);
    }
    CRect rcText = rect;
    rcText.left = rcImage.right + 4;
    CFont fontItem;
    CFont* pFont = GetFont();
    if (m_bDrawUsingFont && pInfo != NULL) {
      LOGFONT lf;
      memset(&lf, 0, sizeof(lf));
      lf.lfHeight = -(rect.Height() - 4);
      lf.lfCharSet = pInfo->m_nCharSet;
      lstrcpyn(lf.lfFaceName, pInfo->m_strName, LF_FACESIZE);
      if (fontItem.CreateFontIndirect(&lf)) pFont = &fontItem;
    }
    CFont* pOld = pDC->SelectObject(pFont);
    pDC->SetBkMode(TRANSPARENT);
    pDC->SetTextColor(::GetSysColor(bSelected ? COLOR_HIGHLIGHTTEXT : COLOR_WINDOWTEXT));
    CString str = pInfo != NULL ? pInfo->GetFullName() : CString();
    pDC->DrawText(str, rcText, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    pDC->SelectObject(pOld);
  }
  if (lpDIS->itemState & ODS_FOCUS) pDC->DrawFocusRect(rect);
}

int CMFCFontComboBox::CompareItem(LPCOMPAREITEMSTRUCT lpCIS) {
  CMFCFontInfo* p1 = (CMFCFontInfo*)lpCIS->itemData1;
  CMFCFontInfo* p2 = (CMFCFontInfo*)lpCIS->itemData2;
  if (p1 == NULL || p2 == NULL) return 0;
  return p1->m_strName.CompareNoCase(p2->m_strName);
}

void CMFCFontComboBox::DeleteItem(LPDELETEITEMSTRUCT lpDIS) {
  CMFCFontInfo* p = (CMFCFontInfo*)lpDIS->itemData;
  if (p != NULL && p != (CMFCFontInfo*)-1) delete p;
}

// ---------------------------------------------------------------------
// CMFCSpinButtonCtrl

IMPLEMENT_DYNAMIC(CMFCSpinButtonCtrl, CSpinButtonCtrl)

BEGIN_MESSAGE_MAP(CMFCSpinButtonCtrl, CSpinButtonCtrl)
  ON_WM_PAINT()
  ON_WM_LBUTTONDOWN()
  ON_WM_LBUTTONUP()
  ON_WM_MOUSEMOVE()
  ON_MESSAGE(WM_MOUSELEAVE, OnMouseLeave)
  ON_WM_CANCELMODE()
END_MESSAGE_MAP()

CMFCSpinButtonCtrl::CMFCSpinButtonCtrl()
    : m_bIsButtonPressedUp(FALSE), m_bIsButtonPressedDown(FALSE), m_bIsButtonHighligtedUp(FALSE),
      m_bIsButtonHighligtedDown(FALSE), m_bTracked(FALSE) {}

CMFCSpinButtonCtrl::~CMFCSpinButtonCtrl() {}

void CMFCSpinButtonCtrl::OnPaint() {
  CPaintDC dc(this);
  OnDraw(&dc);
}

void CMFCSpinButtonCtrl::OnDraw(CDC* pDC) {
  CRect rect;
  GetClientRect(rect);
  int nState = 0;
  if (m_bIsButtonPressedUp) nState |= AFX_SPIN_PRESSEDUP;
  if (m_bIsButtonPressedDown) nState |= AFX_SPIN_PRESSEDDOWN;
  if (m_bIsButtonHighligtedUp) nState |= AFX_SPIN_HIGHLIGHTEDUP;
  if (m_bIsButtonHighligtedDown) nState |= AFX_SPIN_HIGHLIGHTEDDOWN;
  if (!IsWindowEnabled()) nState |= AFX_SPIN_DISABLED;
  CMFCVisualManager::GetInstance()->OnDrawSpinButtons(pDC, rect, nState, (GetStyle() & UDS_HORZ) != 0, this);
}

// Which half the point is in: 1 the up (or left), 2 the down (right).
static int AfxSpinPart(CWnd* pWnd, CPoint point) {
  CRect rect;
  pWnd->GetClientRect(rect);
  if (!rect.PtInRect(point)) return 0;
  if (pWnd->GetStyle() & UDS_HORZ) return point.x < rect.left + rect.Width() / 2 ? 1 : 2;
  return point.y < rect.top + rect.Height() / 2 ? 1 : 2;
}

void CMFCSpinButtonCtrl::OnLButtonDown(UINT nFlags, CPoint point) {
  int nPart = AfxSpinPart(this, point);
  m_bIsButtonPressedUp = nPart == 1;
  m_bIsButtonPressedDown = nPart == 2;
  CSpinButtonCtrl::OnLButtonDown(nFlags, point);
  RedrawWindow();
}

void CMFCSpinButtonCtrl::OnLButtonUp(UINT nFlags, CPoint point) {
  m_bIsButtonPressedUp = m_bIsButtonPressedDown = FALSE;
  CSpinButtonCtrl::OnLButtonUp(nFlags, point);
  RedrawWindow();
}

void CMFCSpinButtonCtrl::OnMouseMove(UINT nFlags, CPoint point) {
  int nPart = AfxSpinPart(this, point);
  BOOL bUp = nPart == 1, bDown = nPart == 2;
  if (bUp != m_bIsButtonHighligtedUp || bDown != m_bIsButtonHighligtedDown) {
    m_bIsButtonHighligtedUp = bUp;
    m_bIsButtonHighligtedDown = bDown;
    RedrawWindow();
  }
  if (nPart != 0 && !m_bTracked) {
    TRACKMOUSEEVENT tme;
    tme.cbSize = sizeof(tme);
    tme.dwFlags = TME_LEAVE;
    tme.hwndTrack = m_hWnd;
    tme.dwHoverTime = 0;
    m_bTracked = ::TrackMouseEvent(&tme);
  }
  CSpinButtonCtrl::OnMouseMove(nFlags, point);
}

LRESULT CMFCSpinButtonCtrl::OnMouseLeave(WPARAM, LPARAM) {
  m_bTracked = FALSE;
  if (m_bIsButtonHighligtedUp || m_bIsButtonHighligtedDown) {
    m_bIsButtonHighligtedUp = m_bIsButtonHighligtedDown = FALSE;
    RedrawWindow();
  }
  return 0;
}

void CMFCSpinButtonCtrl::OnCancelMode() {
  CSpinButtonCtrl::OnCancelMode();
  m_bIsButtonPressedUp = m_bIsButtonPressedDown = FALSE;
  RedrawWindow();
}
