// afxtaskdialog.h - CTaskDialog, the Windows Vista task dialog (comctl32
// version 6's TaskDialogIndirect): a main instruction, content, command
// links, radio buttons, a verification check box, an expandable area, a
// footer and a progress bar.
//
// TaskDialogIndirect is found when the dialog runs, so a program that
// uses CTaskDialog still starts on Windows 2000 and XP. IsSupported says
// whether the task dialog is there; where it is not, DoModal shows a
// message box with the same text and the common buttons instead.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#ifndef __AFXTASKDIALOG_H__
#define __AFXTASKDIALOG_H__

#ifndef __AFXWIN_H__
#include <afxwin.h>
#endif
#include <vector>
#include <commctrl.h>

// The task dialog's declarations, for programs built for systems before
// Windows Vista (whose commctrl.h leaves them out).
#if defined(_WINPORT) || !defined(NTDDI_VERSION) || NTDDI_VERSION < 0x06000000 || defined(NOTASKDIALOG)
#pragma pack(push, 1)
typedef HRESULT(CALLBACK* PFTASKDIALOGCALLBACK)(HWND hwnd, UINT msg, WPARAM wParam,
                                                LPARAM lParam, LONG_PTR lpRefData);
enum _TASKDIALOG_FLAGS {
  TDF_ENABLE_HYPERLINKS = 0x1,
  TDF_USE_HICON_MAIN = 0x2,
  TDF_USE_HICON_FOOTER = 0x4,
  TDF_ALLOW_DIALOG_CANCELLATION = 0x8,
  TDF_USE_COMMAND_LINKS = 0x10,
  TDF_USE_COMMAND_LINKS_NO_ICON = 0x20,
  TDF_EXPAND_FOOTER_AREA = 0x40,
  TDF_EXPANDED_BY_DEFAULT = 0x80,
  TDF_VERIFICATION_FLAG_CHECKED = 0x100,
  TDF_SHOW_PROGRESS_BAR = 0x200,
  TDF_SHOW_MARQUEE_PROGRESS_BAR = 0x400,
  TDF_CALLBACK_TIMER = 0x800,
  TDF_POSITION_RELATIVE_TO_WINDOW = 0x1000,
  TDF_RTL_LAYOUT = 0x2000,
  TDF_NO_DEFAULT_RADIO_BUTTON = 0x4000,
  TDF_CAN_BE_MINIMIZED = 0x8000,
  TDF_SIZE_TO_CONTENT = 0x1000000
};
typedef int TASKDIALOG_FLAGS;
typedef enum _TASKDIALOG_MESSAGES {
  TDM_NAVIGATE_PAGE = WM_USER + 101,
  TDM_CLICK_BUTTON = WM_USER + 102,
  TDM_SET_MARQUEE_PROGRESS_BAR = WM_USER + 103,
  TDM_SET_PROGRESS_BAR_STATE = WM_USER + 104,
  TDM_SET_PROGRESS_BAR_RANGE = WM_USER + 105,
  TDM_SET_PROGRESS_BAR_POS = WM_USER + 106,
  TDM_SET_PROGRESS_BAR_MARQUEE = WM_USER + 107,
  TDM_SET_ELEMENT_TEXT = WM_USER + 108,
  TDM_CLICK_RADIO_BUTTON = WM_USER + 110,
  TDM_ENABLE_BUTTON = WM_USER + 111,
  TDM_ENABLE_RADIO_BUTTON = WM_USER + 112,
  TDM_CLICK_VERIFICATION = WM_USER + 113,
  TDM_UPDATE_ELEMENT_TEXT = WM_USER + 114,
  TDM_SET_BUTTON_ELEVATION_REQUIRED_STATE = WM_USER + 115,
  TDM_UPDATE_ICON = WM_USER + 116
} TASKDIALOG_MESSAGES;
typedef enum _TASKDIALOG_NOTIFICATIONS {
  TDN_CREATED = 0,
  TDN_NAVIGATED = 1,
  TDN_BUTTON_CLICKED = 2,
  TDN_HYPERLINK_CLICKED = 3,
  TDN_TIMER = 4,
  TDN_DESTROYED = 5,
  TDN_RADIO_BUTTON_CLICKED = 6,
  TDN_DIALOG_CONSTRUCTED = 7,
  TDN_VERIFICATION_CLICKED = 8,
  TDN_HELP = 9,
  TDN_EXPANDO_BUTTON_CLICKED = 10
} TASKDIALOG_NOTIFICATIONS;
typedef struct _TASKDIALOG_BUTTON {
  int nButtonID;
  PCWSTR pszButtonText;
} TASKDIALOG_BUTTON;
typedef enum _TASKDIALOG_ELEMENTS {
  TDE_CONTENT,
  TDE_EXPANDED_INFORMATION,
  TDE_FOOTER,
  TDE_MAIN_INSTRUCTION
} TASKDIALOG_ELEMENTS;
typedef enum _TASKDIALOG_ICON_ELEMENTS { TDIE_ICON_MAIN, TDIE_ICON_FOOTER } TASKDIALOG_ICON_ELEMENTS;
#define TD_WARNING_ICON MAKEINTRESOURCEW(-1)
#define TD_ERROR_ICON MAKEINTRESOURCEW(-2)
#define TD_INFORMATION_ICON MAKEINTRESOURCEW(-3)
#define TD_SHIELD_ICON MAKEINTRESOURCEW(-4)
enum _TASKDIALOG_COMMON_BUTTON_FLAGS {
  TDCBF_OK_BUTTON = 0x1,
  TDCBF_YES_BUTTON = 0x2,
  TDCBF_NO_BUTTON = 0x4,
  TDCBF_CANCEL_BUTTON = 0x8,
  TDCBF_RETRY_BUTTON = 0x10,
  TDCBF_CLOSE_BUTTON = 0x20
};
typedef int TASKDIALOG_COMMON_BUTTON_FLAGS;
typedef struct _TASKDIALOGCONFIG {
  UINT cbSize;
  HWND hwndParent;
  HINSTANCE hInstance;
  TASKDIALOG_FLAGS dwFlags;
  TASKDIALOG_COMMON_BUTTON_FLAGS dwCommonButtons;
  PCWSTR pszWindowTitle;
  union {
    HICON hMainIcon;
    PCWSTR pszMainIcon;
  };
  PCWSTR pszMainInstruction;
  PCWSTR pszContent;
  UINT cButtons;
  const TASKDIALOG_BUTTON* pButtons;
  int nDefaultButton;
  UINT cRadioButtons;
  const TASKDIALOG_BUTTON* pRadioButtons;
  int nDefaultRadioButton;
  PCWSTR pszVerificationText;
  PCWSTR pszExpandedInformation;
  PCWSTR pszExpandedControlText;
  PCWSTR pszCollapsedControlText;
  union {
    HICON hFooterIcon;
    PCWSTR pszFooterIcon;
  };
  PCWSTR pszFooter;
  PFTASKDIALOGCALLBACK pfCallback;
  LONG_PTR lpCallbackData;
  UINT cxWidth;
} TASKDIALOGCONFIG;
#pragma pack(pop)
#endif

