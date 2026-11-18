// taskdlg.cpp - CTaskDialog, over comctl32's TaskDialogIndirect when it is
// there (Windows Vista and later, with comctl32 version 6), and a message
// box when it is not.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include "afximpl.h"
#include <afxtaskdialog.h>
#include <shellapi.h>

IMPLEMENT_DYNAMIC(CTaskDialog, CObject)

typedef HRESULT(WINAPI* _AfxTaskDialogIndirectProc)(const TASKDIALOGCONFIG*, int*, int*, BOOL*);

static _AfxTaskDialogIndirectProc _AfxGetTaskDialogIndirect() {
  HMODULE h = ::GetModuleHandle(_T("comctl32.dll"));
  if (h == NULL) h = ::LoadLibrary(_T("comctl32.dll"));
  return h != NULL ? (_AfxTaskDialogIndirectProc)(void*)::GetProcAddress(h, "TaskDialogIndirect")
                   : NULL;
}

BOOL PASCAL CTaskDialog::IsSupported() { return _AfxGetTaskDialogIndirect() != NULL; }

CTaskDialog::CTaskDialog(const CString& strContent, const CString& strMainInstruction,
                         const CString& strTitle, int nCommonButtons, int nTaskDialogOptions,
                         const CString& strFooter) {
  _Init(strContent, strMainInstruction, strTitle, nCommonButtons, nTaskDialogOptions,
        strFooter);
}

CTaskDialog::CTaskDialog(const CString& strContent, const CString& strMainInstruction,
                         const CString& strTitle, int nIDCommandControlsFirst,
                         int nIDCommandControlsLast, int nCommonButtons, int nTaskDialogOptions,
                         const CString& strFooter) {
  _Init(strContent, strMainInstruction, strTitle, nCommonButtons, nTaskDialogOptions,
        strFooter);
  LoadCommandControls(nIDCommandControlsFirst, nIDCommandControlsLast);
}

void CTaskDialog::_Init(const CString& strContent, const CString& strMainInstruction,
                        const CString& strTitle, int nCommonButtons, int nTaskDialogOptions,
                        const CString& strFooter) {
  m_hWnd = NULL;
  m_strContent = strContent;
  m_strMainInstruction = strMainInstruction;
  m_strTitle = strTitle;
  m_strFooter = strFooter;
  m_nCommonButton = nCommonButtons;
  m_nButtonDisabled = m_nButtonElevation = 0;
  m_nFlags = nTaskDialogOptions;
  m_nWidth = 0;
  m_nDefaultCommandControl = m_nDefaultRadioButton = 0;
  m_nButtonId = m_nRadioId = -1;
  m_bVerified = (nTaskDialogOptions & TDF_VERIFICATION_FLAG_CHECKED) != 0;
  m_nProgressRangeMin = 0;
  m_nProgressRangeMax = 100;
  m_nProgressPos = 0;
  m_nProgressState = PBST_NORMAL;
  m_bMarquee = FALSE;
  m_nMarqueeSpeed = 0;
  m_hMainIcon = NULL;
  m_hFooterIcon = NULL;
}

CTaskDialog::~CTaskDialog() {}

LRESULT CTaskDialog::_Send(UINT msg, WPARAM wParam, LPARAM lParam) const {
  return m_hWnd != NULL ? ::SendMessage(m_hWnd, msg, wParam, lParam) : 0;
}

// ---------------------------------------------------------------------
// Command controls and radio buttons

void CTaskDialog::AddCommandControl(int nCommandControlID, const CString& strCaption,
                                    BOOL bEnabled, BOOL bRequiresElevation) {
  ASSERT(m_hWnd == NULL);
  _Button b = {nCommandControlID, strCaption, bEnabled, bRequiresElevation};
  m_aButtons.push_back(b);
}

void CTaskDialog::AddRadioButton(int nRadioButtonID, const CString& strCaption, BOOL bEnabled) {
  ASSERT(m_hWnd == NULL);
  _Button b = {nRadioButtonID, strCaption, bEnabled, FALSE};
  m_aRadioButtons.push_back(b);
}

// One control for each string in the range the string table has.
void CTaskDialog::LoadCommandControls(int nIDCommandControlsFirst, int nIDCommandControlsLast) {
  for (int nID = nIDCommandControlsFirst; nID <= nIDCommandControlsLast; nID++) {
    CString str;
    if (str.LoadString(nID) && !str.IsEmpty()) AddCommandControl(nID, str);
  }
}

