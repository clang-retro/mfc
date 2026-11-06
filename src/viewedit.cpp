// viewedit.cpp - CEditView, a view that is a multi-line edit control.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
#include "afximpl.h"
#include <afxext.h>

IMPLEMENT_DYNCREATE(CEditView, CCtrlView)

const DWORD CEditView::dwStyleDefault = AFX_WS_DEFAULT_VIEW | WS_HSCROLL | WS_VSCROLL |
                                        ES_AUTOHSCROLL | ES_AUTOVSCROLL | ES_MULTILINE |
                                        ES_NOHIDESEL;

static UINT _afxMsgFindReplace = ::RegisterWindowMessage(FINDMSGSTRING);

// The find and replace state is shared by all edit views, so Repeat
// works across windows.
struct AFX_EDIT_STATE {
  CFindReplaceDialog* pFindReplaceDlg;
  BOOL bFindOnly;
  CString strFind;
  CString strReplace;
  BOOL bCase;
  BOOL bNext;
};
static AFX_EDIT_STATE& _AfxEditState() {
  static AFX_EDIT_STATE state = {NULL, TRUE, CString(), CString(), FALSE, TRUE};
  return state;
}

BEGIN_MESSAGE_MAP(CEditView, CCtrlView)
  ON_WM_CREATE()
  ON_WM_DESTROY()
  ON_MESSAGE(WM_SETFONT, OnSetFont)
  ON_CONTROL_REFLECT_EX(EN_CHANGE, OnEditChange)
  ON_UPDATE_COMMAND_UI(ID_EDIT_CUT, OnUpdateNeedSel)
  ON_UPDATE_COMMAND_UI(ID_EDIT_COPY, OnUpdateNeedSel)
  ON_UPDATE_COMMAND_UI(ID_EDIT_PASTE, OnUpdateNeedClip)
  ON_UPDATE_COMMAND_UI(ID_EDIT_CLEAR, OnUpdateNeedSel)
  ON_UPDATE_COMMAND_UI(ID_EDIT_UNDO, OnUpdateEditUndo)
  ON_UPDATE_COMMAND_UI(ID_EDIT_SELECT_ALL, OnUpdateNeedText)
  ON_UPDATE_COMMAND_UI(ID_EDIT_FIND, OnUpdateNeedText)
  ON_UPDATE_COMMAND_UI(ID_EDIT_REPLACE, OnUpdateNeedText)
  ON_UPDATE_COMMAND_UI(ID_EDIT_REPEAT, OnUpdateNeedFind)
  ON_COMMAND(ID_EDIT_CUT, OnEditCut)
  ON_COMMAND(ID_EDIT_COPY, OnEditCopy)
  ON_COMMAND(ID_EDIT_PASTE, OnEditPaste)
  ON_COMMAND(ID_EDIT_CLEAR, OnEditClear)
  ON_COMMAND(ID_EDIT_UNDO, OnEditUndo)
  ON_COMMAND(ID_EDIT_SELECT_ALL, OnEditSelectAll)
  ON_COMMAND(ID_EDIT_FIND, OnEditFind)
  ON_COMMAND(ID_EDIT_REPLACE, OnEditReplace)
  ON_COMMAND(ID_EDIT_REPEAT, OnEditRepeat)
  ON_REGISTERED_MESSAGE(_afxMsgFindReplace, OnFindReplaceCmd)
END_MESSAGE_MAP()

CEditView::CEditView() : CCtrlView(_T("EDIT"), dwStyleDefault) {
  m_nTabStops = 8 * 4;  // dialog units: eight characters
  m_pShadowBuffer = NULL;
  m_nShadowSize = 0;
  m_hPrinterFont = NULL;
  m_hMirrorFont = NULL;
  m_sizeChar = CSize(0, 0);
}

CEditView::~CEditView() { delete[] m_pShadowBuffer; }

BOOL CEditView::PreCreateWindow(CREATESTRUCT& cs) {
  if (!CCtrlView::PreCreateWindow(cs)) return FALSE;
  // Word-wrapping views have no horizontal scrolling.
  if ((cs.style & (WS_HSCROLL | ES_AUTOHSCROLL)) == 0) cs.style &= ~WS_HSCROLL;
  return TRUE;
}

