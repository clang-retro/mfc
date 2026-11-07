// dlgcore.cpp - CDialog: modal and modeless dialogs.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
#include "afximpl.h"

IMPLEMENT_DYNAMIC(CDialog, CWnd)

BEGIN_MESSAGE_MAP(CDialog, CWnd)
  ON_COMMAND(IDOK, OnOK)
  ON_COMMAND(IDCANCEL, OnCancel)
  ON_MESSAGE(WM_COMMANDHELP, OnCommandHelp)
  ON_MESSAGE(WM_HELPHITTEST, OnHelpHitTest)
  ON_MESSAGE(WM_INITDIALOG, HandleInitDialog)
  ON_MESSAGE(WM_SETFONT, HandleSetFont)
END_MESSAGE_MAP()

void CDialog::CommonConstruct() {
  m_nIDHelp = 0;
  m_lpszTemplateName = NULL;
  m_hDialogTemplate = NULL;
  m_lpDialogTemplate = NULL;
  m_lpDialogInit = NULL;
  m_pParentWnd = NULL;
  m_hWndTop = NULL;
  m_bModal = FALSE;
}

CDialog::CDialog() { CommonConstruct(); }

CDialog::CDialog(LPCTSTR lpszTemplateName, CWnd* pParentWnd) {
  CommonConstruct();
  m_pParentWnd = pParentWnd;
  m_lpszTemplateName = lpszTemplateName;
  if (IS_INTRESOURCE(m_lpszTemplateName)) m_nIDHelp = LOWORD((DWORD_PTR)m_lpszTemplateName);
}

CDialog::CDialog(UINT nIDTemplate, CWnd* pParentWnd) {
  CommonConstruct();
  m_pParentWnd = pParentWnd;
  m_lpszTemplateName = MAKEINTRESOURCE(nIDTemplate);
  m_nIDHelp = nIDTemplate;
}

CDialog::~CDialog() {
  if (m_hWnd != NULL) DestroyWindow();
}

// ---------------------------------------------------------------------
// Modeless dialogs

BOOL CDialog::CreateDlgIndirect(LPCDLGTEMPLATE lpDialogTemplate, CWnd* pParentWnd,
                                HINSTANCE hInst) {
  return CWnd::CreateDlgIndirect(lpDialogTemplate, pParentWnd, hInst);
}

BOOL CWnd::CreateDlgIndirect(LPCDLGTEMPLATE lpDialogTemplate, CWnd* pParentWnd,
                             HINSTANCE hInst) {
  if (hInst == NULL) hInst = AfxGetResourceHandle();
  // ActiveX controls in the template are made apart from it.
  if (_afxOccHooks != NULL)
    lpDialogTemplate = _afxOccHooks->pfnPreCreateDialog(this, lpDialogTemplate, hInst);
  AfxHookWindowCreate(this);
  HWND hWnd = ::CreateDialogIndirectParam(hInst, lpDialogTemplate, pParentWnd->GetSafeHwnd(),
                                          AfxDlgProc, 0);
  if (_afxOccHooks != NULL) _afxOccHooks->pfnPostCreateDialog(this);
  if (!AfxUnhookWindowCreate()) PostNcDestroy();
  if (hWnd == NULL && m_hWnd != NULL) {
    // Created and then failed (OnInitDialog asked for EndDialog(-1) and
    // the like): make sure nothing is left behind.
    DestroyWindow();
  }
  return hWnd != NULL;
}

static LPCDLGTEMPLATE _AfxLoadDialog(LPCTSTR lpszTemplateName, HINSTANCE* phInst) {
  HINSTANCE hInst = AfxFindResourceHandle(lpszTemplateName, RT_DIALOG);
  HRSRC hResource = ::FindResource(hInst, lpszTemplateName, RT_DIALOG);
  if (hResource == NULL) return NULL;
  HGLOBAL hTemplate = ::LoadResource(hInst, hResource);
  if (hTemplate == NULL) return NULL;
  if (phInst != NULL) *phInst = hInst;
  return (LPCDLGTEMPLATE)::LockResource(hTemplate);
}

BOOL CWnd::CreateDlg(LPCTSTR lpszTemplateName, CWnd* pParentWnd) {
  HINSTANCE hInst = NULL;
  LPCDLGTEMPLATE lpTemplate = _AfxLoadDialog(lpszTemplateName, &hInst);
  if (lpTemplate == NULL) return FALSE;
  return CreateDlgIndirect(lpTemplate, pParentWnd, hInst);
}

