// viewprnt.cpp - printing from a view: CView::OnFilePrint and friends,
// CPrintInfo and CWinApp::DoPrintDialog.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include "afximpl.h"
#include <afxext.h>

// ---------------------------------------------------------------------
// CPrintInfo

CPrintInfo::CPrintInfo() {
  m_pPD = new CPrintDialog(FALSE, PD_ALLPAGES | PD_USEDEVMODECOPIES | PD_NOSELECTION);
  SetMinPage(1);
  SetMaxPage(0xFFFF);
  m_pPD->m_pd.nFromPage = 1;
  m_pPD->m_pd.nToPage = 0xFFFF;
  m_bDocObject = FALSE;
  m_dwFlags = 0;
  m_nOffsetPage = 0;
  m_bDirect = FALSE;
  m_bPreview = FALSE;
  m_bContinuePrinting = TRUE;
  m_nCurPage = 1;
  m_nNumPreviewPages = 0;
  m_lpUserData = NULL;
  m_rectDraw.SetRectEmpty();
  m_nJobNumber = 0;
}

CPrintInfo::~CPrintInfo() {
  if (m_pPD != NULL && m_pPD->m_pd.hDC != NULL) {
    ::DeleteDC(m_pPD->m_pd.hDC);
    m_pPD->m_pd.hDC = NULL;
  }
  delete m_pPD;
}

// ---------------------------------------------------------------------
// CWinApp

int CWinApp::DoPrintDialog(CPrintDialog* pPD) {
  UpdatePrinterSelection(FALSE);
  pPD->m_pd.hDevMode = m_hDevMode;
  pPD->m_pd.hDevNames = m_hDevNames;
  INT_PTR nResponse = pPD->DoModal();
  // A stale device mode makes the dialog fail; start over with defaults.
  while (nResponse != IDOK && nResponse != IDCANCEL) {
    if (::CommDlgExtendedError() != PDERR_DNDMMISMATCH) return IDCANCEL;
    AfxGlobalFree(m_hDevMode);
    AfxGlobalFree(m_hDevNames);
    m_hDevMode = m_hDevNames = NULL;
    pPD->m_pd.hDevMode = pPD->m_pd.hDevNames = NULL;
    nResponse = pPD->DoModal();
  }
  m_hDevMode = pPD->m_pd.hDevMode;
  m_hDevNames = pPD->m_pd.hDevNames;
  return (int)nResponse;
}

// ---------------------------------------------------------------------
// CView printing

void CView::OnPrepareDC(CDC* pDC, CPrintInfo* pInfo) {
  (void)pDC;
  // Without a known page count, print a single page.
  if (pInfo != NULL)
    pInfo->m_bContinuePrinting = pInfo->GetMaxPage() != 0xFFFF || pInfo->m_nCurPage == 1;
}

BOOL CView::OnPreparePrinting(CPrintInfo* pInfo) { return DoPreparePrinting(pInfo); }

void CView::OnBeginPrinting(CDC* pDC, CPrintInfo* pInfo) {
  (void)pDC;
  (void)pInfo;
}

void CView::OnPrint(CDC* pDC, CPrintInfo* pInfo) {
  (void)pInfo;
  OnDraw(pDC);
}

void CView::OnEndPrinting(CDC* pDC, CPrintInfo* pInfo) {
  (void)pDC;
  (void)pInfo;
}

BOOL CView::DoPreparePrinting(CPrintInfo* pInfo) {
  CWinApp* pApp = AfxGetApp();
  PRINTDLG& pd = pInfo->m_pPD->m_pd;
  if (pInfo->m_bPreview || pInfo->m_bDirect ||
      (pInfo->m_bDocObject && !(pInfo->m_dwFlags & PRINTFLAG_PROMPTUSER))) {
    // No dialog: use the current printer.
    if (pd.hDC == NULL) {
      if (!pApp->GetPrinterDeviceDefaults(&pd)) {
        if (!pInfo->m_bDocObject || (pInfo->m_dwFlags & PRINTFLAG_MAYBOTHERUSER))
          if (pApp->DoPrintDialog(pInfo->m_pPD) != IDOK) return FALSE;
      }
      if (pd.hDC == NULL && pInfo->m_pPD->CreatePrinterDC() == NULL) return FALSE;
    }
    pd.nFromPage = (WORD)pInfo->GetMinPage();
    pd.nToPage = (WORD)pInfo->GetMaxPage();
  } else {
    if (pApp->DoPrintDialog(pInfo->m_pPD) != IDOK) return FALSE;
  }
  return pd.hDC != NULL;
}