int CEditView::OnCreate(LPCREATESTRUCT lpcs) {
  if (CCtrlView::OnCreate(lpcs) == -1) return -1;
  GetEditCtrl().LimitText(0);  // as much as the system allows
  SetTabStops(m_nTabStops);
  return 0;
}

void CEditView::OnDestroy() {
  AFX_EDIT_STATE& state = _AfxEditState();
  if (state.pFindReplaceDlg != NULL && state.pFindReplaceDlg->m_fr.hwndOwner == m_hWnd) {
    state.pFindReplaceDlg->SendMessage(WM_CLOSE);
    state.pFindReplaceDlg = NULL;
  }
  CCtrlView::OnDestroy();
}

LRESULT CEditView::OnSetFont(WPARAM wParam, LPARAM lParam) {
  LRESULT lResult = Default();
  (void)wParam;
  (void)lParam;
  SetTabStops(m_nTabStops);  // tab stops scale with the font
  return lResult;
}

void CEditView::CalcWindowRect(LPRECT lpClientRect, UINT nAdjustType) {
  CCtrlView::CalcWindowRect(lpClientRect, nAdjustType);
}

BOOL CEditView::OnEditChange() {
  CDocument* pDoc = GetDocument();
  if (pDoc != NULL) pDoc->SetModifiedFlag();
  return FALSE;  // the parent may want the notification too
}

// ---------------------------------------------------------------------
// The text

LPCTSTR CEditView::LockBuffer() const {
  // A copy of the control's text; valid until the next call.
  UINT nLen = (UINT)::GetWindowTextLength(m_hWnd);
  if (m_pShadowBuffer == NULL || m_nShadowSize < nLen + 1) {
    delete[] m_pShadowBuffer;
    m_nShadowSize = nLen + 1;
    m_pShadowBuffer = new TCHAR[m_nShadowSize];
  }
  ::GetWindowText(m_hWnd, m_pShadowBuffer, (int)m_nShadowSize);
  m_pShadowBuffer[nLen] = 0;
  return m_pShadowBuffer;
}

void CEditView::UnlockBuffer() const {}

UINT CEditView::GetBufferLength() const { return (UINT)::GetWindowTextLength(m_hWnd); }

void CEditView::GetSelectedText(CString& strResult) const {
  int nStart, nEnd;
  GetEditCtrl().GetSel(nStart, nEnd);
  LPCTSTR lpsz = LockBuffer();
  int nLen = lstrlen(lpsz);
  if (nEnd > nLen) nEnd = nLen;
  if (nStart > nEnd) nStart = nEnd;
  strResult = CString(lpsz + nStart, nEnd - nStart);
  UnlockBuffer();
}

void CEditView::SetTabStops(int nTabStops) {
  m_nTabStops = nTabStops;
  if (m_hWnd != NULL) {
    SendMessage(EM_SETTABSTOPS, 1, (LPARAM)&m_nTabStops);
    Invalidate();
  }
}

CFont* CEditView::GetPrinterFont() const { return CFont::FromHandle(m_hPrinterFont); }

void CEditView::SetPrinterFont(CFont* pFont) { m_hPrinterFont = (HFONT)pFont->GetSafeHandle(); }

// The raw form: the text itself, as in a plain text file. As in MFC, a
// Unicode build reads and writes UTF-16 text.
void CEditView::ReadFromArchive(CArchive& ar, UINT nBytes) {
  UINT nChars = nBytes / sizeof(TCHAR);
  LPTSTR pBuf = new TCHAR[nChars + 1];
  UINT nRead = ar.Read(pBuf, nChars * sizeof(TCHAR));
  nChars = nRead / sizeof(TCHAR);
  pBuf[nChars] = 0;
  SetWindowText(pBuf);
  delete[] pBuf;
  GetEditCtrl().SetModify(FALSE);
}

void CEditView::WriteToArchive(CArchive& ar) {
  LPCTSTR lpsz = LockBuffer();
  ar.Write(lpsz, lstrlen(lpsz) * sizeof(TCHAR));
  UnlockBuffer();
}