#ifndef PBST_NORMAL
#define PBST_NORMAL 0x0001
#define PBST_ERROR 0x0002
#define PBST_PAUSED 0x0003
#endif
#ifndef IDCLOSE
#define IDCLOSE 8
#endif

class CTaskDialog : public CObject {
  DECLARE_DYNAMIC(CTaskDialog)
public:
  CTaskDialog(const CString& strContent, const CString& strMainInstruction,
              const CString& strTitle,
              int nCommonButtons = TDCBF_OK_BUTTON | TDCBF_CANCEL_BUTTON,
              int nTaskDialogOptions = TDF_ENABLE_HYPERLINKS | TDF_USE_COMMAND_LINKS,
              const CString& strFooter = CString());
  CTaskDialog(const CString& strContent, const CString& strMainInstruction,
              const CString& strTitle, int nIDCommandControlsFirst, int nIDCommandControlsLast,
              int nCommonButtons,
              int nTaskDialogOptions = TDF_ENABLE_HYPERLINKS | TDF_USE_COMMAND_LINKS,
              const CString& strFooter = CString());
  virtual ~CTaskDialog();

  static BOOL PASCAL IsSupported();
  static INT_PTR PASCAL ShowDialog(const CString& strContent, const CString& strMainInstruction,
                                   const CString& strTitle, int nIDCommandControlsFirst,
                                   int nIDCommandControlsLast,
                                   int nCommonButtons = TDCBF_YES_BUTTON | TDCBF_NO_BUTTON,
                                   int nTaskDialogOptions = TDF_ENABLE_HYPERLINKS |
                                                            TDF_USE_COMMAND_LINKS,
                                   const CString& strFooter = CString());

  virtual INT_PTR DoModal(HWND hParent = ::GetActiveWindow());
  void NavigateTo(CTaskDialog& oTaskDialog) const;

  // The command controls (buttons, or command links) and radio buttons.
  void AddCommandControl(int nCommandControlID, const CString& strCaption, BOOL bEnabled = TRUE,
                         BOOL bRequiresElevation = FALSE);
  void AddRadioButton(int nRadioButtonID, const CString& strCaption, BOOL bEnabled = TRUE);
  void LoadCommandControls(int nIDCommandControlsFirst, int nIDCommandControlsLast);
  void LoadRadioButtons(int nIDRadioButtonsFirst, int nIDRadioButtonsLast);
  void RemoveAllCommandControls();
  void RemoveAllRadioButtons();
  int GetSelectedCommandControlID() const;
  int GetSelectedRadioButtonID() const;
  BOOL IsCommandControlEnabled(int nCommandControlID) const;
  BOOL IsRadioButtonEnabled(int nRadioButtonID) const;
  void SetCommandControlOptions(int nCommandControlID, BOOL bEnabled,
                                BOOL bRequiresElevation = FALSE);
  void SetRadioButtonOptions(int nRadioButtonID, BOOL bEnabled);
  void SetDefaultCommandControl(int nCommandControlID);
  void SetDefaultRadioButton(int nRadioButtonID);
  void ClickCommandControl(int nCommandControlID) const;
  void ClickRadioButton(int nRadioButtonID) const;

