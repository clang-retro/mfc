// dlgprop.cpp - CPropertyPage and CPropertySheet, on the common control
// property sheet.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
#include "afximpl.h"
#include <afxdlgs.h>

// The structure sizes Windows 95 and NT 4 understand.
#define _AFX_PSP_SIZE PROPSHEETPAGE_V1_SIZE
#define _AFX_PSH_SIZE PROPSHEETHEADER_V1_SIZE

// ---------------------------------------------------------------------
// CPropertyPage

IMPLEMENT_DYNAMIC(CPropertyPage, CDialog)

BEGIN_MESSAGE_MAP(CPropertyPage, CDialog)
END_MESSAGE_MAP()

// Called by the sheet just before it creates the page's window, so that
// the window is attached to the page object as it is created.
static UINT CALLBACK _AfxPropPageCallback(HWND, UINT message, LPPROPSHEETPAGE pPropPage) {
  if (message == PSPCB_CREATE) {
    CPropertyPage* pPage = (CPropertyPage*)pPropPage->lParam;
    AfxHookWindowCreate(pPage);
  }
  return TRUE;
}

CPropertyPage::CPropertyPage() { Construct((LPCTSTR)NULL, 0); }

CPropertyPage::CPropertyPage(UINT nIDTemplate, UINT nIDCaption, DWORD dwSize) {
  (void)dwSize;
  Construct(nIDTemplate, nIDCaption);
}

CPropertyPage::CPropertyPage(LPCTSTR lpszTemplateName, UINT nIDCaption, DWORD dwSize) {
  (void)dwSize;
  Construct(lpszTemplateName, nIDCaption);
}

CPropertyPage::~CPropertyPage() {}

void CPropertyPage::Construct(UINT nIDTemplate, UINT nIDCaption) {
  Construct(MAKEINTRESOURCE(nIDTemplate), nIDCaption);
}

void CPropertyPage::Construct(LPCTSTR lpszTemplateName, UINT nIDCaption) {
  memset(&m_psp, 0, sizeof(m_psp));
  m_psp.dwSize = _AFX_PSP_SIZE;
  m_psp.dwFlags = PSP_USECALLBACK;
  if (lpszTemplateName != NULL)
    m_psp.hInstance = AfxFindResourceHandle(lpszTemplateName, RT_DIALOG);
  m_psp.pszTemplate = lpszTemplateName;
  m_psp.pfnDlgProc = AfxDlgProc;
  m_psp.lParam = (LPARAM)this;
  m_psp.pfnCallback = _AfxPropPageCallback;
  m_lpszTemplateName = lpszTemplateName;
  if (IS_INTRESOURCE(lpszTemplateName)) m_nIDHelp = LOWORD((DWORD_PTR)lpszTemplateName);
  if (nIDCaption != 0) {
    m_strCaption.LoadString(nIDCaption);
    m_psp.pszTitle = m_strCaption;
    m_psp.dwFlags |= PSP_USETITLE;
  }
  m_bFirstSetActive = TRUE;
}

CPropertySheet* CPropertyPage::GetParentSheet() {
  CWnd* pParent = GetParent();
  if (pParent != NULL && pParent->IsKindOf(RUNTIME_CLASS(CPropertySheet)))
    return (CPropertySheet*)pParent;
  return NULL;
}

void CPropertyPage::CancelToClose() { ::SendMessage(::GetParent(m_hWnd), PSM_CANCELTOCLOSE, 0, 0); }

void CPropertyPage::SetModified(BOOL bChanged) {
  ::SendMessage(::GetParent(m_hWnd), bChanged ? PSM_CHANGED : PSM_UNCHANGED, (WPARAM)m_hWnd, 0);
}

LRESULT CPropertyPage::QuerySiblings(WPARAM wParam, LPARAM lParam) {
  return ::SendMessage(::GetParent(m_hWnd), PSM_QUERYSIBLINGS, wParam, lParam);
}

BOOL CPropertyPage::OnApply() {
  OnOK();
  return TRUE;
}

void CPropertyPage::OnReset() { OnCancel(); }

// A page does not end anything itself; the sheet does.
void CPropertyPage::OnOK() {}
void CPropertyPage::OnCancel() {}

BOOL CPropertyPage::OnSetActive() {
  if (m_bFirstSetActive)
    m_bFirstSetActive = FALSE;  // OnInitDialog already filled the controls
  else
    UpdateData(FALSE);
  return TRUE;
}

BOOL CPropertyPage::OnKillActive() { return UpdateData(TRUE); }