void CEditView::SerializeRaw(CArchive& ar) {
  if (ar.IsStoring()) {
    WriteToArchive(ar);
  } else {
    CFile* pFile = ar.GetFile();
    ULONGLONG nLength = pFile->GetLength();
    ULONGLONG nPos = pFile->GetPosition();
    ULONGLONG nBytes = nLength > nPos ? nLength - nPos : 0;
    if (nBytes > 0x7FFFFFFF) AfxThrowArchiveException(CArchiveException::badIndex);
    ReadFromArchive(ar, (UINT)nBytes);
  }
}

void CEditView::Serialize(CArchive& ar) {
  // The object form: a length in characters, then the text.
  if (ar.IsStoring()) {
    ar << (DWORD)GetBufferLength();
    WriteToArchive(ar);
  } else {
    DWORD dwLen;
    ar >> dwLen;
    ReadFromArchive(ar, (UINT)(dwLen * sizeof(TCHAR)));
  }
}

// ---------------------------------------------------------------------
// Finding text

static BOOL _AfxMatchAt(LPCTSTR lpszText, LPCTSTR lpszFind, int nFind, BOOL bCase) {
  return (bCase ? ::CompareString(LOCALE_USER_DEFAULT, 0, lpszText, nFind, lpszFind, nFind)
                : ::CompareString(LOCALE_USER_DEFAULT, NORM_IGNORECASE, lpszText, nFind, lpszFind,
                                  nFind)) == CSTR_EQUAL;
}

BOOL CEditView::SameAsSelected(LPCTSTR lpszCompare, BOOL bCase) {
  CString strSelect;
  GetSelectedText(strSelect);
  int nLen = lstrlen(lpszCompare);
  return strSelect.GetLength() == nLen && _AfxMatchAt(strSelect, lpszCompare, nLen, bCase);
}

// Searches from the selection; wraps around the end when bWrap is set.
static int _AfxFindIn(LPCTSTR lpszText, int nTextLen, LPCTSTR lpszFind, int nFind, int nStart,
                      BOOL bNext, BOOL bCase, BOOL bWrap) {
  if (nFind == 0 || nFind > nTextLen) return -1;
  int nLast = nTextLen - nFind;
  if (bNext) {
    for (int i = nStart; i <= nLast; i++)
      if (_AfxMatchAt(lpszText + i, lpszFind, nFind, bCase)) return i;
    if (bWrap)
      for (int i = 0; i < nStart && i <= nLast; i++)
        if (_AfxMatchAt(lpszText + i, lpszFind, nFind, bCase)) return i;
  } else {
    for (int i = min(nStart, nLast); i >= 0; i--)
      if (_AfxMatchAt(lpszText + i, lpszFind, nFind, bCase)) return i;
    if (bWrap)
      for (int i = nLast; i > nStart; i--)
        if (_AfxMatchAt(lpszText + i, lpszFind, nFind, bCase)) return i;
  }
  return -1;
}

BOOL CEditView::FindText(LPCTSTR lpszFind, BOOL bNext, BOOL bCase) {
  int nStartSel, nEndSel;
  GetEditCtrl().GetSel(nStartSel, nEndSel);
  LPCTSTR lpszText = LockBuffer();
  int nTextLen = lstrlen(lpszText);
  int nFind = lstrlen(lpszFind);
  int nStart = bNext ? nEndSel : nStartSel - 1;
  int nFound = _AfxFindIn(lpszText, nTextLen, lpszFind, nFind, nStart, bNext, bCase, TRUE);
  UnlockBuffer();
  if (nFound < 0) return FALSE;
  GetEditCtrl().SetSel(nFound, nFound + nFind);
  return TRUE;
}

void CEditView::OnFindNext(LPCTSTR lpszFind, BOOL bNext, BOOL bCase) {
  AFX_EDIT_STATE& state = _AfxEditState();
  state.strFind = lpszFind;
  state.bCase = bCase;
  state.bNext = bNext;
  if (!FindText(state.strFind, bNext, bCase)) OnTextNotFound(state.strFind);
}