// ---------------------------------------------------------------------
// The "Printing" dialog, with a Cancel button, shown while a job spools

static BOOL _afxUserAbort = FALSE;
static HWND _afxhWndPrinting = NULL;

class CPrintingDialog : public CDialog {
public:
  CPrintingDialog(CWnd* pParent) {
    HINSTANCE hInst = AfxFindResourceHandle(MAKEINTRESOURCE(AFX_IDD_PRINTDLG), RT_DIALOG);
    if (::FindResource(hInst, MAKEINTRESOURCE(AFX_IDD_PRINTDLG), RT_DIALOG) != NULL) {
      Create(AFX_IDD_PRINTDLG, pParent);
    } else {
      CAfxDlgTemplate dt(WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME | DS_CENTER, 133, 64,
                         L"Printing");
      dt.AddItem(0x82, SS_CENTER, 0, 8, 133, 8, AFX_IDC_PRINT_DOCNAME, L"");
      dt.AddItem(0x82, SS_CENTER, 0, 18, 133, 8, AFX_IDC_PRINT_PRINTERNAME, L"");
      dt.AddItem(0x82, SS_CENTER, 0, 28, 133, 8, AFX_IDC_PRINT_PAGENUM, L"");
      dt.AddItem(0x80, BS_DEFPUSHBUTTON | WS_TABSTOP, 44, 42, 45, 15, IDCANCEL, L"Cancel");
      CreateIndirect(dt.GetTemplate(), pParent);
    }
  }
  virtual BOOL OnInitDialog() {
    SetWindowText(AfxGetAppName());
    CenterWindow();
    return CDialog::OnInitDialog();
  }
  virtual void OnCancel() {
    _afxUserAbort = TRUE;
    CDialog::OnCancel();
  }
};

// The abort procedure keeps the program and the dialog responsive while
// GDI spools, and stops the job when the user cancels.
static BOOL CALLBACK _AfxAbortProc(HDC, int) {
  MSG msg;
  while (!_afxUserAbort && ::PeekMessage(&msg, NULL, 0, 0, PM_NOREMOVE)) {
    if (_afxhWndPrinting != NULL && ::IsDialogMessage(_afxhWndPrinting, &msg)) {
      ::PeekMessage(&msg, NULL, 0, 0, PM_REMOVE);
      continue;
    }
    if (!AfxPumpMessage()) return FALSE;
  }
  return !_afxUserAbort;
}