void CTaskDialog::LoadRadioButtons(int nIDRadioButtonsFirst, int nIDRadioButtonsLast) {
  for (int nID = nIDRadioButtonsFirst; nID <= nIDRadioButtonsLast; nID++) {
    CString str;
    if (str.LoadString(nID) && !str.IsEmpty()) AddRadioButton(nID, str);
  }
}

void CTaskDialog::RemoveAllCommandControls() {
  ASSERT(m_hWnd == NULL);
  m_aButtons.clear();
}

void CTaskDialog::RemoveAllRadioButtons() {
  ASSERT(m_hWnd == NULL);
  m_aRadioButtons.clear();
}

// The command control the user chose, or -1 when it was a common button.
int CTaskDialog::GetSelectedCommandControlID() const {
  for (size_t i = 0; i < m_aButtons.size(); i++)
    if (m_aButtons[i].nID == m_nButtonId) return m_nButtonId;
  return -1;
}

int CTaskDialog::GetSelectedRadioButtonID() const { return m_nRadioId; }

BOOL CTaskDialog::IsCommandControlEnabled(int nCommandControlID) const {
  for (size_t i = 0; i < m_aButtons.size(); i++)
    if (m_aButtons[i].nID == nCommandControlID) return m_aButtons[i].bEnabled;
  return FALSE;
}

BOOL CTaskDialog::IsRadioButtonEnabled(int nRadioButtonID) const {
  for (size_t i = 0; i < m_aRadioButtons.size(); i++)
    if (m_aRadioButtons[i].nID == nRadioButtonID) return m_aRadioButtons[i].bEnabled;
  return FALSE;
}

void CTaskDialog::SetCommandControlOptions(int nCommandControlID, BOOL bEnabled,
                                           BOOL bRequiresElevation) {
  for (size_t i = 0; i < m_aButtons.size(); i++) {
    if (m_aButtons[i].nID != nCommandControlID) continue;
    m_aButtons[i].bEnabled = bEnabled;
    m_aButtons[i].bElevation = bRequiresElevation;
    _Send(TDM_ENABLE_BUTTON, nCommandControlID, bEnabled);
    _Send(TDM_SET_BUTTON_ELEVATION_REQUIRED_STATE, nCommandControlID, bRequiresElevation);
  }
}

void CTaskDialog::SetRadioButtonOptions(int nRadioButtonID, BOOL bEnabled) {
  for (size_t i = 0; i < m_aRadioButtons.size(); i++) {
    if (m_aRadioButtons[i].nID != nRadioButtonID) continue;
    m_aRadioButtons[i].bEnabled = bEnabled;
    _Send(TDM_ENABLE_RADIO_BUTTON, nRadioButtonID, bEnabled);
  }
}

void CTaskDialog::SetDefaultCommandControl(int nCommandControlID) {
  ASSERT(m_hWnd == NULL);
  m_nDefaultCommandControl = nCommandControlID;
}

void CTaskDialog::SetDefaultRadioButton(int nRadioButtonID) {
  ASSERT(m_hWnd == NULL);
  m_nDefaultRadioButton = nRadioButtonID;
}

void CTaskDialog::ClickCommandControl(int nCommandControlID) const {
  _Send(TDM_CLICK_BUTTON, nCommandControlID);
}

void CTaskDialog::ClickRadioButton(int nRadioButtonID) const {
  _Send(TDM_CLICK_RADIO_BUTTON, nRadioButtonID);
}

// ---------------------------------------------------------------------
// Common buttons

static const struct {
  int nFlag;
  int nID;
} _afxCommonButtons[] = {{TDCBF_OK_BUTTON, IDOK},         {TDCBF_YES_BUTTON, IDYES},
                         {TDCBF_NO_BUTTON, IDNO},         {TDCBF_CANCEL_BUTTON, IDCANCEL},
                         {TDCBF_RETRY_BUTTON, IDRETRY},   {TDCBF_CLOSE_BUTTON, IDCLOSE}};

void CTaskDialog::SetCommonButtons(int nButtonMask, int nDisabledButtonMask,
                                   int nElevationButtonMask) {
  ASSERT(m_hWnd == NULL);
  m_nCommonButton = nButtonMask;
  m_nButtonDisabled = nDisabledButtonMask;
  m_nButtonElevation = nElevationButtonMask;
}