void CEditView::OnReplaceSel(LPCTSTR lpszFind, BOOL bNext, BOOL bCase, LPCTSTR lpszReplace) {
  AFX_EDIT_STATE& state = _AfxEditState();
  state.strFind = lpszFind;
  state.strReplace = lpszReplace;
  state.bCase = bCase;
  state.bNext = bNext;
  if (!SameAsSelected(state.strFind, bCase)) {
    if (!FindText(state.strFind, bNext, bCase)) OnTextNotFound(state.strFind);
    return;
  }
  GetEditCtrl().ReplaceSel(state.strReplace, TRUE);
  if (!FindText(state.strFind, bNext, bCase)) OnTextNotFound(state.strFind);
}

void CEditView::OnReplaceAll(LPCTSTR lpszFind, LPCTSTR lpszReplace, BOOL bCase) {
  AFX_EDIT_STATE& state = _AfxEditState();
  state.strFind = lpszFind;
  state.strReplace = lpszReplace;
  state.bCase = bCase;
  state.bNext = TRUE;

  // One pass from the top, without wrapping, so a replacement that
  // contains the search text is not replaced again.
  CWaitCursor wait;
  int nFind = state.strFind.GetLength();
  int nPos = 0;
  BOOL bFound = FALSE;
  for (;;) {
    LPCTSTR lpszText = LockBuffer();
    int nFound = _AfxFindIn(lpszText, lstrlen(lpszText), state.strFind, nFind, nPos, TRUE, bCase,
                            FALSE);
    UnlockBuffer();
    if (nFound < 0) break;
    bFound = TRUE;
    GetEditCtrl().SetSel(nFound, nFound + nFind);
    GetEditCtrl().ReplaceSel(state.strReplace, TRUE);
    nPos = nFound + state.strReplace.GetLength();
  }
  if (!bFound) OnTextNotFound(state.strFind);
}

void CEditView::OnTextNotFound(LPCTSTR) { ::MessageBeep(MB_ICONHAND); }

void CEditView::OnEditFindReplace(BOOL bFindOnly) {
  AFX_EDIT_STATE& state = _AfxEditState();
  if (state.pFindReplaceDlg != NULL) {
    if (state.bFindOnly == bFindOnly) {
      state.pFindReplaceDlg->SetActiveWindow();
      state.pFindReplaceDlg->ShowWindow(SW_SHOW);
      return;
    }
    state.pFindReplaceDlg->SendMessage(WM_CLOSE);
    state.pFindReplaceDlg = NULL;
  }

  // Start with the selection when it is a single line.
  CString strFind;
  GetSelectedText(strFind);
  if (strFind.IsEmpty() || strFind.FindOneOf(_T("\r\n")) >= 0) strFind = state.strFind;

  CFindReplaceDialog* pDlg = new CFindReplaceDialog;
  DWORD dwFlags = (state.bNext ? FR_DOWN : 0) | (state.bCase ? FR_MATCHCASE : 0);
  if (!pDlg->Create(bFindOnly, strFind, state.strReplace, dwFlags, this)) {
    delete pDlg;
    return;
  }
  state.pFindReplaceDlg = pDlg;
  state.bFindOnly = bFindOnly;
  pDlg->SetActiveWindow();
  pDlg->ShowWindow(SW_SHOW);
}

LRESULT CEditView::OnFindReplaceCmd(WPARAM, LPARAM lParam) {
  AFX_EDIT_STATE& state = _AfxEditState();
  CFindReplaceDialog* pDlg = CFindReplaceDialog::GetNotifier(lParam);
  if (pDlg == NULL) return 0;
  if (pDlg->IsTerminating()) {
    state.pFindReplaceDlg = NULL;
  } else if (pDlg->FindNext()) {
    OnFindNext(pDlg->GetFindString(), pDlg->SearchDown(), pDlg->MatchCase());
  } else if (pDlg->ReplaceCurrent()) {
    OnReplaceSel(pDlg->GetFindString(), pDlg->SearchDown(), pDlg->MatchCase(),
                 pDlg->GetReplaceString());
  } else if (pDlg->ReplaceAll()) {
    OnReplaceAll(pDlg->GetFindString(), pDlg->GetReplaceString(), pDlg->MatchCase());
  }
  return 0;
}