  // The common buttons (OK, Yes, No, Cancel, Retry, Close).
  void SetCommonButtons(int nButtonMask, int nDisabledButtonMask = 0,
                        int nElevationButtonMask = 0);
  void SetCommonButtonOptions(int nDisabledButtonMask, int nElevationButtonMask = 0);

  // The text and icons.
  void SetWindowTitle(const CString& strWindowTitle);
  void SetMainInstruction(const CString& strInstructions);
  void SetContent(const CString& strContent);
  void SetFooterText(const CString& strFooterText);
  void SetExpansionArea(const CString& strExpandedInformation,
                        const CString& strCollapsedLabel = CString(),
                        const CString& strExpandedLabel = CString());
  void SetMainIcon(HICON hMainIcon);
  void SetMainIcon(LPCWSTR lpszMainIcon);
  void SetFooterIcon(HICON hFooterIcon);
  void SetFooterIcon(LPCWSTR lpszFooterIcon);
  void SetDialogWidth(int nWidth = 0);

  // The verification check box.
  void SetVerificationCheckboxText(const CString& strVerificationText);
  void SetVerificationCheckbox(BOOL bChecked);
  BOOL GetVerificationCheckboxState() const;

  // The progress bar.
  void SetProgressBarMarquee(BOOL bEnabled = TRUE, int nMarqueeSpeed = 0);
  void SetProgressBarPosition(int nProgressPos);
  void SetProgressBarRange(int nRangeMin, int nRangeMax);
  void SetProgressBarState(int nState = PBST_NORMAL);

  void SetOptions(int nOptionFlag);
  int GetOptions() const { return m_nFlags; }

  HWND m_hWnd;  // the dialog while it runs

protected:
  // Notifications, while the dialog runs. OnCommandControlClick returns
  // S_OK to close the dialog, S_FALSE to keep it open.
  virtual HRESULT OnCreate();
  virtual HRESULT OnInit();
  virtual HRESULT OnDestroy();
  virtual HRESULT OnCommandControlClick(int nCommandControlID);
  virtual HRESULT OnRadioButtonClick(int nRadioButtonID);
  virtual HRESULT OnVerificationCheckboxClick(BOOL bChecked);
  virtual HRESULT OnExpandButtonClick(BOOL bExpanded);
  virtual HRESULT OnHyperlinkClick(const CString& strHref);
  virtual HRESULT OnHelp();
  virtual HRESULT OnNavigatePage();
  virtual HRESULT OnTimer(long lTime);

  struct _Button {
    int nID;
    CString strCaption;
    BOOL bEnabled;
    BOOL bElevation;
  };
  std::vector<_Button> m_aButtons;
  std::vector<_Button> m_aRadioButtons;

  CString m_strTitle, m_strMainInstruction, m_strContent, m_strFooter;
  CString m_strVerification, m_strInformation, m_strCollapse, m_strExpand;
  int m_nCommonButton, m_nButtonDisabled, m_nButtonElevation;
  int m_nFlags, m_nWidth;
  int m_nDefaultCommandControl, m_nDefaultRadioButton;
  int m_nButtonId, m_nRadioId;
  BOOL m_bVerified;
  int m_nProgressRangeMin, m_nProgressRangeMax, m_nProgressPos, m_nProgressState;
  BOOL m_bMarquee;
  int m_nMarqueeSpeed;
  union {
    HICON m_hMainIcon;
    LPCWSTR m_lpszMainIcon;
  };
  union {
    HICON m_hFooterIcon;
    LPCWSTR m_lpszFooterIcon;
  };

private:
  void _Init(const CString& strContent, const CString& strMainInstruction,
             const CString& strTitle, int nCommonButtons, int nTaskDialogOptions,
             const CString& strFooter);
  void _FillConfig(TASKDIALOGCONFIG& config, HWND hParent) const;
  void _ApplyState() const;
  INT_PTR _MessageBoxFallback(HWND hParent);
  static HRESULT CALLBACK _Callback(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
                                    LONG_PTR lpRefData);
  LRESULT _Send(UINT msg, WPARAM wParam = 0, LPARAM lParam = 0) const;

  // The wide strings the configuration points to, kept while it runs.
  mutable CStringW m_w[9];
  mutable std::vector<CStringW> m_wButtons, m_wRadios;
  mutable std::vector<TASKDIALOG_BUTTON> m_tdButtons, m_tdRadios;
};

#endif  // __AFXTASKDIALOG_H__