void CView::OnFilePrint() {
  CPrintInfo printInfo;
  const MSG* pMsg = GetCurrentMessage();
  if (pMsg != NULL && pMsg->message == WM_COMMAND && LOWORD(pMsg->wParam) == ID_FILE_PRINT_DIRECT)
    printInfo.m_bDirect = TRUE;
  if (!OnPreparePrinting(&printInfo)) return;
  HDC hDC = printInfo.m_pPD->m_pd.hDC;
  if (hDC == NULL) return;

  // Printing to a file asks for its name.
  CString strOutput;
  if (printInfo.m_pPD->m_pd.Flags & PD_PRINTTOFILE) {
    CFileDialog dlg(FALSE, _T("prn"), NULL, OFN_HIDEREADONLY | OFN_OVERWRITEPROMPT,
                    _T("Printer Files (*.prn)|*.prn|All Files (*.*)|*.*||"), this);
    if (dlg.DoModal() != IDOK) return;
    strOutput = dlg.GetPathName();
  }

  CString strTitle;
  CDocument* pDoc = GetDocument();
  if (pDoc != NULL) strTitle = pDoc->GetTitle();
  if (strTitle.GetLength() > 31) strTitle = strTitle.Left(31);

  DOCINFO docInfo;
  memset(&docInfo, 0, sizeof(docInfo));
  docInfo.cbSize = sizeof(docInfo);
  docInfo.lpszDocName = strTitle;
  docInfo.lpszOutput = strOutput.IsEmpty() ? NULL : (LPCTSTR)strOutput;

  CDC dcPrint;
  dcPrint.Attach(hDC);
  dcPrint.m_bPrinting = TRUE;
  OnBeginPrinting(&dcPrint, &printInfo);
  dcPrint.SetAbortProc(_AfxAbortProc);

  CWnd* pMainWnd = AfxGetMainWnd();
  if (pMainWnd != NULL) pMainWnd->EnableWindow(FALSE);

  // Show the document, the printer and the port while printing.
  _afxUserAbort = FALSE;
  CPrintingDialog dlgPrinting(pMainWnd);
  _afxhWndPrinting = dlgPrinting.m_hWnd;
  if (dlgPrinting.m_hWnd != NULL) {
    dlgPrinting.SetDlgItemText(AFX_IDC_PRINT_DOCNAME, strTitle);
    CString strPrinter = printInfo.m_pPD->GetDeviceName(), strPort = printInfo.m_pPD->GetPortName();
    if (!strPort.IsEmpty()) {
      CString strTemp;
      AfxFormatString1(strTemp, AFX_IDS_PRINTONPORT, strPort);
      if (dlgPrinting.GetDlgItem(AFX_IDC_PRINT_PORTNAME) != NULL)
        dlgPrinting.SetDlgItemText(AFX_IDC_PRINT_PORTNAME, strTemp);
      else
        strPrinter += _T(" ") + strTemp;
    }
    dlgPrinting.SetDlgItemText(AFX_IDC_PRINT_PRINTERNAME, strPrinter);
    dlgPrinting.ShowWindow(SW_SHOW);
    dlgPrinting.UpdateWindow();
  }

  if (dcPrint.StartDoc(&docInfo) == SP_ERROR) {
    _afxhWndPrinting = NULL;
    dlgPrinting.DestroyWindow();
    if (pMainWnd != NULL) pMainWnd->EnableWindow(TRUE);
    OnEndPrinting(&dcPrint, &printInfo);
    dcPrint.Detach();
    AfxMessageBox(AFX_IDP_FAILED_TO_START_PRINT);
    return;
  }

  UINT nStartPage = printInfo.GetFromPage();
  UINT nEndPage = printInfo.GetToPage();
  if (!(printInfo.m_pPD->m_pd.Flags & PD_PAGENUMS)) {
    nStartPage = printInfo.GetMinPage();
    nEndPage = printInfo.GetMaxPage();
  }
  if (nStartPage < printInfo.GetMinPage()) nStartPage = printInfo.GetMinPage();
  if (nStartPage > printInfo.GetMaxPage()) nStartPage = printInfo.GetMaxPage();
  if (nEndPage < printInfo.GetMinPage()) nEndPage = printInfo.GetMinPage();
  if (nEndPage > printInfo.GetMaxPage()) nEndPage = printInfo.GetMaxPage();
  int nStep = nEndPage >= nStartPage ? 1 : -1;
  nEndPage += nStep;

  BOOL bError = FALSE;
  for (printInfo.m_nCurPage = nStartPage; printInfo.m_nCurPage != nEndPage;
       printInfo.m_nCurPage += nStep) {
    OnPrepareDC(&dcPrint, &printInfo);
    if (!printInfo.m_bContinuePrinting) break;
    printInfo.m_rectDraw.SetRect(0, 0, dcPrint.GetDeviceCaps(HORZRES),
                                 dcPrint.GetDeviceCaps(VERTRES));
    dcPrint.DPtoLP(&printInfo.m_rectDraw);
    if (dcPrint.StartPage() < 0) {
      bError = TRUE;
      break;
    }
    if (dlgPrinting.m_hWnd != NULL) {
      CString strPage;
      AfxLoadString(AFX_IDS_PRINTPAGENUM, strPage);
      CString strText;
      strText.Format(strPage, printInfo.m_nCurPage);
      dlgPrinting.SetDlgItemText(AFX_IDC_PRINT_PAGENUM, strText);
    }
    // StartPage resets the DC on Windows 95, so prepare it again.
    OnPrepareDC(&dcPrint, &printInfo);
    OnPrint(&dcPrint, &printInfo);
    if (dcPrint.EndPage() < 0 || !_AfxAbortProc(dcPrint.m_hDC, 0)) {
      bError = TRUE;
      break;
    }
  }
  if (!bError)
    dcPrint.EndDoc();
  else
    dcPrint.AbortDoc();

  // The main window is enabled before the dialog goes, so activation
  // returns to it.
  if (pMainWnd != NULL) pMainWnd->EnableWindow(TRUE);
  _afxhWndPrinting = NULL;
  dlgPrinting.DestroyWindow();
  OnEndPrinting(&dcPrint, &printInfo);
  dcPrint.Detach();
}