void CEditView::OnEditFind() { OnEditFindReplace(TRUE); }

void CEditView::OnEditReplace() { OnEditFindReplace(FALSE); }

void CEditView::OnEditRepeat() {
  AFX_EDIT_STATE& state = _AfxEditState();
  if (state.strFind.IsEmpty()) {
    OnEditFindReplace(TRUE);
    return;
  }
  if (!FindText(state.strFind, state.bNext, state.bCase)) OnTextNotFound(state.strFind);
}

// ---------------------------------------------------------------------
// Edit commands

void CEditView::OnUpdateNeedSel(CCmdUI* pCmdUI) {
  int nStart, nEnd;
  GetEditCtrl().GetSel(nStart, nEnd);
  pCmdUI->Enable(nStart != nEnd);
}

void CEditView::OnUpdateNeedClip(CCmdUI* pCmdUI) {
  pCmdUI->Enable(::IsClipboardFormatAvailable(CF_TEXT));
}

void CEditView::OnUpdateNeedText(CCmdUI* pCmdUI) { pCmdUI->Enable(GetWindowTextLength() != 0); }

void CEditView::OnUpdateNeedFind(CCmdUI* pCmdUI) {
  pCmdUI->Enable(GetWindowTextLength() != 0 && !_AfxEditState().strFind.IsEmpty());
}

void CEditView::OnUpdateEditUndo(CCmdUI* pCmdUI) { pCmdUI->Enable(GetEditCtrl().CanUndo()); }

void CEditView::OnEditCut() { GetEditCtrl().Cut(); }
void CEditView::OnEditCopy() { GetEditCtrl().Copy(); }
void CEditView::OnEditPaste() { GetEditCtrl().Paste(); }
void CEditView::OnEditClear() { GetEditCtrl().Clear(); }
void CEditView::OnEditUndo() { GetEditCtrl().Undo(); }
void CEditView::OnEditSelectAll() { GetEditCtrl().SetSel(0, -1); }

// ---------------------------------------------------------------------
// Printing: the text is laid out a page at a time with word wrapping.

BOOL CEditView::OnPreparePrinting(CPrintInfo* pInfo) { return DoPreparePrinting(pInfo); }

void CEditView::OnBeginPrinting(CDC* pDC, CPrintInfo* pInfo) {
  (void)pInfo;
  m_aPageStart.RemoveAll();
  m_aPageStart.Add(0);

  // Use the printer font if one was set; otherwise the screen font, scaled
  // to the printer's resolution.
  if (m_hPrinterFont == NULL) {
    HFONT hFont = (HFONT)SendMessage(WM_GETFONT);
    if (hFont == NULL) hFont = (HFONT)::GetStockObject(SYSTEM_FONT);
    LOGFONT lf;
    ::GetObject(hFont, sizeof(lf), &lf);
    CClientDC dcScreen(NULL);
    lf.lfHeight = ::MulDiv(lf.lfHeight, pDC->GetDeviceCaps(LOGPIXELSY),
                           dcScreen.GetDeviceCaps(LOGPIXELSY));
    lf.lfWidth = ::MulDiv(lf.lfWidth, pDC->GetDeviceCaps(LOGPIXELSX),
                          dcScreen.GetDeviceCaps(LOGPIXELSX));
    m_hMirrorFont = ::CreateFontIndirect(&lf);
  }
  HGDIOBJ hOld = pDC->SelectObject(m_hPrinterFont != NULL ? m_hPrinterFont : m_hMirrorFont);
  TEXTMETRIC tm;
  pDC->GetTextMetrics(&tm);
  m_sizeChar = CSize(tm.tmAveCharWidth, tm.tmHeight + tm.tmExternalLeading);
  pDC->SelectObject(hOld);
}