void CTaskDialog::SetCommonButtonOptions(int nDisabledButtonMask, int nElevationButtonMask) {
  m_nButtonDisabled = nDisabledButtonMask;
  m_nButtonElevation = nElevationButtonMask;
  if (m_hWnd == NULL) return;
  for (const auto& b : _afxCommonButtons) {
    if (!(m_nCommonButton & b.nFlag)) continue;
    _Send(TDM_ENABLE_BUTTON, b.nID, !(nDisabledButtonMask & b.nFlag));
    _Send(TDM_SET_BUTTON_ELEVATION_REQUIRED_STATE, b.nID, (nElevationButtonMask & b.nFlag) != 0);
  }
}

// ---------------------------------------------------------------------
// Text, icons, the check box, the progress bar

void CTaskDialog::SetWindowTitle(const CString& strWindowTitle) {
  ASSERT(m_hWnd == NULL);
  m_strTitle = strWindowTitle;
}

static void _AfxSetElementText(HWND hWnd, CStringW& w, const CString& str, int nElement) {
  if (hWnd == NULL) return;
  w = CStringW(str);
  ::SendMessage(hWnd, TDM_SET_ELEMENT_TEXT, nElement, (LPARAM)(LPCWSTR)w);
}

void CTaskDialog::SetMainInstruction(const CString& strInstructions) {
  m_strMainInstruction = strInstructions;
  _AfxSetElementText(m_hWnd, m_w[1], strInstructions, TDE_MAIN_INSTRUCTION);
}

void CTaskDialog::SetContent(const CString& strContent) {
  m_strContent = strContent;
  _AfxSetElementText(m_hWnd, m_w[2], strContent, TDE_CONTENT);
}

void CTaskDialog::SetFooterText(const CString& strFooterText) {
  m_strFooter = strFooterText;
  _AfxSetElementText(m_hWnd, m_w[3], strFooterText, TDE_FOOTER);
}

void CTaskDialog::SetExpansionArea(const CString& strExpandedInformation,
                                   const CString& strCollapsedLabel,
                                   const CString& strExpandedLabel) {
  m_strInformation = strExpandedInformation;
  m_strCollapse = strCollapsedLabel;
  m_strExpand = strExpandedLabel;
  _AfxSetElementText(m_hWnd, m_w[5], strExpandedInformation, TDE_EXPANDED_INFORMATION);
}

void CTaskDialog::SetMainIcon(HICON hMainIcon) {
  ASSERT(m_hWnd == NULL || (m_nFlags & TDF_USE_HICON_MAIN));
  m_hMainIcon = hMainIcon;
  m_nFlags |= TDF_USE_HICON_MAIN;
  _Send(TDM_UPDATE_ICON, TDIE_ICON_MAIN, (LPARAM)hMainIcon);
}

void CTaskDialog::SetMainIcon(LPCWSTR lpszMainIcon) {
  ASSERT(m_hWnd == NULL || !(m_nFlags & TDF_USE_HICON_MAIN));
  m_lpszMainIcon = lpszMainIcon;
  m_nFlags &= ~TDF_USE_HICON_MAIN;
  _Send(TDM_UPDATE_ICON, TDIE_ICON_MAIN, (LPARAM)lpszMainIcon);
}

void CTaskDialog::SetFooterIcon(HICON hFooterIcon) {
  ASSERT(m_hWnd == NULL || (m_nFlags & TDF_USE_HICON_FOOTER));
  m_hFooterIcon = hFooterIcon;
  m_nFlags |= TDF_USE_HICON_FOOTER;
  _Send(TDM_UPDATE_ICON, TDIE_ICON_FOOTER, (LPARAM)hFooterIcon);
}

void CTaskDialog::SetFooterIcon(LPCWSTR lpszFooterIcon) {
  ASSERT(m_hWnd == NULL || !(m_nFlags & TDF_USE_HICON_FOOTER));
  m_lpszFooterIcon = lpszFooterIcon;
  m_nFlags &= ~TDF_USE_HICON_FOOTER;
  _Send(TDM_UPDATE_ICON, TDIE_ICON_FOOTER, (LPARAM)lpszFooterIcon);
}

void CTaskDialog::SetDialogWidth(int nWidth) {
  ASSERT(m_hWnd == NULL);
  m_nWidth = nWidth;
}

void CTaskDialog::SetVerificationCheckboxText(const CString& strVerificationText) {
  ASSERT(m_hWnd == NULL);
  m_strVerification = strVerificationText;
}