BOOL CPropertyPage::OnQueryCancel() { return TRUE; }

LRESULT CPropertyPage::OnWizardBack() { return 0; }
LRESULT CPropertyPage::OnWizardNext() { return 0; }
BOOL CPropertyPage::OnWizardFinish() { return TRUE; }
HWND CPropertyPage::OnWizardFinishEx() { return OnWizardFinish() ? NULL : (HWND)-1; }

static void _AfxSetSheetResult(CPropertyPage* pPage, int nResult) {
  CWnd* pSheet = CWnd::FromHandlePermanent(::GetParent(pPage->m_hWnd));
  if (pSheet != NULL && pSheet->IsKindOf(RUNTIME_CLASS(CPropertySheet)) &&
      !((CPropertySheet*)pSheet)->m_bEndDialogCalled)
    ((CPropertySheet*)pSheet)->m_nModalResult = nResult;
}

BOOL CPropertyPage::OnNotify(WPARAM wParam, LPARAM lParam, LRESULT* pResult) {
  NMHDR* pNMHDR = (NMHDR*)lParam;
  // Sheet notifications come from the sheet, not from a control.
  if (pNMHDR->hwndFrom == ::GetParent(m_hWnd)) {
    switch (pNMHDR->code) {
      case PSN_SETACTIVE:
        *pResult = OnSetActive() ? 0 : -1;
        return TRUE;
      case PSN_KILLACTIVE:
        *pResult = !OnKillActive();
        return TRUE;
      case PSN_APPLY:
        *pResult = OnApply() ? PSNRET_NOERROR : PSNRET_INVALID_NOCHANGEPAGE;
        // lParam is set when the apply comes from OK rather than Apply.
        // Not every comctl32 sends the sheet a WM_COMMAND for OK (Wine's
        // PSM_PRESSBUTTON does not), so record the result here too.
        if (*pResult == PSNRET_NOERROR && ((LPPSHNOTIFY)lParam)->lParam) _AfxSetSheetResult(this, IDOK);
        return TRUE;
      case PSN_RESET:
        OnReset();
        *pResult = 0;
        return TRUE;
      case PSN_QUERYCANCEL:
        *pResult = !OnQueryCancel();
        return TRUE;
      case PSN_WIZBACK:
        *pResult = OnWizardBack();
        return TRUE;
      case PSN_WIZNEXT:
        *pResult = OnWizardNext();
        return TRUE;
      case PSN_WIZFINISH:
        *pResult = !OnWizardFinish();
        if (*pResult == 0) _AfxSetSheetResult(this, ID_WIZFINISH);
        return TRUE;
      case PSN_HELP:
        SendMessage(WM_COMMAND, ID_HELP);
        *pResult = 0;
        return TRUE;
    }
  }
  return CDialog::OnNotify(wParam, lParam, pResult);
}

BOOL CPropertyPage::PreTranslateMessage(MSG* pMsg) {
  // The sheet does the dialog keyboard handling for all its pages.
  CWnd* pParent = GetParent();
  if (pParent != NULL && pParent->PreTranslateMessage(pMsg)) return TRUE;
  return FALSE;
}

// ---------------------------------------------------------------------
// CPropertySheet

IMPLEMENT_DYNAMIC(CPropertySheet, CWnd)

BEGIN_MESSAGE_MAP(CPropertySheet, CWnd)
  ON_MESSAGE(WM_INITDIALOG, HandleInitDialog)
END_MESSAGE_MAP()

CPropertySheet::CPropertySheet() { Construct((LPCTSTR)NULL, NULL, 0); }

CPropertySheet::CPropertySheet(UINT nIDCaption, CWnd* pParentWnd, UINT iSelectPage) {
  Construct(nIDCaption, pParentWnd, iSelectPage);
}

CPropertySheet::CPropertySheet(LPCTSTR pszCaption, CWnd* pParentWnd, UINT iSelectPage) {
  Construct(pszCaption, pParentWnd, iSelectPage);
}

CPropertySheet::~CPropertySheet() { delete[] (BYTE*)m_phpage; }

void CPropertySheet::Construct(UINT nIDCaption, CWnd* pParentWnd, UINT iSelectPage) {
  CString strCaption;
  strCaption.LoadString(nIDCaption);
  Construct(strCaption, pParentWnd, iSelectPage);
}