BOOL CDialog::Create(LPCTSTR lpszTemplateName, CWnd* pParentWnd) {
  m_lpszTemplateName = lpszTemplateName;
  if (IS_INTRESOURCE(m_lpszTemplateName) && m_nIDHelp == 0)
    m_nIDHelp = LOWORD((DWORD_PTR)m_lpszTemplateName);
  if (pParentWnd == NULL) pParentWnd = AfxGetMainWnd();
  m_pParentWnd = pParentWnd;
  HINSTANCE hInst = NULL;
  LPCDLGTEMPLATE lpTemplate = _AfxLoadDialog(lpszTemplateName, &hInst);
  if (lpTemplate == NULL) return FALSE;
  return CreateDlgIndirect(lpTemplate, pParentWnd, hInst);
}

BOOL CDialog::Create(UINT nIDTemplate, CWnd* pParentWnd) {
  return Create(MAKEINTRESOURCE(nIDTemplate), pParentWnd);
}

BOOL CDialog::CreateIndirect(LPCDLGTEMPLATE lpDialogTemplate, CWnd* pParentWnd,
                             void* lpDialogInit) {
  if (pParentWnd == NULL) pParentWnd = AfxGetMainWnd();
  m_lpDialogInit = lpDialogInit;
  return CreateDlgIndirect(lpDialogTemplate, pParentWnd, NULL);
}

BOOL CDialog::CreateIndirect(HGLOBAL hDialogTemplate, CWnd* pParentWnd) {
  LPCDLGTEMPLATE lpTemplate = (LPCDLGTEMPLATE)::GlobalLock(hDialogTemplate);
  BOOL bResult = CreateIndirect(lpTemplate, pParentWnd, NULL);
  ::GlobalUnlock(hDialogTemplate);
  return bResult;
}

// ---------------------------------------------------------------------
// Modal dialogs

BOOL CDialog::InitModalIndirect(LPCDLGTEMPLATE lpDialogTemplate, CWnd* pParentWnd,
                                void* lpDialogInit) {
  m_lpDialogTemplate = lpDialogTemplate;
  m_pParentWnd = pParentWnd;
  m_lpDialogInit = lpDialogInit;
  return TRUE;
}

BOOL CDialog::InitModalIndirect(HGLOBAL hDialogTemplate, CWnd* pParentWnd) {
  m_hDialogTemplate = hDialogTemplate;
  m_pParentWnd = pParentWnd;
  return TRUE;
}

HWND CDialog::PreModal() {
  HWND hWnd = _AfxGetSafeOwner(m_pParentWnd->GetSafeHwnd(), &m_hWndTop);
  AfxHookWindowCreate(this);
  return hWnd;
}

void CDialog::PostModal() {
  AfxUnhookWindowCreate();
  Detach();
  if (m_hWndTop != NULL && ::IsWindow(m_hWndTop)) ::EnableWindow(m_hWndTop, TRUE);
  m_hWndTop = NULL;
}

INT_PTR CDialog::DoModal() {
  LPCDLGTEMPLATE lpDialogTemplate = m_lpDialogTemplate;
  HINSTANCE hInst = AfxGetResourceHandle();
  if (m_lpszTemplateName != NULL) {
    lpDialogTemplate = _AfxLoadDialog(m_lpszTemplateName, &hInst);
  } else if (m_hDialogTemplate != NULL) {
    lpDialogTemplate = (LPCDLGTEMPLATE)::GlobalLock(m_hDialogTemplate);
  }
  if (lpDialogTemplate == NULL) return -1;

  HWND hWndParent = PreModal();
  BOOL bEnableParent = FALSE;
  if (hWndParent != NULL && hWndParent != ::GetDesktopWindow() && ::IsWindowEnabled(hWndParent)) {
    ::EnableWindow(hWndParent, FALSE);
    bEnableParent = TRUE;
  }

  m_bModal = TRUE;
  try {
    m_nModalResult = -1;
    m_nFlags |= WF_CONTINUEMODAL;
    if (CreateDlgIndirect(lpDialogTemplate, CWnd::FromHandle(hWndParent), hInst)) {
      if (m_nFlags & WF_CONTINUEMODAL) {
        DWORD dwFlags = MLF_SHOWONIDLE;
        if (GetStyle() & DS_NOIDLEMSG) dwFlags |= MLF_NOIDLEMSG;
        RunModalLoop(dwFlags);
      }
      // Hide the dialog before the parent comes back, to avoid flicker.
      if (m_hWnd != NULL)
        SetWindowPos(NULL, 0, 0, 0, 0,
                     SWP_HIDEWINDOW | SWP_NOSIZE | SWP_NOMOVE | SWP_NOACTIVATE | SWP_NOZORDER);
    }
  } catch (CException* e) {
    e->Delete();
    m_nModalResult = -1;
  }
  m_bModal = FALSE;

  if (bEnableParent) ::EnableWindow(hWndParent, TRUE);
  if (hWndParent != NULL && ::GetActiveWindow() == m_hWnd) ::SetActiveWindow(hWndParent);

  DestroyWindow();
  PostModal();

  if (m_hDialogTemplate != NULL && m_lpszTemplateName == NULL) ::GlobalUnlock(m_hDialogTemplate);
  return m_nModalResult;
}

