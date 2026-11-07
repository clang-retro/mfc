// viewrich.cpp - CRichEditView and CRichEditDoc: a document whose text
// lives in a rich edit control, saved as rich text or plain text.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
#include "afximpl.h"
#include <afxrich.h>
#include <afxodlgs.h>

IMPLEMENT_DYNCREATE(CRichEditView, CCtrlView)
IMPLEMENT_DYNAMIC(CRichEditDoc, COleDocument)
IMPLEMENT_SERIAL(CRichEditCntrItem, COleClientItem, 0)

BEGIN_INTERFACE_MAP(CRichEditView, CCtrlView)
  INTERFACE_PART(CRichEditView, IID_IRichEditOleCallback, RichEditOleCallback)
END_INTERFACE_MAP()

const DWORD CRichEditView::dwStyleDefault = AFX_WS_DEFAULT_VIEW | WS_HSCROLL | WS_VSCROLL |
                                            ES_AUTOHSCROLL | ES_AUTOVSCROLL | ES_MULTILINE |
                                            ES_NOHIDESEL;

static UINT _afxMsgRichFindReplace = ::RegisterWindowMessage(FINDMSGSTRING);

// The find and replace state, shared by the rich edit views so Repeat
// works across windows.
struct AFX_RICHEDIT_STATE {
  CFindReplaceDialog* pFindReplaceDlg;
  BOOL bFindOnly;
  CString strFind;
  CString strReplace;
  BOOL bCase;
  BOOL bWord;
  BOOL bNext;
};
static AFX_RICHEDIT_STATE& _AfxRichEditState() {
  static AFX_RICHEDIT_STATE state = {NULL, TRUE, CString(), CString(), FALSE, FALSE, TRUE};
  return state;
}

BEGIN_MESSAGE_MAP(CRichEditView, CCtrlView)
  ON_WM_CREATE()
  ON_WM_DESTROY()
  ON_COMMAND(ID_OLE_INSERT_NEW, OnInsertObject)
  ON_COMMAND(ID_EDIT_PASTE_SPECIAL, OnEditPasteSpecial)
  ON_UPDATE_COMMAND_UI(ID_EDIT_PASTE_SPECIAL, OnUpdateEditPasteSpecial)
  ON_WM_SIZE()
  ON_NOTIFY_REFLECT(EN_SELCHANGE, OnSelChange)
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
  ON_COMMAND(ID_FORMAT_FONT, OnFormatFont)
  ON_COMMAND(ID_FILE_PRINT, CView::OnFilePrint)
  ON_COMMAND(ID_FILE_PRINT_DIRECT, CView::OnFilePrint)
  ON_COMMAND(ID_FILE_PRINT_PREVIEW, CView::OnFilePrintPreview)
  ON_REGISTERED_MESSAGE(_afxMsgRichFindReplace, OnFindReplaceCmd)
END_MESSAGE_MAP()

CRichEditView::CRichEditView() : CCtrlView(RICHEDIT_CLASS, dwStyleDefault) {
  m_nWordWrap = WrapToWindow;
  m_nBulletIndent = 1440 / 4;
  m_lpRichEditOle = NULL;
  // US Letter with the usual margins, in twips, until a printer says
  // otherwise.
  m_sizePaper = CSize(8 * 1440 + 720, 11 * 1440);
  m_rectMargin = CRect(1800, 1440, 1800, 1440);
  m_bSyncCharFormat = m_bSyncParaFormat = TRUE;
  memset(&m_charformat, 0, sizeof(m_charformat));
  m_charformat.cbSize = sizeof(m_charformat);
  memset(&m_paraformat, 0, sizeof(m_paraformat));
  m_paraformat.cbSize = sizeof(m_paraformat);
}

CRichEditView::~CRichEditView() {}

BOOL CRichEditView::PreCreateWindow(CREATESTRUCT& cs) {
  // Rich edit 2.0 when the system has it, else the 1.0 control.
  if (!AfxInitRichEdit2()) {
    if (!AfxInitRichEdit()) return FALSE;
    m_strClass = _T("RICHEDIT");
  }
  return CCtrlView::PreCreateWindow(cs);
}

int CRichEditView::OnCreate(LPCREATESTRUCT lpcs) {
  if (CCtrlView::OnCreate(lpcs) == -1) return -1;
  CRichEditCtrl& ctrl = GetRichEditCtrl();
  ctrl.LimitText(0x0FFFFFFF);
  ctrl.SetEventMask(ENM_SELCHANGE | ENM_CHANGE | ENM_SCROLL);
  ctrl.SetOptions(ECOOP_OR, ECO_AUTOWORDSELECTION);
  // Objects in the text go through the callback and IRichEditOle.
  ctrl.SendMessage(EM_SETOLECALLBACK, 0, (LPARAM)&m_xRichEditOleCallback);
  ctrl.SendMessage(EM_GETOLEINTERFACE, 0, (LPARAM)&m_lpRichEditOle);
  return 0;
}

void CRichEditView::OnDestroy() {
  AFX_RICHEDIT_STATE& state = _AfxRichEditState();
  if (state.pFindReplaceDlg != NULL && state.pFindReplaceDlg->m_fr.hwndOwner == m_hWnd) {
    state.pFindReplaceDlg->SendMessage(WM_CLOSE);
    state.pFindReplaceDlg = NULL;
  }
  // The objects go with the text; their items go too.
  CRichEditDoc* pDoc = GetDocument();
  if (pDoc != NULL && pDoc->IsKindOf(RUNTIME_CLASS(CRichEditDoc))) {
    POSITION pos = pDoc->GetStartPosition();
    while (pos != NULL) {
      CDocItem* pItem = pDoc->GetNextItem(pos);
      if (pItem->IsKindOf(RUNTIME_CLASS(CRichEditCntrItem))) delete pItem;
    }
  }
  if (m_lpRichEditOle != NULL) {
    m_lpRichEditOle->Release();
    m_lpRichEditOle = NULL;
  }
  SendMessage(EM_SETOLECALLBACK, 0, 0);
  CCtrlView::OnDestroy();
}