void CTaskDialog::SetVerificationCheckbox(BOOL bChecked) {
  m_bVerified = bChecked;
  if (bChecked)
    m_nFlags |= TDF_VERIFICATION_FLAG_CHECKED;
  else
    m_nFlags &= ~TDF_VERIFICATION_FLAG_CHECKED;
  _Send(TDM_CLICK_VERIFICATION, bChecked, FALSE);
}

BOOL CTaskDialog::GetVerificationCheckboxState() const { return m_bVerified; }

void CTaskDialog::SetProgressBarMarquee(BOOL bEnabled, int nMarqueeSpeed) {
  m_bMarquee = bEnabled;
  m_nMarqueeSpeed = nMarqueeSpeed;
  _Send(TDM_SET_MARQUEE_PROGRESS_BAR, bEnabled);
  _Send(TDM_SET_PROGRESS_BAR_MARQUEE, bEnabled, nMarqueeSpeed);
}

void CTaskDialog::SetProgressBarPosition(int nProgressPos) {
  m_nProgressPos = nProgressPos;
  _Send(TDM_SET_PROGRESS_BAR_POS, nProgressPos);
}

void CTaskDialog::SetProgressBarRange(int nRangeMin, int nRangeMax) {
  m_nProgressRangeMin = nRangeMin;
  m_nProgressRangeMax = nRangeMax;
  _Send(TDM_SET_PROGRESS_BAR_RANGE, 0, MAKELPARAM(nRangeMin, nRangeMax));
}

void CTaskDialog::SetProgressBarState(int nState) {
  m_nProgressState = nState;
  _Send(TDM_SET_PROGRESS_BAR_STATE, nState);
}

void CTaskDialog::SetOptions(int nOptionFlag) {
  ASSERT(m_hWnd == NULL);
  m_nFlags = nOptionFlag;
}

// ---------------------------------------------------------------------
// Running it

void CTaskDialog::_FillConfig(TASKDIALOGCONFIG& config, HWND hParent) const {
  memset(&config, 0, sizeof(config));
  config.cbSize = sizeof(config);
  config.hwndParent = hParent;
  config.hInstance = AfxGetResourceHandle();
  config.dwFlags = m_nFlags;
  config.dwCommonButtons = m_nCommonButton;
  const CString* strs[] = {&m_strTitle,       &m_strMainInstruction, &m_strContent,
                           &m_strFooter,      &m_strVerification,    &m_strInformation,
                           &m_strCollapse,    &m_strExpand};
  for (int i = 0; i < 8; i++) m_w[i] = CStringW(*strs[i]);
  auto text = [](const CStringW& w) -> PCWSTR { return w.IsEmpty() ? NULL : (LPCWSTR)w; };
  config.pszWindowTitle = text(m_w[0]);
  config.pszMainInstruction = text(m_w[1]);
  config.pszContent = text(m_w[2]);
  config.pszFooter = text(m_w[3]);
  config.pszVerificationText = text(m_w[4]);
  config.pszExpandedInformation = text(m_w[5]);
  config.pszCollapsedControlText = text(m_w[6]);
  config.pszExpandedControlText = text(m_w[7]);
  if (m_nFlags & TDF_USE_HICON_MAIN)
    config.hMainIcon = m_hMainIcon;
  else
    config.pszMainIcon = m_lpszMainIcon;
  if (m_nFlags & TDF_USE_HICON_FOOTER)
    config.hFooterIcon = m_hFooterIcon;
  else
    config.pszFooterIcon = m_lpszFooterIcon;

  // Copy the buttons' captions first, then point at them.
  m_wButtons.resize(m_aButtons.size());
  m_tdButtons.resize(m_aButtons.size());
  for (size_t i = 0; i < m_aButtons.size(); i++) m_wButtons[i] = CStringW(m_aButtons[i].strCaption);
  for (size_t i = 0; i < m_aButtons.size(); i++) {
    m_tdButtons[i].nButtonID = m_aButtons[i].nID;
    m_tdButtons[i].pszButtonText = m_wButtons[i];
  }
  m_wRadios.resize(m_aRadioButtons.size());
  m_tdRadios.resize(m_aRadioButtons.size());
  for (size_t i = 0; i < m_aRadioButtons.size(); i++)
    m_wRadios[i] = CStringW(m_aRadioButtons[i].strCaption);
  for (size_t i = 0; i < m_aRadioButtons.size(); i++) {
    m_tdRadios[i].nButtonID = m_aRadioButtons[i].nID;
    m_tdRadios[i].pszButtonText = m_wRadios[i];
  }
  config.cButtons = (UINT)m_tdButtons.size();
  config.pButtons = config.cButtons ? m_tdButtons.data() : NULL;
  config.nDefaultButton = m_nDefaultCommandControl;
  config.cRadioButtons = (UINT)m_tdRadios.size();
  config.pRadioButtons = config.cRadioButtons ? m_tdRadios.data() : NULL;
  config.nDefaultRadioButton = m_nDefaultRadioButton;
  config.pfCallback = _Callback;
  config.lpCallbackData = (LONG_PTR)this;
  config.cxWidth = m_nWidth;
}