void CEditView::OnPrepareDC(CDC* pDC, CPrintInfo* pInfo) {
  CCtrlView::OnPrepareDC(pDC, pInfo);
  if (pInfo == NULL || !pDC->IsPrinting()) return;

  // Lay out pages up to the one being printed.
  UINT nTextLen = GetBufferLength();
  CRect rect(0, 0, pDC->GetDeviceCaps(HORZRES), pDC->GetDeviceCaps(VERTRES));
  HGDIOBJ hOld = pDC->SelectObject(m_hPrinterFont != NULL ? m_hPrinterFont : m_hMirrorFont);
  while ((UINT)m_aPageStart.GetSize() < pInfo->m_nCurPage) {
    UINT nStart = m_aPageStart[m_aPageStart.GetSize() - 1];
    if (nStart >= nTextLen) break;
    CRect rectLayout = rect;
    // Measure without drawing: clip everything away.
    int nSaved = pDC->SaveDC();
    pDC->IntersectClipRect(0, 0, 0, 0);
    UINT nNext = PrintInsideRect(pDC, rectLayout, nStart, nTextLen);
    pDC->RestoreDC(nSaved);
    m_aPageStart.Add(nNext);
  }
  pDC->SelectObject(hOld);
  pInfo->m_bContinuePrinting =
      (UINT)m_aPageStart.GetSize() >= pInfo->m_nCurPage &&
      m_aPageStart[pInfo->m_nCurPage - 1] < nTextLen;
  if (nTextLen == 0) pInfo->m_bContinuePrinting = pInfo->m_nCurPage == 1;
}

UINT CEditView::PrintInsideRect(CDC* pDC, RECT& rectLayout, UINT nIndexStart, UINT nIndexStop) {
  LPCTSTR lpszText = LockBuffer();
  UINT nTextLen = (UINT)lstrlen(lpszText);
  if (nIndexStop > nTextLen) nIndexStop = nTextLen;
  UINT nIndex = nIndexStart;
  int y = rectLayout.top;
  int cxWidth = rectLayout.right - rectLayout.left;
  UINT nFormat = DT_WORDBREAK | DT_EXPANDTABS | DT_NOPREFIX | DT_LEFT;

  while (nIndex < nIndexStop) {
    // One logical line, up to the line break.
    UINT nEnd = nIndex;
    while (nEnd < nIndexStop && lpszText[nEnd] != '\r' && lpszText[nEnd] != '\n') nEnd++;
    UINT nNext = nEnd;
    if (nNext < nIndexStop && lpszText[nNext] == '\r') nNext++;
    if (nNext < nIndexStop && lpszText[nNext] == '\n') nNext++;

    CRect rectLine(rectLayout.left, y, rectLayout.left + cxWidth, y);
    int nLen = (int)(nEnd - nIndex);
    if (nLen > 0)
      pDC->DrawText(lpszText + nIndex, nLen, &rectLine, nFormat | DT_CALCRECT);
    int cy = max((int)rectLine.Height(), (int)m_sizeChar.cy);
    if (y + cy > rectLayout.bottom && y != rectLayout.top) break;  // the next page
    rectLine.SetRect(rectLayout.left, y, rectLayout.right, y + cy);
    if (nLen > 0) pDC->DrawText(lpszText + nIndex, nLen, &rectLine, nFormat);
    y += cy;
    nIndex = nNext;
  }
  UnlockBuffer();
  rectLayout.bottom = y;
  return nIndex;
}

void CEditView::OnPrint(CDC* pDC, CPrintInfo* pInfo) {
  if ((UINT)m_aPageStart.GetSize() < pInfo->m_nCurPage) return;
  HGDIOBJ hOld = pDC->SelectObject(m_hPrinterFont != NULL ? m_hPrinterFont : m_hMirrorFont);
  CRect rect = pInfo->m_rectDraw;
  UINT nNext = PrintInsideRect(pDC, rect, m_aPageStart[pInfo->m_nCurPage - 1], GetBufferLength());
  if ((UINT)m_aPageStart.GetSize() == pInfo->m_nCurPage) m_aPageStart.Add(nNext);
  pDC->SelectObject(hOld);
}

void CEditView::OnEndPrinting(CDC* pDC, CPrintInfo* pInfo) {
  (void)pDC;
  (void)pInfo;
  if (m_hMirrorFont != NULL) {
    ::DeleteObject(m_hMirrorFont);
    m_hMirrorFont = NULL;
  }
  m_aPageStart.RemoveAll();
}