void CDialog::EndDialog(int nResult) {
  if (m_nFlags & (WF_MODALLOOP | WF_CONTINUEMODAL)) EndModalLoop(nResult);
  ::EndDialog(m_hWnd, nResult);
}

// ---------------------------------------------------------------------
// Initialization and the standard buttons

LRESULT CDialog::HandleInitDialog(WPARAM, LPARAM) {
  PreInitDialog();
  // The default processing calls AfxDlgProc, which calls OnInitDialog.
  return Default();
}

LRESULT CDialog::HandleSetFont(WPARAM wParam, LPARAM) {
  OnSetFont(CFont::FromHandle((HFONT)wParam));
  return Default();
}

void CDialog::PreInitDialog() {}

INT_PTR CALLBACK AfxDlgProc(HWND hWnd, UINT message, WPARAM, LPARAM) {
  if (message == WM_INITDIALOG) {
    CDialog* pDlg = DYNAMIC_DOWNCAST(CDialog, CWnd::FromHandlePermanent(hWnd));
    if (pDlg != NULL) return pDlg->OnInitDialog();
    return 1;
  }
  return 0;
}

BOOL CDialog::OnInitDialog() {
  // Fill list and combo boxes from the RT_DLGINIT resource, then the
  // controls from the member variables.
  BOOL bDlgInit;
  if (m_lpDialogInit != NULL)
    bDlgInit = ExecuteDlgInit(m_lpDialogInit);
  else
    bDlgInit = ExecuteDlgInit(m_lpszTemplateName);
  if (!bDlgInit) {
    EndDialog(-1);
    return FALSE;
  }
  if (!UpdateData(FALSE)) {
    EndDialog(-1);
    return FALSE;
  }
  return TRUE;  // let Windows set the focus
}

void CDialog::OnOK() {
  if (!UpdateData(TRUE)) return;  // validation failed; stay open
  EndDialog(IDOK);
}

void CDialog::OnCancel() { EndDialog(IDCANCEL); }

BOOL CDialog::CheckAutoCenter() { return TRUE; }

void CDialog::OnPaint() { Default(); }

HBRUSH CDialog::OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor) {
  return CWnd::OnCtlColor(pDC, pWnd, nCtlColor);
}

LRESULT CDialog::OnCommandHelp(WPARAM, LPARAM lParam) {
  if (lParam == 0 && m_nIDHelp != 0) lParam = 0x20000 + m_nIDHelp;  // HID_BASE_RESOURCE
  if (lParam != 0) {
    CWinApp* pApp = AfxGetApp();
    if (pApp != NULL) pApp->WinHelp((DWORD_PTR)lParam);
    return TRUE;
  }
  return FALSE;
}

LRESULT CDialog::OnHelpHitTest(WPARAM, LPARAM) {
  return m_nIDHelp != 0 ? 0x20000 + m_nIDHelp : 0;
}

BOOL CDialog::PreTranslateMessage(MSG* pMsg) {
  // Keyboard and mouse input goes through IsDialogMessage, which handles
  // Tab, the arrow keys, Enter, Escape and mnemonics.
  if ((pMsg->message < WM_KEYFIRST || pMsg->message > WM_KEYLAST) &&
      (pMsg->message < WM_MOUSEFIRST || pMsg->message > WM_MOUSELAST))
    return FALSE;
  return ::IsDialogMessage(m_hWnd, pMsg);
}

BOOL CDialog::OnCmdMsg(UINT nID, int nCode, void* pExtra, AFX_CMDHANDLERINFO* pHandlerInfo) {
  if (CWnd::OnCmdMsg(nID, nCode, pExtra, pHandlerInfo)) return TRUE;

  // Commands the dialog does not handle go to the application.
  if ((nCode != CN_COMMAND && (UINT)nCode != CN_UPDATE_COMMAND_UI) || nID >= 0xF000)
    return FALSE;
  CWinThread* pThread = AfxGetThread();
  if (pThread != NULL && pThread->OnCmdMsg(nID, nCode, pExtra, pHandlerInfo)) return TRUE;
  return FALSE;
}