void CRichEditView::OnInitialUpdate() {
  CCtrlView::OnInitialUpdate();
  // The page comes from the default printer, if there is one.
  CDC dc;
  if (AfxGetApp() != NULL && AfxGetApp()->CreatePrinterDC(dc)) OnPrinterChanged(dc);
  WrapChanged();
  m_bSyncCharFormat = m_bSyncParaFormat = TRUE;
}

void CRichEditView::OnSize(UINT nType, int cx, int cy) {
  CCtrlView::OnSize(nType, cx, cy);
}

void CRichEditView::OnPrinterChanged(const CDC& dcPrinter) {
  CDC& dc = (CDC&)dcPrinter;
  HDC hDC = dc.m_hAttribDC != NULL ? dc.m_hAttribDC : dc.m_hDC;
  int cxInch = ::GetDeviceCaps(hDC, LOGPIXELSX), cyInch = ::GetDeviceCaps(hDC, LOGPIXELSY);
  int cx = ::GetDeviceCaps(hDC, PHYSICALWIDTH), cy = ::GetDeviceCaps(hDC, PHYSICALHEIGHT);
  if (cx <= 0 || cy <= 0) {  // not a printer: its whole surface
    cx = ::GetDeviceCaps(hDC, HORZRES);
    cy = ::GetDeviceCaps(hDC, VERTRES);
  }
  if (cxInch > 0 && cyInch > 0 && cx > 0 && cy > 0)
    SetPaperSize(CSize(::MulDiv(cx, 1440, cxInch), ::MulDiv(cy, 1440, cyInch)));
}

void CRichEditView::SetPaperSize(CSize sizePaper) {
  m_sizePaper = sizePaper;
  if (m_nWordWrap == WrapToTargetDevice && m_hWnd != NULL) WrapChanged();
}

CRect CRichEditView::GetPrintRect() const {
  CRect rect = GetPageRect();
  rect.left += m_rectMargin.left;
  rect.top += m_rectMargin.top;
  rect.right -= m_rectMargin.right;
  rect.bottom -= m_rectMargin.bottom;
  if (rect.right < rect.left) rect.right = rect.left;
  if (rect.bottom < rect.top) rect.bottom = rect.top;
  return rect;
}

int CRichEditView::GetPrintWidth() const { return GetPrintRect().Width(); }

// Word wrap: none, at the window's edge, or as it will print.
void CRichEditView::WrapChanged() {
  if (m_hWnd == NULL) return;
  CWaitCursor wait;
  CRichEditCtrl& ctrl = GetRichEditCtrl();
  switch (m_nWordWrap) {
    case WrapNone:
      ctrl.SetTargetDevice(NULL, 1);
      break;
    case WrapToTargetDevice: {
      CDC dc;
      if (AfxGetApp() != NULL && AfxGetApp()->CreatePrinterDC(dc)) {
        ctrl.SetTargetDevice(dc.m_hDC, GetPrintWidth());
        break;
      }
      ctrl.SetTargetDevice(NULL, 0);
      break;
    }
    default:
      ctrl.SetTargetDevice(NULL, 0);
      break;
  }
}

long CRichEditView::GetTextLength() const { return GetRichEditCtrl().GetTextLength(); }

long CRichEditView::GetTextLengthEx(DWORD dwFlags, UINT uCodePage) const {
  return GetRichEditCtrl().GetTextLengthEx(dwFlags, uCodePage);
}

// ---------------------------------------------------------------------
// Formatting

CHARFORMAT2& CRichEditView::GetCharFormatSelection() {
  if (m_bSyncCharFormat) {
    m_charformat.cbSize = sizeof(m_charformat);
    m_charformat.dwMask = CFM_ALL2;
    ::SendMessage(m_hWnd, EM_GETCHARFORMAT, SCF_SELECTION, (LPARAM)&m_charformat);
    m_bSyncCharFormat = FALSE;
  }
  return m_charformat;
}

PARAFORMAT2& CRichEditView::GetParaFormatSelection() {
  if (m_bSyncParaFormat) {
    m_paraformat.cbSize = sizeof(m_paraformat);
    m_paraformat.dwMask = PFM_ALL2;
    ::SendMessage(m_hWnd, EM_GETPARAFORMAT, 0, (LPARAM)&m_paraformat);
    m_bSyncParaFormat = FALSE;
  }
  return m_paraformat;
}