// What the configuration cannot say: disabled and elevated buttons, and
// the progress bar's settings.
void CTaskDialog::_ApplyState() const {
  for (size_t i = 0; i < m_aButtons.size(); i++) {
    if (!m_aButtons[i].bEnabled) _Send(TDM_ENABLE_BUTTON, m_aButtons[i].nID, FALSE);
    if (m_aButtons[i].bElevation)
      _Send(TDM_SET_BUTTON_ELEVATION_REQUIRED_STATE, m_aButtons[i].nID, TRUE);
  }
  for (size_t i = 0; i < m_aRadioButtons.size(); i++)
    if (!m_aRadioButtons[i].bEnabled) _Send(TDM_ENABLE_RADIO_BUTTON, m_aRadioButtons[i].nID, FALSE);
  for (const auto& b : _afxCommonButtons) {
    if (!(m_nCommonButton & b.nFlag)) continue;
    if (m_nButtonDisabled & b.nFlag) _Send(TDM_ENABLE_BUTTON, b.nID, FALSE);
    if (m_nButtonElevation & b.nFlag) _Send(TDM_SET_BUTTON_ELEVATION_REQUIRED_STATE, b.nID, TRUE);
  }
  if (m_nFlags & TDF_SHOW_MARQUEE_PROGRESS_BAR) {
    if (m_bMarquee) _Send(TDM_SET_PROGRESS_BAR_MARQUEE, TRUE, m_nMarqueeSpeed);
  } else if (m_nFlags & TDF_SHOW_PROGRESS_BAR) {
    _Send(TDM_SET_PROGRESS_BAR_RANGE, 0, MAKELPARAM(m_nProgressRangeMin, m_nProgressRangeMax));
    _Send(TDM_SET_PROGRESS_BAR_POS, m_nProgressPos);
    if (m_nProgressState != PBST_NORMAL) _Send(TDM_SET_PROGRESS_BAR_STATE, m_nProgressState);
  }
}

HRESULT CALLBACK CTaskDialog::_Callback(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
                                        LONG_PTR lpRefData) {
  CTaskDialog* p = (CTaskDialog*)lpRefData;
  if (p == NULL) return S_OK;
  switch (msg) {
  case TDN_DIALOG_CONSTRUCTED:
    p->m_hWnd = hwnd;
    p->_ApplyState();
    return p->OnInit();
  case TDN_CREATED:
    p->m_hWnd = hwnd;
    return p->OnCreate();
  case TDN_NAVIGATED:
    p->m_hWnd = hwnd;
    p->_ApplyState();
    return p->OnNavigatePage();
  case TDN_BUTTON_CLICKED: {
    HRESULT hr = p->OnCommandControlClick((int)wParam);
    if (hr == S_OK) p->m_nButtonId = (int)wParam;
    return hr;
  }
  case TDN_RADIO_BUTTON_CLICKED:
    p->m_nRadioId = (int)wParam;
    return p->OnRadioButtonClick((int)wParam);
  case TDN_VERIFICATION_CLICKED:
    p->m_bVerified = (BOOL)wParam;
    return p->OnVerificationCheckboxClick((BOOL)wParam);
  case TDN_EXPANDO_BUTTON_CLICKED: return p->OnExpandButtonClick((BOOL)wParam);
  case TDN_HYPERLINK_CLICKED: return p->OnHyperlinkClick(CString((LPCWSTR)lParam));
  case TDN_HELP: return p->OnHelp();
  case TDN_TIMER: return p->OnTimer((long)wParam);
  case TDN_DESTROYED: {
    HRESULT hr = p->OnDestroy();
    p->m_hWnd = NULL;
    return hr;
  }
  }
  return S_OK;
}