void CPropertySheet::Construct(LPCTSTR pszCaption, CWnd* pParentWnd, UINT iSelectPage) {
  memset(&m_psh, 0, sizeof(m_psh));
  m_psh.dwSize = _AFX_PSH_SIZE;
  m_psh.dwFlags = PSH_PROPSHEETPAGE;
  m_psh.nStartPage = iSelectPage;
  m_strCaption = pszCaption;
  m_psh.pszCaption = m_strCaption;
  m_pParentWnd = pParentWnd;
  m_bStacked = TRUE;
  m_bModeless = FALSE;
  m_nModalResult = IDCANCEL;
  m_bEndDialogCalled = FALSE;
  m_phpage = NULL;
}

void CPropertySheet::BuildPropPageArray() {
  // The sheet steps through the array by each page's dwSize, so pack the
  // short Windows 95 form of each page.
  delete[] (BYTE*)m_phpage;
  int nPages = (int)m_pages.GetCount();
  BYTE* pBuf = new BYTE[nPages * _AFX_PSP_SIZE + 1];
  BYTE* p = pBuf;
  POSITION pos = m_pages.GetHeadPosition();
  while (pos != NULL) {
    CPropertyPage* pPage = (CPropertyPage*)m_pages.GetNext(pos);
    memcpy(p, &pPage->m_psp, _AFX_PSP_SIZE);
    p += _AFX_PSP_SIZE;
  }
  m_phpage = (HPROPSHEETPAGE*)pBuf;
  m_psh.ppsp = (LPCPROPSHEETPAGE)pBuf;
  m_psh.nPages = nPages;
  m_psh.pszCaption = m_strCaption;
}

INT_PTR CPropertySheet::DoModal() {
  BuildPropPageArray();
  HWND hWndTop = NULL;
  m_psh.hwndParent = _AfxGetSafeOwner(m_pParentWnd->GetSafeHwnd(), &hWndTop);
  m_psh.hInstance = AfxGetResourceHandle();
  m_psh.dwFlags &= ~PSH_MODELESS;
  m_nModalResult = IDCANCEL;
  m_bEndDialogCalled = FALSE;
  m_bModeless = FALSE;

  // The sheet's own window is the first one ::PropertySheet creates.
  AfxHookWindowCreate(this);
  INT_PTR nResult = ::PropertySheet(&m_psh);
  AfxUnhookWindowCreate();
  if (hWndTop != NULL) ::EnableWindow(hWndTop, TRUE);
  if (nResult < 0) return -1;
  return m_nModalResult;
}

BOOL CPropertySheet::Create(CWnd* pParentWnd, DWORD dwStyle, DWORD dwExStyle) {
  (void)dwStyle;
  (void)dwExStyle;
  BuildPropPageArray();
  if (pParentWnd == NULL) pParentWnd = m_pParentWnd;
  m_psh.hwndParent = pParentWnd->GetSafeHwnd();
  m_psh.hInstance = AfxGetResourceHandle();
  m_psh.dwFlags |= PSH_MODELESS;
  m_bModeless = TRUE;
  AfxHookWindowCreate(this);
  HWND hWnd = (HWND)::PropertySheet(&m_psh);
  AfxUnhookWindowCreate();
  return hWnd != NULL;
}

LRESULT CPropertySheet::HandleInitDialog(WPARAM, LPARAM) {
  LRESULT lResult = Default();
  OnInitDialog();
  return lResult;
}

BOOL CPropertySheet::OnInitDialog() { return TRUE; }

BOOL CPropertySheet::OnCommand(WPARAM wParam, LPARAM lParam) {
  UINT nID = LOWORD(wParam);
  if (HIWORD(wParam) == BN_CLICKED && !m_bEndDialogCalled) {
    if (nID == IDOK || nID == IDCANCEL || nID == ID_WIZFINISH) m_nModalResult = nID;
  }
  if (CWnd::OnCommand(wParam, lParam)) return TRUE;
  if (m_bModeless && (nID == IDOK || nID == IDCANCEL || nID == ID_WIZFINISH)) {
    // Let the sheet do its processing, then go away if it has finished.
    Default();
    if (::SendMessage(m_hWnd, PSM_GETCURRENTPAGEHWND, 0, 0) == 0) DestroyWindow();
    return TRUE;
  }
  return FALSE;
}

BOOL CPropertySheet::PreTranslateMessage(MSG* pMsg) {
  if (CWnd::PreTranslateMessage(pMsg)) return TRUE;
  return (BOOL)::SendMessage(m_hWnd, PSM_ISDIALOGMESSAGE, 0, (LPARAM)pMsg);
}