void CRichEditView::SetCharFormat(CHARFORMAT2 cf) {
  CWaitCursor wait;
  cf.cbSize = sizeof(cf);
  ::SendMessage(m_hWnd, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
  m_bSyncCharFormat = TRUE;
}

BOOL CRichEditView::SetParaFormat(PARAFORMAT2& pf) {
  CWaitCursor wait;
  pf.cbSize = sizeof(pf);
  BOOL bResult = (BOOL)::SendMessage(m_hWnd, EM_SETPARAFORMAT, 0, (LPARAM)&pf);
  m_bSyncParaFormat = TRUE;
  return bResult;
}

// Turns an effect (bold, italic, ...) on for the selection, or off when
// all of it has it already.
void CRichEditView::OnCharEffect(DWORD dwMask, DWORD dwEffect) {
  GetCharFormatSelection();
  if (m_charformat.dwMask & dwMask)
    m_charformat.dwEffects ^= dwEffect;
  else
    m_charformat.dwEffects |= dwEffect;
  m_charformat.dwMask = dwMask;
  SetCharFormat(m_charformat);
}

void CRichEditView::OnUpdateCharEffect(CCmdUI* pCmdUI, DWORD dwMask, DWORD dwEffect) {
  GetCharFormatSelection();
  // 2 (indeterminate) when the selection is mixed.
  pCmdUI->SetCheck((m_charformat.dwMask & dwMask) ? ((m_charformat.dwEffects & dwEffect) ? 1 : 0)
                                                  : 2);
}

void CRichEditView::OnParaAlign(WORD wAlign) {
  PARAFORMAT2 pf;
  memset(&pf, 0, sizeof(pf));
  pf.cbSize = sizeof(pf);
  pf.dwMask = PFM_ALIGNMENT;
  pf.wAlignment = wAlign;
  SetParaFormat(pf);
}

void CRichEditView::OnUpdateParaAlign(CCmdUI* pCmdUI, WORD wAlign) {
  GetParaFormatSelection();
  pCmdUI->SetCheck((m_paraformat.dwMask & PFM_ALIGNMENT) ? (m_paraformat.wAlignment == wAlign)
                                                         : 2);
}

void CRichEditView::OnFormatFont() {
  CFontDialog dlg(GetCharFormatSelection(), CF_BOTH | CF_NOOEMFONTS, NULL, this);
  if (dlg.DoModal() != IDOK) return;
  CHARFORMAT2 cf;
  memset(&cf, 0, sizeof(cf));
  cf.cbSize = sizeof(cf);
  dlg.GetCharFormat(cf);
  SetCharFormat(cf);
}

void CRichEditView::OnSelChange(NMHDR* pNMHDR, LRESULT* pResult) {
  (void)pNMHDR;
  m_bSyncCharFormat = m_bSyncParaFormat = TRUE;
  if (pResult != NULL) *pResult = 0;
}

BOOL CRichEditView::OnEditChange() {
  CDocument* pDoc = GetDocument();
  if (pDoc != NULL) pDoc->SetModifiedFlag();
  return FALSE;  // the parent may want the notification too
}

// ---------------------------------------------------------------------
// Reading and writing

void CRichEditView::DeleteContents() {
  if (m_hWnd == NULL) return;
  SetWindowText(_T(""));
  GetRichEditCtrl().EmptyUndoBuffer();
  m_bSyncCharFormat = m_bSyncParaFormat = TRUE;
}

// The control's stream goes to or from the archive; an exception from
// the archive waits until the control is done.
struct AFX_RICHSTREAM {
  CArchive* pArchive;
  CException* pException;
};

DWORD CALLBACK CRichEditView::EditStreamCallBack(DWORD_PTR dwCookie, LPBYTE pbBuff, LONG cb,
                                                 LONG* pcb) {
  AFX_RICHSTREAM* pStream = (AFX_RICHSTREAM*)dwCookie;
  CArchive& ar = *pStream->pArchive;
  try {
    if (ar.IsStoring()) {
      ar.Write(pbBuff, (UINT)cb);
      *pcb = cb;
    } else {
      *pcb = (LONG)ar.Read(pbBuff, (UINT)cb);
    }
  } catch (CException* e) {
    pStream->pException = e;
    *pcb = 0;
    return 1;
  }
  return 0;
}

void CRichEditView::Stream(CArchive& ar, BOOL bSelection) {
  AFX_RICHSTREAM stream = {&ar, NULL};
  EDITSTREAM es;
  es.dwCookie = (DWORD_PTR)&stream;
  es.dwError = 0;
  es.pfnCallback = EditStreamCallBack;
  CRichEditDoc* pDoc = GetDocument();
  int nFormat = (pDoc == NULL || pDoc->m_bRTF) ? SF_RTF : SF_TEXT;
  if (bSelection) nFormat |= SFF_SELECTION;
  if (ar.IsStoring()) {
    GetRichEditCtrl().StreamOut(nFormat, es);
  } else {
    GetRichEditCtrl().StreamIn(nFormat, es);
    m_bSyncCharFormat = m_bSyncParaFormat = TRUE;
    Invalidate();
  }
  if (stream.pException != NULL) throw stream.pException;
  if (es.dwError != 0) AfxThrowFileException(CFileException::genericException);
}

void CRichEditView::Serialize(CArchive& ar) { Stream(ar, FALSE); }

// ---------------------------------------------------------------------
// Finding text

BOOL CRichEditView::FindTextSimple(LPCTSTR lpszFind, BOOL bCase, BOOL bWord, BOOL bNext) {
  CRichEditCtrl& ctrl = GetRichEditCtrl();
  CHARRANGE cr;
  ctrl.GetSel(cr);
  FINDTEXTEX ft;
  ft.lpstrText = lpszFind;
  if (bNext) {
    ft.chrg.cpMin = cr.cpMax;
    ft.chrg.cpMax = -1;
  } else {
    ft.chrg.cpMin = cr.cpMin;
    ft.chrg.cpMax = 0;
  }
  DWORD dwFlags = (bCase ? FR_MATCHCASE : 0) | (bWord ? FR_WHOLEWORD : 0) | (bNext ? FR_DOWN : 0);
  if (ctrl.FindText(dwFlags, &ft) < 0) return FALSE;
  ctrl.SetSel(ft.chrgText);
  return TRUE;
}

// Like FindTextSimple, but wraps around the end (or the start).
BOOL CRichEditView::FindText(LPCTSTR lpszFind, BOOL bCase, BOOL bWord, BOOL bNext) {
  CWaitCursor wait;
  if (FindTextSimple(lpszFind, bCase, bWord, bNext)) return TRUE;
  CRichEditCtrl& ctrl = GetRichEditCtrl();
  CHARRANGE crOld;
  ctrl.GetSel(crOld);
  long nEnd = GetTextLength();
  ctrl.SetSel(bNext ? 0 : nEnd, bNext ? 0 : nEnd);
  if (FindTextSimple(lpszFind, bCase, bWord, bNext)) return TRUE;
  ctrl.SetSel(crOld);
  return FALSE;
}

BOOL CRichEditView::SameAsSelected(LPCTSTR lpszCompare, BOOL bCase, BOOL bWord) {
  (void)bWord;
  CString strSelect = GetRichEditCtrl().GetSelText();
  int nLen = lstrlen(lpszCompare);
  if (strSelect.GetLength() != nLen) return FALSE;
  return ::CompareString(LOCALE_USER_DEFAULT, bCase ? 0 : NORM_IGNORECASE, strSelect, nLen,
                         lpszCompare, nLen) == CSTR_EQUAL;
}

void CRichEditView::OnFindNext(LPCTSTR lpszFind, BOOL bNext, BOOL bCase, BOOL bWord) {
  AFX_RICHEDIT_STATE& state = _AfxRichEditState();
  state.strFind = lpszFind;
  state.bCase = bCase;
  state.bWord = bWord;
  state.bNext = bNext;
  if (!FindText(state.strFind, bCase, bWord, bNext)) OnTextNotFound(state.strFind);
}

void CRichEditView::OnReplaceSel(LPCTSTR lpszFind, BOOL bNext, BOOL bCase, BOOL bWord,
                                 LPCTSTR lpszReplace) {
  AFX_RICHEDIT_STATE& state = _AfxRichEditState();
  state.strFind = lpszFind;
  state.strReplace = lpszReplace;
  state.bCase = bCase;
  state.bWord = bWord;
  state.bNext = bNext;
  if (SameAsSelected(state.strFind, bCase, bWord))
    GetRichEditCtrl().ReplaceSel(state.strReplace, TRUE);
  if (!FindText(state.strFind, bCase, bWord, bNext)) OnTextNotFound(state.strFind);
}

void CRichEditView::OnReplaceAll(LPCTSTR lpszFind, LPCTSTR lpszReplace, BOOL bCase, BOOL bWord) {
  AFX_RICHEDIT_STATE& state = _AfxRichEditState();
  state.strFind = lpszFind;
  state.strReplace = lpszReplace;
  state.bCase = bCase;
  state.bWord = bWord;
  state.bNext = TRUE;

  // One pass from the top, so a replacement that contains the search
  // text is not replaced again.
  CWaitCursor wait;
  CRichEditCtrl& ctrl = GetRichEditCtrl();
  ctrl.HideSelection(TRUE, FALSE);
  ctrl.SetSel(0, 0);
  BOOL bFound = FALSE;
  while (FindTextSimple(state.strFind, bCase, bWord, TRUE)) {
    bFound = TRUE;
    ctrl.ReplaceSel(state.strReplace, TRUE);
  }
  ctrl.HideSelection(FALSE, FALSE);
  if (!bFound) OnTextNotFound(state.strFind);
}

void CRichEditView::OnTextNotFound(LPCTSTR) { ::MessageBeep(MB_ICONHAND); }

void CRichEditView::OnEditFindReplace(BOOL bFindOnly) {
  AFX_RICHEDIT_STATE& state = _AfxRichEditState();
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
  CString strFind = GetRichEditCtrl().GetSelText();
  if (strFind.IsEmpty() || strFind.FindOneOf(_T("\r\n")) >= 0) strFind = state.strFind;

  CFindReplaceDialog* pDlg = new CFindReplaceDialog;
  DWORD dwFlags = (state.bNext ? FR_DOWN : 0) | (state.bCase ? FR_MATCHCASE : 0) |
                  (state.bWord ? FR_WHOLEWORD : 0);
  if (!pDlg->Create(bFindOnly, strFind, state.strReplace, dwFlags, this)) {
    delete pDlg;
    return;
  }
  state.pFindReplaceDlg = pDlg;
  state.bFindOnly = bFindOnly;
  pDlg->SetActiveWindow();
  pDlg->ShowWindow(SW_SHOW);
}

LRESULT CRichEditView::OnFindReplaceCmd(WPARAM, LPARAM lParam) {
  AFX_RICHEDIT_STATE& state = _AfxRichEditState();
  CFindReplaceDialog* pDlg = CFindReplaceDialog::GetNotifier(lParam);
  if (pDlg == NULL) return 0;
  if (pDlg->IsTerminating()) {
    state.pFindReplaceDlg = NULL;
  } else if (pDlg->FindNext()) {
    OnFindNext(pDlg->GetFindString(), pDlg->SearchDown(), pDlg->MatchCase(),
               pDlg->MatchWholeWord());
  } else if (pDlg->ReplaceCurrent()) {
    OnReplaceSel(pDlg->GetFindString(), pDlg->SearchDown(), pDlg->MatchCase(),
                 pDlg->MatchWholeWord(), pDlg->GetReplaceString());
  } else if (pDlg->ReplaceAll()) {
    OnReplaceAll(pDlg->GetFindString(), pDlg->GetReplaceString(), pDlg->MatchCase(),
                 pDlg->MatchWholeWord());
  }
  return 0;
}

void CRichEditView::OnEditFind() { OnEditFindReplace(TRUE); }

void CRichEditView::OnEditReplace() { OnEditFindReplace(FALSE); }

void CRichEditView::OnEditRepeat() {
  AFX_RICHEDIT_STATE& state = _AfxRichEditState();
  if (state.strFind.IsEmpty()) {
    OnEditFindReplace(TRUE);
    return;
  }
  if (!FindText(state.strFind, state.bCase, state.bWord, state.bNext))
    OnTextNotFound(state.strFind);
}

// ---------------------------------------------------------------------
// Edit commands

BOOL AFX_CDECL CRichEditView::IsRichEditFormat(CLIPFORMAT cf) {
  static UINT cfRTF = ::RegisterClipboardFormat(CF_RTF);
  return cf == CF_TEXT || cf == CF_UNICODETEXT || cf == cfRTF;
}

BOOL CRichEditView::CanPaste() const {
  return GetRichEditCtrl().CanPaste(0);
}

void CRichEditView::OnUpdateNeedSel(CCmdUI* pCmdUI) {
  CHARRANGE cr;
  GetRichEditCtrl().GetSel(cr);
  pCmdUI->Enable(cr.cpMin != cr.cpMax);
}

void CRichEditView::OnUpdateNeedClip(CCmdUI* pCmdUI) { pCmdUI->Enable(CanPaste()); }

void CRichEditView::OnUpdateNeedText(CCmdUI* pCmdUI) { pCmdUI->Enable(GetTextLength() != 0); }

void CRichEditView::OnUpdateNeedFind(CCmdUI* pCmdUI) {
  pCmdUI->Enable(GetTextLength() != 0 && !_AfxRichEditState().strFind.IsEmpty());
}

void CRichEditView::OnUpdateEditUndo(CCmdUI* pCmdUI) { pCmdUI->Enable(GetRichEditCtrl().CanUndo()); }

void CRichEditView::OnEditCut() { GetRichEditCtrl().Cut(); }
void CRichEditView::OnEditCopy() { GetRichEditCtrl().Copy(); }
void CRichEditView::OnEditPaste() { GetRichEditCtrl().Paste(); }
void CRichEditView::OnEditClear() { GetRichEditCtrl().Clear(); }
void CRichEditView::OnEditUndo() {
  GetRichEditCtrl().Undo();
  m_bSyncCharFormat = m_bSyncParaFormat = TRUE;
}
void CRichEditView::OnEditSelectAll() { GetRichEditCtrl().SetSel(0, -1); }

// ---------------------------------------------------------------------
// Printing: the control lays the text out a page at a time with
// EM_FORMATRANGE, inside the margins.

BOOL CRichEditView::OnPreparePrinting(CPrintInfo* pInfo) { return DoPreparePrinting(pInfo); }

void CRichEditView::OnBeginPrinting(CDC* pDC, CPrintInfo* pInfo) {
  (void)pInfo;
  // The page is the printer's.
  if (pDC != NULL) OnPrinterChanged(*pDC);
  m_aPageStart.RemoveAll();
  m_aPageStart.Add(0);
}

// rectLayout is in twips on the page; the device's origin is the corner
// of its printable area.
long CRichEditView::PrintInsideRect(CDC* pDC, RECT& rectLayout, long nIndexStart,
                                    long nIndexStop, BOOL bOutput) {
  HDC hAttrib = pDC->m_hAttribDC != NULL ? pDC->m_hAttribDC : pDC->m_hDC;
  int cxInch = ::GetDeviceCaps(hAttrib, LOGPIXELSX), cyInch = ::GetDeviceCaps(hAttrib, LOGPIXELSY);
  int xOffset = ::GetDeviceCaps(hAttrib, PHYSICALOFFSETX);
  int yOffset = ::GetDeviceCaps(hAttrib, PHYSICALOFFSETY);
  CPoint ptOffset(cxInch > 0 ? ::MulDiv(xOffset, 1440, cxInch) : 0,
                  cyInch > 0 ? ::MulDiv(yOffset, 1440, cyInch) : 0);

  FORMATRANGE fr;
  fr.hdc = pDC->m_hDC;
  fr.hdcTarget = hAttrib;
  fr.rc = rectLayout;
  ::OffsetRect(&fr.rc, -ptOffset.x, -ptOffset.y);
  fr.rcPage = GetPageRect();
  ::OffsetRect(&fr.rcPage, -ptOffset.x, -ptOffset.y);
  fr.chrg.cpMin = nIndexStart;
  fr.chrg.cpMax = nIndexStop >= 0 ? nIndexStop : GetTextLength();
  long nNext = GetRichEditCtrl().FormatRange(&fr, bOutput);
  // The control says how far down it got.
  rectLayout.bottom = fr.rc.bottom + ptOffset.y;
  return nNext;
}

long CRichEditView::PrintPage(CDC* pDC, long nIndexStart, long nIndexStop) {
  CRect rect = GetPrintRect();
  return PrintInsideRect(pDC, rect, nIndexStart, nIndexStop, TRUE);
}

void CRichEditView::OnPrepareDC(CDC* pDC, CPrintInfo* pInfo) {
  CCtrlView::OnPrepareDC(pDC, pInfo);
  if (pInfo == NULL || !pDC->IsPrinting()) return;

  // Lay out the pages up to the one being printed.
  long nTextLen = GetTextLength();
  while ((UINT)m_aPageStart.GetSize() < pInfo->m_nCurPage) {
    long nStart = (long)m_aPageStart[m_aPageStart.GetSize() - 1];
    if (nStart >= nTextLen) break;
    CRect rect = GetPrintRect();
    long nNext = PrintInsideRect(pDC, rect, nStart, -1, FALSE);
    if (nNext <= nStart) break;  // nothing fits
    m_aPageStart.Add((DWORD)nNext);
  }
  GetRichEditCtrl().FormatRange(NULL, FALSE);
  pInfo->m_bContinuePrinting =
      (UINT)m_aPageStart.GetSize() >= pInfo->m_nCurPage &&
      (long)m_aPageStart[pInfo->m_nCurPage - 1] < nTextLen;
  if (nTextLen == 0) pInfo->m_bContinuePrinting = pInfo->m_nCurPage == 1;
}

void CRichEditView::OnPrint(CDC* pDC, CPrintInfo* pInfo) {
  if ((UINT)m_aPageStart.GetSize() < pInfo->m_nCurPage) return;
  long nNext = PrintPage(pDC, (long)m_aPageStart[pInfo->m_nCurPage - 1], -1);
  if ((UINT)m_aPageStart.GetSize() == pInfo->m_nCurPage) m_aPageStart.Add((DWORD)nNext);
  GetRichEditCtrl().FormatRange(NULL, FALSE);
}

void CRichEditView::OnEndPrinting(CDC* pDC, CPrintInfo* pInfo) {
  (void)pDC;
  (void)pInfo;
  GetRichEditCtrl().FormatRange(NULL, FALSE);
  m_aPageStart.RemoveAll();
}

// ---------------------------------------------------------------------
// Objects in the text

BOOL CRichEditView::IsSelected(const CObject* pDocItem) const {
  return pDocItem != NULL && pDocItem == GetSelectedItem();
}

CRichEditCntrItem* CRichEditView::GetSelectedItem() const {
  if (m_lpRichEditOle == NULL) return NULL;
  REOBJECT reo;
  memset(&reo, 0, sizeof(reo));
  reo.cbStruct = sizeof(reo);
  if (m_lpRichEditOle->GetObject(REO_IOB_SELECTION, &reo, REO_GETOBJ_POLEOBJ) != S_OK ||
      reo.poleobj == NULL)
    return NULL;
  // Only a selection that is just the object.
  CHARRANGE cr;
  GetRichEditCtrl().GetSel(cr);
  CRichEditCntrItem* pItem = NULL;
  if (cr.cpMax - cr.cpMin == 1) {
    CRichEditDoc* pDoc = GetDocument();
    pItem = pDoc->LookupItem(reo.poleobj);
    if (pItem == NULL) {
      // Ask again for everything the item needs.
      LPOLEOBJECT lpObject = reo.poleobj;
      memset(&reo, 0, sizeof(reo));
      reo.cbStruct = sizeof(reo);
      if (m_lpRichEditOle->GetObject(REO_IOB_SELECTION, &reo,
                                     REO_GETOBJ_POLEOBJ | REO_GETOBJ_PSTG) == S_OK) {
        pItem = pDoc->CreateClientItem(&reo);
        if (reo.poleobj != NULL) reo.poleobj->Release();
        if (reo.pstg != NULL) reo.pstg->Release();
      }
      reo.poleobj = lpObject;
    }
  }
  reo.poleobj->Release();
  return pItem;
}

CRichEditCntrItem* CRichEditView::GetInPlaceActiveItem() const {
  CRichEditDoc* pDoc = GetDocument();
  return pDoc != NULL ? (CRichEditCntrItem*)pDoc->GetInPlaceActiveItem((CWnd*)this) : NULL;
}

void CRichEditView::InsertItem(CRichEditCntrItem* pItem) {
  if (m_lpRichEditOle == NULL || pItem->m_lpObject == NULL) AfxThrowOleException(E_UNEXPECTED);
  REOBJECT reo;
  memset(&reo, 0, sizeof(reo));
  reo.cbStruct = sizeof(reo);
  reo.cp = REO_CP_SELECTION;
  pItem->GetClassID(&reo.clsid);
  reo.poleobj = pItem->m_lpObject;
  reo.pstg = pItem->m_lpStorage;
  reo.dvaspect = pItem->GetDrawAspect();
  reo.dwFlags = REO_RESIZABLE;
  // In HIMETRIC; left at 0 the control asks the object.
  CSize size(0, 0);
  if (pItem->GetExtent(&size)) {
    reo.sizel.cx = size.cx;
    reo.sizel.cy = size.cy;
  }
  SCODE sc = m_lpRichEditOle->GetClientSite(&reo.polesite);
  if (SUCCEEDED(sc)) {
    sc = m_lpRichEditOle->InsertObject(&reo);
    reo.polesite->Release();
  }
  if (FAILED(sc)) AfxThrowOleException(sc);
  pItem->m_pView = this;
  GetDocument()->SetModifiedFlag();
}

void CRichEditView::DoPaste(COleDataObject& dataobj, CLIPFORMAT cf, HMETAFILEPICT hMetaPict) {
  CRichEditCntrItem* pItem = GetDocument()->CreateClientItem(NULL);
  try {
    BOOL bCreated = cf == 0 ? pItem->CreateFromData(&dataobj)
                            : pItem->CreateStaticFromData(&dataobj, OLERENDER_FORMAT, cf);
    if (!bCreated) AfxThrowOleException(pItem->GetLastStatus());
    if (hMetaPict != NULL) {
      pItem->SetIconicMetafile(hMetaPict);
      pItem->SetDrawAspect(DVASPECT_ICON);
    }
    InsertItem(pItem);
  } catch (CException*) {
    pItem->Delete();
    throw;
  }
}

HRESULT CRichEditView::QueryAcceptData(LPDATAOBJECT, CLIPFORMAT*, DWORD, BOOL, HGLOBAL) {
  return S_OK;
}

HRESULT CRichEditView::GetClipboardData(CHARRANGE*, DWORD, LPDATAOBJECT, LPDATAOBJECT* lplpdataobj) {
  *lplpdataobj = NULL;
  return E_NOTIMPL;
}

HMENU CRichEditView::GetContextMenu(WORD, LPOLEOBJECT, CHARRANGE*) { return NULL; }

void CRichEditView::OnInsertObject() {
  COleInsertDialog dlg;
  if (dlg.DoModal() != IDOK) return;
  CWaitCursor wait;
  CRichEditCntrItem* pItem = GetDocument()->CreateClientItem(NULL);
  try {
    if (!dlg.CreateItem(pItem)) AfxThrowOleException(pItem->GetLastStatus());
    InsertItem(pItem);
  } catch (COleException* e) {
    pItem->ReportError(e->m_sc);
    e->Delete();
    pItem->Delete();
    return;
  }
  // A new object opens so that it can be filled in.
  if (dlg.GetSelectionType() == COleInsertDialog::createNewItem)
    pItem->DoVerb(OLEIVERB_SHOW, this);
}

void CRichEditView::OnEditPasteSpecial() {
  COlePasteSpecialDialog dlg;
  // The control's own formats first, then the objects and pictures.
  dlg.AddFormat(CF_TEXT, TYMED_HGLOBAL, AFX_IDS_RTF_FORMAT_TEXT, FALSE, FALSE);
  dlg.AddStandardFormats();
  if (dlg.DoModal() != IDOK) return;
  int i = dlg.GetPasteIndex();
  if (dlg.GetSelectionType() == COlePasteSpecialDialog::pasteOther && i == 0) {
    GetRichEditCtrl().PasteSpecial(CF_TEXT);
    return;
  }
  CRichEditCntrItem* pItem = GetDocument()->CreateClientItem(NULL);
  try {
    if (!dlg.CreateItem(pItem)) AfxThrowOleException(pItem->GetLastStatus());
    InsertItem(pItem);
  } catch (COleException* e) {
    pItem->ReportError(e->m_sc);
    e->Delete();
    pItem->Delete();
  }
}

void CRichEditView::OnUpdateEditPasteSpecial(CCmdUI* pCmdUI) {
  pCmdUI->Enable(::CountClipboardFormats() != 0);
}

// ---------------------------------------------------------------------
// IRichEditOleCallback

STDMETHODIMP_(ULONG) CRichEditView::XRichEditOleCallback::AddRef() {
  METHOD_PROLOGUE_EX_(CRichEditView, RichEditOleCallback)
  return pThis->ExternalAddRef();
}

STDMETHODIMP_(ULONG) CRichEditView::XRichEditOleCallback::Release() {
  METHOD_PROLOGUE_EX_(CRichEditView, RichEditOleCallback)
  return pThis->ExternalRelease();
}

STDMETHODIMP CRichEditView::XRichEditOleCallback::QueryInterface(REFIID iid, LPVOID* ppvObj) {
  METHOD_PROLOGUE_EX_(CRichEditView, RichEditOleCallback)
  return pThis->ExternalQueryInterface(&iid, ppvObj);
}

// Objects the control reads from rich text get a storage in memory.
STDMETHODIMP CRichEditView::XRichEditOleCallback::GetNewStorage(LPSTORAGE* lplpstg) {
  *lplpstg = NULL;
  LPLOCKBYTES lpLockBytes = NULL;
  SCODE sc = ::CreateILockBytesOnHGlobal(NULL, TRUE, &lpLockBytes);
  if (FAILED(sc)) return sc;
  sc = ::StgCreateDocfileOnILockBytes(
      lpLockBytes, STGM_CREATE | STGM_READWRITE | STGM_SHARE_EXCLUSIVE, 0, lplpstg);
  lpLockBytes->Release();
  return sc;
}

STDMETHODIMP CRichEditView::XRichEditOleCallback::GetInPlaceContext(LPOLEINPLACEFRAME*,
                                                                    LPOLEINPLACEUIWINDOW*,
                                                                    LPOLEINPLACEFRAMEINFO) {
  return E_NOTIMPL;
}

STDMETHODIMP CRichEditView::XRichEditOleCallback::ShowContainerUI(BOOL) { return S_OK; }

STDMETHODIMP CRichEditView::XRichEditOleCallback::QueryInsertObject(LPCLSID, LPSTORAGE, LONG) {
  return S_OK;
}

STDMETHODIMP CRichEditView::XRichEditOleCallback::DeleteObject(LPOLEOBJECT lpoleobj) {
  METHOD_PROLOGUE_EX(CRichEditView, RichEditOleCallback)
  CRichEditDoc* pDoc = pThis->GetDocument();
  CRichEditCntrItem* pItem = pDoc != NULL ? pDoc->LookupItem(lpoleobj) : NULL;
  if (pItem != NULL && !pItem->m_bLock) delete pItem;
  return S_OK;
}

STDMETHODIMP CRichEditView::XRichEditOleCallback::QueryAcceptData(LPDATAOBJECT lpdataobj,
                                                                  CLIPFORMAT* lpcfFormat,
                                                                  DWORD reco, BOOL fReally,
                                                                  HGLOBAL hMetaPict) {
  METHOD_PROLOGUE_EX(CRichEditView, RichEditOleCallback)
  return pThis->QueryAcceptData(lpdataobj, lpcfFormat, reco, fReally, hMetaPict);
}

STDMETHODIMP CRichEditView::XRichEditOleCallback::ContextSensitiveHelp(BOOL) { return S_OK; }

STDMETHODIMP CRichEditView::XRichEditOleCallback::GetClipboardData(CHARRANGE* lpchrg, DWORD reco,
                                                                   LPDATAOBJECT* lplpdataobj) {
  METHOD_PROLOGUE_EX(CRichEditView, RichEditOleCallback)
  return pThis->GetClipboardData(lpchrg, reco, NULL, lplpdataobj);
}

STDMETHODIMP CRichEditView::XRichEditOleCallback::GetDragDropEffect(BOOL, DWORD, LPDWORD) {
  return E_NOTIMPL;
}

STDMETHODIMP CRichEditView::XRichEditOleCallback::GetContextMenu(WORD seltype,
                                                                 LPOLEOBJECT lpoleobj,
                                                                 CHARRANGE* lpchrg,
                                                                 HMENU* lphmenu) {
  METHOD_PROLOGUE_EX(CRichEditView, RichEditOleCallback)
  *lphmenu = pThis->GetContextMenu(seltype, lpoleobj, lpchrg);
  return *lphmenu != NULL ? S_OK : E_NOTIMPL;
}

// ---------------------------------------------------------------------
// CRichEditDoc

CRichEditDoc::CRichEditDoc() { m_bRTF = TRUE; }

CRichEditDoc::~CRichEditDoc() {}

CRichEditView* CRichEditDoc::GetView() const {
  POSITION pos = GetFirstViewPosition();
  while (pos != NULL) {
    CView* pView = GetNextView(pos);
    if (pView->IsKindOf(RUNTIME_CLASS(CRichEditView))) return (CRichEditView*)pView;
  }
  return NULL;
}

CRichEditCntrItem* CRichEditDoc::CreateClientItem(REOBJECT* preo) const {
  return new CRichEditCntrItem(preo, (CRichEditDoc*)this);
}

CRichEditCntrItem* CRichEditDoc::LookupItem(LPOLEOBJECT lpobj) const {
  POSITION pos = GetStartPosition();
  while (pos != NULL) {
    CDocItem* pItem = GetNextItem(pos);
    if (pItem->IsKindOf(RUNTIME_CLASS(CRichEditCntrItem)) &&
        ((CRichEditCntrItem*)pItem)->m_lpObject == lpobj)
      return (CRichEditCntrItem*)pItem;
  }
  return NULL;
}

// Only the objects still in the text count: the control may drop an
// object without telling the callback, which leaves its item behind.
void CRichEditDoc::UpdateModifiedFlag() {
  CRichEditView* pView = GetView();
  if (pView == NULL || pView->m_lpRichEditOle == NULL) {
    COleDocument::UpdateModifiedFlag();
    return;
  }
  LPRICHEDITOLE lpReOle = pView->m_lpRichEditOle;
  LONG n = lpReOle->GetObjectCount();
  for (LONG i = 0; i < n; i++) {
    REOBJECT reo;
    memset(&reo, 0, sizeof(reo));
    reo.cbStruct = sizeof(reo);
    if (lpReOle->GetObject(i, &reo, REO_GETOBJ_POLEOBJ) != S_OK) continue;
    CRichEditCntrItem* pItem = LookupItem(reo.poleobj);
    if (reo.poleobj != NULL) reo.poleobj->Release();
    if (pItem != NULL && pItem->IsModified()) {
      SetModifiedFlag();
      return;
    }
  }
}

// The objects are part of the text, so the control saves and loads them.
void CRichEditDoc::Serialize(CArchive& ar) {
  CRichEditView* pView = GetView();
  if (pView != NULL) pView->Serialize(ar);
}

void CRichEditDoc::DeleteContents() {
  // The text first: that lets the control release its objects.
  CRichEditView* pView = GetView();
  if (pView != NULL) pView->DeleteContents();
  COleDocument::DeleteContents();
}

BOOL CRichEditDoc::OnNewDocument() {
  if (!COleDocument::OnNewDocument()) return FALSE;
  CRichEditView* pView = GetView();
  if (pView != NULL) pView->GetRichEditCtrl().SetModify(FALSE);
  return TRUE;
}

// ---------------------------------------------------------------------
// CRichEditCntrItem

CRichEditCntrItem::CRichEditCntrItem(REOBJECT* preo, CRichEditDoc* pContainer)
    : COleClientItem(pContainer), m_bLock(FALSE) {
  if (preo != NULL) SyncToRichEditObject(*preo);
}

CRichEditCntrItem::~CRichEditCntrItem() { Release(); }

void CRichEditCntrItem::SyncToRichEditObject(REOBJECT& reo) {
  m_nDrawAspect = (DVASPECT)reo.dvaspect;
  if (reo.poleobj == NULL || reo.poleobj == m_lpObject) return;
  Release();
  m_lpObject = reo.poleobj;
  m_lpObject->AddRef();
  if (reo.pstg != NULL) {
    m_lpStorage = reo.pstg;
    m_lpStorage->AddRef();
  }
  m_lpObject->QueryInterface(IID_IViewObject2, (void**)&m_lpViewObject);
  LPOLELINK lpLink = NULL;
  if (SUCCEEDED(m_lpObject->QueryInterface(IID_IOleLink, (void**)&lpLink))) {
    lpLink->Release();
    m_nItemType = OT_LINK;
  } else {
    m_nItemType = OT_EMBEDDED;
  }
  m_nItemState = loadedState;
}

// The control keeps the object, so the item only lets go of it.
void CRichEditCntrItem::Release(OLECLOSE) {
  if (m_lpViewObject != NULL) {
    m_lpViewObject->SetAdvise(m_nDrawAspect, 0, NULL);
    m_lpViewObject->Release();
    m_lpViewObject = NULL;
  }
  if (m_lpObject != NULL) {
    if (m_dwConnection != 0) m_lpObject->Unadvise(m_dwConnection);
    m_dwConnection = 0;
    m_lpObject->Release();
    m_lpObject = NULL;
  }
  ReleaseStorage();
  m_nItemState = emptyState;
}

void CRichEditCntrItem::OnChange(OLE_NOTIFICATION nCode, DWORD dwParam) {
  COleClientItem::OnChange(nCode, dwParam);
  if (m_pView != NULL && (nCode == OLE_CHANGED || nCode == OLE_CHANGED_ASPECT))
    m_pView->Invalidate();
}

BOOL CRichEditCntrItem::CanActivate() { return FALSE; }