INT_PTR CTaskDialog::DoModal(HWND hParent) {
  _AfxTaskDialogIndirectProc pfn = _AfxGetTaskDialogIndirect();
  if (pfn == NULL) return _MessageBoxFallback(hParent);
  TASKDIALOGCONFIG config;
  _FillConfig(config, hParent);
  int nButton = 0, nRadio = 0;
  BOOL bVerified = m_bVerified;
  m_nButtonId = m_nRadioId = -1;
  HRESULT hr = pfn(&config, &nButton, &nRadio, &bVerified);
  m_hWnd = NULL;
  if (FAILED(hr)) return -1;
  m_nButtonId = nButton;
  // The radio button last clicked, as the notifications said (some
  // versions of comctl32 give back the first one), or the default one.
  if (m_nRadioId == -1) m_nRadioId = m_aRadioButtons.size() ? nRadio : -1;
  m_bVerified = bVerified;
  return nButton;
}

// Without a task dialog: the same text in a message box, with the common
// buttons it can show (command controls and radio buttons cannot be).
INT_PTR CTaskDialog::_MessageBoxFallback(HWND hParent) {
  CString strText = m_strMainInstruction;
  if (!m_strContent.IsEmpty()) {
    if (!strText.IsEmpty()) strText += _T("\n\n");
    strText += m_strContent;
  }
  if (!m_strFooter.IsEmpty()) strText += _T("\n\n") + m_strFooter;
  int b = m_nCommonButton;
  UINT nType = MB_OK;
  if ((b & TDCBF_YES_BUTTON) && (b & TDCBF_NO_BUTTON))
    nType = (b & TDCBF_CANCEL_BUTTON) ? MB_YESNOCANCEL : MB_YESNO;
  else if ((b & TDCBF_RETRY_BUTTON) && (b & TDCBF_CANCEL_BUTTON))
    nType = MB_RETRYCANCEL;
  else if ((b & TDCBF_OK_BUTTON) && (b & TDCBF_CANCEL_BUTTON))
    nType = MB_OKCANCEL;
  if (m_nFlags & TDF_USE_HICON_MAIN) {
  } else if (m_lpszMainIcon == TD_WARNING_ICON) {
    nType |= MB_ICONWARNING;
  } else if (m_lpszMainIcon == TD_ERROR_ICON) {
    nType |= MB_ICONERROR;
  } else if (m_lpszMainIcon == TD_INFORMATION_ICON) {
    nType |= MB_ICONINFORMATION;
  }
  int nResult = ::MessageBox(hParent, strText, m_strTitle.IsEmpty() ? NULL : (LPCTSTR)m_strTitle,
                             nType);
  m_nButtonId = nResult;
  m_nRadioId = -1;
  return nResult;
}

void CTaskDialog::NavigateTo(CTaskDialog& oTaskDialog) const {
  if (m_hWnd == NULL) return;
  TASKDIALOGCONFIG config;
  oTaskDialog._FillConfig(config, ::GetParent(m_hWnd));
  ::SendMessage(m_hWnd, TDM_NAVIGATE_PAGE, 0, (LPARAM)&config);
}

INT_PTR PASCAL CTaskDialog::ShowDialog(const CString& strContent,
                                       const CString& strMainInstruction,
                                       const CString& strTitle, int nIDCommandControlsFirst,
                                       int nIDCommandControlsLast, int nCommonButtons,
                                       int nTaskDialogOptions, const CString& strFooter) {
  CTaskDialog dlg(strContent, strMainInstruction, strTitle, nIDCommandControlsFirst,
                  nIDCommandControlsLast, nCommonButtons, nTaskDialogOptions, strFooter);
  return dlg.DoModal();
}

// ---------------------------------------------------------------------
// The default notifications

HRESULT CTaskDialog::OnCreate() { return S_OK; }
HRESULT CTaskDialog::OnInit() { return S_OK; }
HRESULT CTaskDialog::OnDestroy() { return S_OK; }
HRESULT CTaskDialog::OnCommandControlClick(int) { return S_OK; }
HRESULT CTaskDialog::OnRadioButtonClick(int) { return S_OK; }
HRESULT CTaskDialog::OnVerificationCheckboxClick(BOOL) { return S_OK; }
HRESULT CTaskDialog::OnExpandButtonClick(BOOL) { return S_OK; }
HRESULT CTaskDialog::OnHelp() { return S_OK; }
HRESULT CTaskDialog::OnNavigatePage() { return S_OK; }
HRESULT CTaskDialog::OnTimer(long) { return S_OK; }

// A link is opened with the program the shell has for it.
HRESULT CTaskDialog::OnHyperlinkClick(const CString& strHref) {
  ::ShellExecute(m_hWnd, NULL, strHref, NULL, NULL, SW_SHOW);
  return S_OK;
}