BOOL CPropertySheet::ContinueModal() { return TRUE; }

void CPropertySheet::PostNcDestroy() { CWnd::PostNcDestroy(); }

void CPropertySheet::AddPage(CPropertyPage* pPage) {
  m_pages.AddTail(pPage);
  if (m_hWnd != NULL) {
    PROPSHEETPAGE psp;
    memcpy(&psp, &pPage->m_psp, sizeof(psp));
    psp.dwSize = _AFX_PSP_SIZE;
    HPROPSHEETPAGE hPage = ::CreatePropertySheetPage(&psp);
    if (hPage != NULL) ::SendMessage(m_hWnd, PSM_ADDPAGE, 0, (LPARAM)hPage);
  }
}

void CPropertySheet::RemovePage(CPropertyPage* pPage) {
  int nPage = GetPageIndex(pPage);
  if (nPage >= 0) RemovePage(nPage);
}

void CPropertySheet::RemovePage(int nPage) {
  POSITION pos = m_pages.FindIndex(nPage);
  if (pos == NULL) return;
  if (m_hWnd != NULL) ::SendMessage(m_hWnd, PSM_REMOVEPAGE, nPage, 0);
  m_pages.RemoveAt(pos);
}

int CPropertySheet::GetPageCount() const { return (int)m_pages.GetCount(); }

CPropertyPage* CPropertySheet::GetPage(int nPage) const {
  POSITION pos = m_pages.FindIndex(nPage);
  return pos != NULL ? (CPropertyPage*)m_pages.GetAt(pos) : NULL;
}

int CPropertySheet::GetPageIndex(CPropertyPage* pPage) {
  int i = 0;
  POSITION pos = m_pages.GetHeadPosition();
  while (pos != NULL) {
    if (m_pages.GetNext(pos) == pPage) return i;
    i++;
  }
  return -1;
}

int CPropertySheet::GetActiveIndex() const {
  if (m_hWnd == NULL) return (int)m_psh.nStartPage;
  HWND hWndTab = (HWND)::SendMessage(m_hWnd, PSM_GETTABCONTROL, 0, 0);
  return hWndTab != NULL ? (int)::SendMessage(hWndTab, TCM_GETCURSEL, 0, 0) : 0;
}

CPropertyPage* CPropertySheet::GetActivePage() const { return GetPage(GetActiveIndex()); }

BOOL CPropertySheet::SetActivePage(int nPage) {
  if (m_hWnd == NULL) {
    m_psh.nStartPage = nPage;
    return TRUE;
  }
  return (BOOL)::SendMessage(m_hWnd, PSM_SETCURSEL, nPage, 0);
}

BOOL CPropertySheet::SetActivePage(CPropertyPage* pPage) {
  int nPage = GetPageIndex(pPage);
  return nPage >= 0 && SetActivePage(nPage);
}

void CPropertySheet::SetTitle(LPCTSTR lpszText, UINT nStyle) {
  m_strCaption = lpszText;
  if (m_hWnd != NULL) {
    ::SendMessage(m_hWnd, PSM_SETTITLE, nStyle, (LPARAM)lpszText);
  } else {
    m_psh.pszCaption = m_strCaption;
    if (nStyle & PSH_PROPTITLE) m_psh.dwFlags |= PSH_PROPTITLE;
  }
}

CTabCtrl* CPropertySheet::GetTabControl() const {
  return (CTabCtrl*)CWnd::FromHandle((HWND)::SendMessage(m_hWnd, PSM_GETTABCONTROL, 0, 0));
}

void CPropertySheet::SetWizardMode() { m_psh.dwFlags |= PSH_WIZARD; }

void CPropertySheet::SetWizardButtons(DWORD dwFlags) {
  ::PostMessage(m_hWnd, PSM_SETWIZBUTTONS, 0, dwFlags);
}

void CPropertySheet::SetFinishText(LPCTSTR lpszText) {
  ::SendMessage(m_hWnd, PSM_SETFINISHTEXT, 0, (LPARAM)lpszText);
}

void CPropertySheet::PressButton(int nButton) {
  ::SendMessage(m_hWnd, PSM_PRESSBUTTON, nButton, 0);
}

void CPropertySheet::EndDialog(int nEndID) {
  m_nModalResult = nEndID;
  m_bEndDialogCalled = TRUE;
  if (m_bModeless)
    DestroyWindow();
  else
    ::PostMessage(m_hWnd, PSM_PRESSBUTTON, PSBTN_CANCEL, 0);
}
