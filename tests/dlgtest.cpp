// dlgtest.cpp - a dialog-based program: dialog data exchange, controls,
// common controls and a property sheet.
#include <afxwin.h>
#include <afxext.h>
#include <afxdlgs.h>
#include <afxcmn.h>
#include <afxdialogex.h>
#include "dlgtest.h"
#include "testutil.h"

// ---------------------------------------------------------------------
// Property sheet

class CPage1 : public CPropertyPage {
public:
  CString m_strText;
  CPage1() : CPropertyPage(IDD_PAGE1), m_strText(_T("page one")) {}
  virtual void DoDataExchange(CDataExchange* pDX) {
    CPropertyPage::DoDataExchange(pDX);
    DDX_Text(pDX, IDC_PAGE_EDIT, m_strText);
  }
  virtual BOOL OnInitDialog() {
    CPropertyPage::OnInitDialog();
    SetTimer(1, 200, NULL);
    m_nStep = 0;
    return TRUE;
  }
  afx_msg void OnTimer(UINT_PTR) {
    CPropertySheet* pSheet = (CPropertySheet*)GetParent();
    switch (m_nStep++) {
      case 0: {
        CString str;
        GetDlgItemText(IDC_PAGE_EDIT, str);
        CHECK(str == _T("page one"));
        CHECK(pSheet->GetPageCount() == 2);
        CHECK(pSheet->GetActiveIndex() == 0);
        SetDlgItemText(IDC_PAGE_EDIT, _T("changed"));
        SetModified(TRUE);
        TestSaveWindowBitmap(pSheet->m_hWnd, _T("dlgtest-sheet.bmp"));
        pSheet->SetActivePage(1);
        break;
      }
      case 1:
        CHECK(pSheet->GetActiveIndex() == 1);
        pSheet->GetActivePage()->CheckDlgButton(IDC_PAGE_CHECK, BST_CHECKED);
        pSheet->PressButton(PSBTN_OK);
        KillTimer(1);
        break;
    }
  }
  int m_nStep;
  DECLARE_MESSAGE_MAP()
};

BEGIN_MESSAGE_MAP(CPage1, CPropertyPage)
  ON_WM_TIMER()
END_MESSAGE_MAP()

class CPage2 : public CPropertyPage {
public:
  BOOL m_bOption;
  CPage2() : CPropertyPage(IDD_PAGE2), m_bOption(FALSE) {}
  virtual void DoDataExchange(CDataExchange* pDX) {
    CPropertyPage::DoDataExchange(pDX);
    DDX_Check(pDX, IDC_PAGE_CHECK, m_bOption);
  }
};

static void TestPropertySheet() {
  CPropertySheet sheet(_T("Properties"));
  CPage1 page1;
  CPage2 page2;
  sheet.AddPage(&page1);
  sheet.AddPage(&page2);
  INT_PTR nResult = sheet.DoModal();
  CHECK(nResult == IDOK);
  CHECK(page1.m_strText == _T("changed"));
  CHECK(page2.m_bOption == TRUE);
  TestLog("property sheet: result %d, text '%s', option %d", (int)nResult,
          (LPCSTR)CStringA(page1.m_strText), page2.m_bOption);
}


// A Wizard97 wizard: CPropertySheetEx with a watermark and a header
// bitmap, and CPropertyPageEx pages with header titles; and the same
// through MFC 7's CPropertyPage constructor.
class CWizPage : public CPropertyPageEx {
public:
  int m_nIndex;
  int m_nActivated;
  CWizPage(UINT nIDTemplate, int nIndex, UINT nTitle, UINT nSubTitle)
      : CPropertyPageEx(nIDTemplate, 0, nTitle, nSubTitle), m_nIndex(nIndex), m_nActivated(0) {}
  virtual BOOL OnSetActive() {
    m_nActivated++;
    CPropertySheet* pSheet = GetParentSheet();
    pSheet->SetWizardButtons(m_nIndex == 0 ? PSWIZB_NEXT : PSWIZB_BACK | PSWIZB_FINISH);
    SetTimer(2, 150, NULL);
    return CPropertyPageEx::OnSetActive();
  }
  afx_msg void OnTimer(UINT_PTR) {
    KillTimer(2);
    CPropertySheet* pSheet = GetParentSheet();
    CHECK(pSheet->GetActiveIndex() == m_nIndex);
    if (m_nIndex == 0) {
      TestSaveWindowBitmap(pSheet->m_hWnd, _T("dlgtest-wizard.bmp"));
      pSheet->PressButton(PSBTN_NEXT);
    } else {
      pSheet->PressButton(PSBTN_FINISH);
    }
  }
  DECLARE_MESSAGE_MAP()
};

BEGIN_MESSAGE_MAP(CWizPage, CPropertyPageEx)
  ON_WM_TIMER()
END_MESSAGE_MAP()

static HBITMAP MakeTestBitmap(int cx, int cy, COLORREF color) {
  HDC hdcScreen = ::GetDC(NULL);
  HDC hdc = ::CreateCompatibleDC(hdcScreen);
  HBITMAP hbm = ::CreateCompatibleBitmap(hdcScreen, cx, cy);
  HGDIOBJ old = ::SelectObject(hdc, hbm);
  HBRUSH br = ::CreateSolidBrush(color);
  RECT rc = {0, 0, cx, cy};
  ::FillRect(hdc, &rc, br);
  ::DeleteObject(br);
  ::SelectObject(hdc, old);
  ::DeleteDC(hdc);
  ::ReleaseDC(NULL, hdcScreen);
  return hbm;
}

static void TestWizard97() {
  HBITMAP hbmWatermark = MakeTestBitmap(164, 314, RGB(0, 0, 128));
  HBITMAP hbmHeader = MakeTestBitmap(49, 49, RGB(0, 128, 0));
  CPropertySheetEx sheet(_T("Wizard"), NULL, 0, hbmWatermark, NULL, hbmHeader);
  sheet.m_psh.dwFlags |= PSH_WIZARD97;
  CHECK(sheet.IsWizard());
  CHECK(sheet.m_psh.dwSize == sizeof(PROPSHEETHEADER));
  CHECK((sheet.m_psh.dwFlags & (PSH_WATERMARK | PSH_HEADER)) == (PSH_WATERMARK | PSH_HEADER));
  CHECK(sheet.IsKindOf(RUNTIME_CLASS(CPropertySheetEx)));
  CWizPage page1(IDD_PAGE1, 0, IDS_WIZ_TITLE, IDS_WIZ_SUBTITLE);
  CWizPage page2(IDD_PAGE2, 1, IDS_WIZ_TITLE2, 0);
  CHECK(page1.m_strHeaderTitle == _T("Welcome"));
  CHECK(page1.m_strHeaderSubTitle == _T("The first step of the wizard"));
  CHECK((page1.m_psp.dwFlags & (PSP_USEHEADERTITLE | PSP_USEHEADERSUBTITLE)) ==
        (PSP_USEHEADERTITLE | PSP_USEHEADERSUBTITLE));
  CHECK(page1.m_psp.dwSize == PROPSHEETPAGE_V2_SIZE);
  sheet.AddPage(&page1);
  sheet.AddPage(&page2);
  INT_PTR nResult = sheet.DoModal();
  CHECK(nResult == ID_WIZFINISH);
  CHECK(page1.m_nActivated == 1 && page2.m_nActivated == 1);
  TestLog("wizard97: result %d", (int)nResult);

  // MFC 7's forms on the base classes.
  CPropertyPage page(IDD_PAGE1, 0, IDS_WIZ_TITLE, 0);
  CHECK(page.m_strHeaderTitle == _T("Welcome") && page.m_strHeaderSubTitle.IsEmpty());
  CPropertySheet sheet7(_T("Wizard"), NULL, 0, hbmWatermark);
  CHECK(sheet7.m_psh.hbmWatermark == hbmWatermark && sheet7.m_psh.hbmHeader == NULL);
  ::DeleteObject(hbmWatermark);
  ::DeleteObject(hbmHeader);
}

// CPrintDialogEx: the structure it sets up, and the default printer
// (there may be none under Wine).
static void TestPrintDialogEx() {
  CPrintDialogEx dlg;
  CHECK(dlg.m_pdex.lStructSize == sizeof(PRINTDLGEX));
  CHECK(dlg.m_pdex.nStartPage == START_PAGE_GENERAL);
  CHECK(dlg.PrintAll() && !dlg.PrintRange() && !dlg.PrintCurrentPage());
  CHECK(dlg.IsKindOf(RUNTIME_CLASS(CCommonDialog)));
  BOOL bDefaults = dlg.GetDefaults();
  if (bDefaults) CHECK(!dlg.GetDeviceName().IsEmpty() && dlg.GetCopies() >= 1);
  else CHECK(dlg.GetDeviceName().IsEmpty() && dlg.GetPrinterDC() == NULL);
  TestLog("print dialog ex: default printer '%s'", (LPCSTR)CStringA(dlg.GetDeviceName()));
}

// CDialogEx: a background color, then a tiled bitmap, read back from the
// dialog's client area.
class CExDlg : public CDialogEx {
public:
  COLORREF m_crColor, m_crTile0, m_crTile1;
  int m_nStep;
  CExDlg() : CDialogEx(IDD_DLGEX), m_crColor(0), m_crTile0(0), m_crTile1(0), m_nStep(0) {}
  virtual BOOL OnInitDialog() {
    CDialogEx::OnInitDialog();
    SetBackgroundColor(RGB(10, 200, 30));
    SetTimer(3, 150, NULL);
    return TRUE;
  }
  COLORREF ClientPixel(int x, int y) {
    CClientDC dc(this);
    return dc.GetPixel(x, y);
  }
  afx_msg void OnTimer(UINT_PTR) {
    switch (m_nStep++) {
    case 0: {
      RedrawWindow(NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW | RDW_ALLCHILDREN);
      m_crColor = ClientPixel(2, 2);
      TestSaveWindowBitmap(m_hWnd, _T("dlgtest-dialogex.bmp"));
      // A 2x1 bitmap, red then blue, tiled across the dialog.
      HDC hdcScreen = ::GetDC(NULL);
      HDC hdc = ::CreateCompatibleDC(hdcScreen);
      HBITMAP hbm = ::CreateCompatibleBitmap(hdcScreen, 2, 1);
      HGDIOBJ old = ::SelectObject(hdc, hbm);
      ::SetPixelV(hdc, 0, 0, RGB(255, 0, 0));
      ::SetPixelV(hdc, 1, 0, RGB(0, 0, 255));
      ::SelectObject(hdc, old);
      ::DeleteDC(hdc);
      ::ReleaseDC(NULL, hdcScreen);
      SetBackgroundImage(hbm, BACKGR_TILE, TRUE, FALSE);
      RedrawWindow(NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW | RDW_ALLCHILDREN);
      m_crTile0 = ClientPixel(4, 3);
      m_crTile1 = ClientPixel(5, 3);
      break;
    }
    case 1:
      KillTimer(3);
      EndDialog(IDOK);
      break;
    }
  }
  DECLARE_MESSAGE_MAP()
};

BEGIN_MESSAGE_MAP(CExDlg, CDialogEx)
  ON_WM_TIMER()
END_MESSAGE_MAP()

static void TestDialogEx() {
  CExDlg dlg;
  CHECK(dlg.IsKindOf(RUNTIME_CLASS(CDialog)));
  CHECK(dlg.DoModal() == IDOK);
  CHECK(dlg.m_crColor == RGB(10, 200, 30));
  CHECK(dlg.m_crTile0 == RGB(255, 0, 0) && dlg.m_crTile1 == RGB(0, 0, 255));
  TestLog("dialogex: color %06lx, tiles %06lx %06lx", (unsigned long)dlg.m_crColor,
          (unsigned long)dlg.m_crTile0, (unsigned long)dlg.m_crTile1);
}

// CTaskDialog, driven from its own timer: a radio button, the progress
// bar, the check box, then a command link closes it.
class CTestTaskDialog : public CTaskDialog {
public:
  int m_nTicks, m_nCreated, m_nRadioClicks, m_nDestroyed;
  CTestTaskDialog()
      : CTaskDialog(_T("Pick one of the choices below."), _T("What next?"), _T("dlgtest"),
                    IDS_TASK_FIRST, IDS_TASK_THIRD, TDCBF_CANCEL_BUTTON,
                    TDF_USE_COMMAND_LINKS | TDF_CALLBACK_TIMER | TDF_SHOW_PROGRESS_BAR),
        m_nTicks(0), m_nCreated(0), m_nRadioClicks(0), m_nDestroyed(0) {
    AddRadioButton(301, _T("Fast"));
    AddRadioButton(302, _T("Thorough"));
    AddRadioButton(303, _T("Off"), FALSE);
    SetDefaultRadioButton(302);
    SetVerificationCheckboxText(_T("Don't ask again"));
    SetFooterText(_T("A footer"));
    SetExpansionArea(_T("More detail"), _T("Show"), _T("Hide"));
    SetProgressBarRange(0, 10);
    SetProgressBarPosition(3);
  }
  virtual HRESULT OnCreate() {
    m_nCreated++;
    return S_OK;
  }
  virtual HRESULT OnDestroy() {
    m_nDestroyed++;
    return S_OK;
  }
  virtual HRESULT OnRadioButtonClick(int) {
    m_nRadioClicks++;
    return S_OK;
  }
  virtual HRESULT OnCommandControlClick(int nID) {
    // The first click on the second link is refused; the dialog stays.
    static bool refused = false;
    if (nID == IDS_TASK_SECOND && !refused) {
      refused = true;
      return S_FALSE;
    }
    return S_OK;
  }
  virtual HRESULT OnTimer(long) {
    switch (m_nTicks++) {
    case 2:
      CHECK(m_hWnd != NULL && ::IsWindow(m_hWnd));
      ClickRadioButton(302);
      // (Wine's task dialog does not act on this; Windows checks it.)
      SetProgressBarPosition(7);
      SetVerificationCheckbox(TRUE);
      SetContent(_T("Changed while it runs."));
      TestSaveWindowBitmap(m_hWnd, _T("dlgtest-taskdialog.bmp"));
      break;
    case 4: ClickCommandControl(IDS_TASK_SECOND); break;  // refused
    case 6: ClickCommandControl(IDS_TASK_SECOND); break;
    }
    return S_OK;
  }
};

static void TestTaskDialog() {
  if (!CTaskDialog::IsSupported()) {
    TestLog("task dialog: not supported here");
    return;
  }
  CTestTaskDialog dlg;
  CHECK(dlg.IsCommandControlEnabled(IDS_TASK_FIRST));
  CHECK(!dlg.IsCommandControlEnabled(IDS_TASK_THIRD));  // no such string: no control
  CHECK(!dlg.IsRadioButtonEnabled(303) && dlg.IsRadioButtonEnabled(301));
  CHECK(dlg.GetOptions() & TDF_CALLBACK_TIMER);
  INT_PTR nResult = dlg.DoModal(NULL);
  CHECK(nResult == IDS_TASK_SECOND);
  CHECK(dlg.GetSelectedCommandControlID() == IDS_TASK_SECOND);
  CHECK(dlg.GetSelectedRadioButtonID() == 302);
  CHECK(dlg.GetVerificationCheckboxState());
  CHECK(dlg.m_nCreated == 1 && dlg.m_nDestroyed == 1);
  CHECK(dlg.m_hWnd == NULL);
  TestLog("task dialog: result %d, radio %d, verified %d", (int)nResult,
          dlg.GetSelectedRadioButtonID(), dlg.GetVerificationCheckboxState());
}

// ---------------------------------------------------------------------
// The dialog

class CTestDlg : public CDialog {
public:
  enum { IDD = IDD_TEST };
  CString m_strName;
  int m_nAge;
  BOOL m_bCheck;
  int m_nRadio;
  CString m_strCombo;
  int m_nList;
  int m_nSlider;
  CProgressCtrl m_progress;
  CListCtrl m_listCtrl;
  CTreeCtrl m_tree;
  CComboBox m_combo;
  int m_nStep;
  int m_nPressed;

  CTestDlg() : CDialog(IDD) {
    m_strName = _T("Ada");
    m_nAge = 36;
    m_bCheck = TRUE;
    m_nRadio = 1;
    m_nList = 2;
    m_nSlider = 0;
    m_nStep = 0;
    m_nPressed = 0;
  }

  virtual void DoDataExchange(CDataExchange* pDX) {
    CDialog::DoDataExchange(pDX);
    DDX_Control(pDX, IDC_PROGRESS, m_progress);
    DDX_Control(pDX, IDC_LISTCTRL, m_listCtrl);
    DDX_Control(pDX, IDC_TREE, m_tree);
    DDX_Control(pDX, IDC_COMBO, m_combo);
    DDX_Text(pDX, IDC_NAME, m_strName);
    DDV_MaxChars(pDX, m_strName, 20);
    DDX_Text(pDX, IDC_AGE, m_nAge);
    DDV_MinMaxInt(pDX, m_nAge, 0, 150);
    DDX_Check(pDX, IDC_CHECK, m_bCheck);
    DDX_Radio(pDX, IDC_RADIO1, m_nRadio);
    DDX_CBString(pDX, IDC_COMBO, m_strCombo);
    DDX_LBIndex(pDX, IDC_LIST, m_nList);
    DDX_Slider(pDX, IDC_SLIDER, m_nSlider);
  }

  virtual BOOL OnInitDialog() {
    // Fill the lists before the data exchange selects in them.
    CListBox* pList = (CListBox*)GetDlgItem(IDC_LIST);
    pList->AddString(_T("alpha"));
    pList->AddString(_T("beta"));
    pList->AddString(_T("gamma"));
    pList->AddString(_T("delta"));
    CComboBox* pCombo = (CComboBox*)GetDlgItem(IDC_COMBO);
    pCombo->AddString(_T("one"));
    pCombo->AddString(_T("two"));
    pCombo->AddString(_T("three"));
    ((CSliderCtrl*)GetDlgItem(IDC_SLIDER))->SetRange(0, 10);

    CDialog::OnInitDialog();

    m_progress.SetRange(0, 100);
    m_progress.SetPos(60);
    m_listCtrl.InsertColumn(0, _T("Name"), LVCFMT_LEFT, 80);
    m_listCtrl.InsertColumn(1, _T("Size"), LVCFMT_RIGHT, 50);
    int i = m_listCtrl.InsertItem(0, _T("readme.txt"));
    m_listCtrl.SetItemText(i, 1, _T("12"));
    i = m_listCtrl.InsertItem(1, _T("setup.exe"));
    m_listCtrl.SetItemText(i, 1, _T("4096"));
    m_listCtrl.SetItemData(i, 77);
    HTREEITEM hRoot = m_tree.InsertItem(_T("Root"));
    m_tree.InsertItem(_T("Child 1"), hRoot);
    m_tree.InsertItem(_T("Child 2"), hRoot);
    m_tree.Expand(hRoot, TVE_EXPAND);

    SetTimer(1, 200, NULL);
    return TRUE;
  }

  afx_msg void OnPress() { m_nPressed++; }

  afx_msg void OnTimer(UINT_PTR) {
    switch (m_nStep++) {
      case 0: {
        // The data went into the controls.
        CString str;
        GetDlgItemText(IDC_NAME, str);
        CHECK(str == _T("Ada"));
        CHECK(GetDlgItemInt(IDC_AGE) == 36);
        CHECK(IsDlgButtonChecked(IDC_CHECK) == BST_CHECKED);
        CHECK(GetCheckedRadioButton(IDC_RADIO1, IDC_RADIO3) == IDC_RADIO2);
        CHECK(((CListBox*)GetDlgItem(IDC_LIST))->GetCurSel() == 2);
        CHECK(m_progress.GetPos() == 60);
        CHECK(m_listCtrl.GetItemCount() == 2);
        CHECK(m_listCtrl.GetItemText(1, 0) == _T("setup.exe"));
        CHECK(m_listCtrl.GetItemText(1, 1) == _T("4096"));
        CHECK(m_listCtrl.GetItemData(1) == 77);
        CHECK(m_tree.GetCount() == 3);
        CHECK(m_tree.GetItemText(m_tree.GetRootItem()) == _T("Root"));
        CHECK(m_tree.ItemHasChildren(m_tree.GetRootItem()));
        CString strRes;
        CHECK(strRes.LoadString(IDS_HELLO) && strRes == _T("Hello from the string table"));
        // Controls created on the fly wrap as temporary objects.
        CWnd* pWnd = GetDlgItem(IDC_NAME);
        CHECK(pWnd != NULL && pWnd->GetDlgCtrlID() == IDC_NAME);
        CHECK(CWnd::FromHandlePermanent(m_listCtrl.m_hWnd) == &m_listCtrl);
        TestSaveWindowBitmap(m_hWnd, _T("dlgtest.bmp"));
        break;
      }
      case 1: {
        // The user changes things; the data comes back.
        SetDlgItemText(IDC_NAME, _T("Grace"));
        SetDlgItemInt(IDC_AGE, 85);
        CheckDlgButton(IDC_CHECK, BST_UNCHECKED);
        CheckRadioButton(IDC_RADIO1, IDC_RADIO3, IDC_RADIO3);
        m_combo.SetCurSel(1);
        ((CListBox*)GetDlgItem(IDC_LIST))->SetCurSel(0);
        ((CSliderCtrl*)GetDlgItem(IDC_SLIDER))->SetPos(7);
        CHECK(UpdateData(TRUE));
        CHECK(m_strName == _T("Grace"));
        CHECK(m_nAge == 85);
        CHECK(m_bCheck == FALSE);
        CHECK(m_nRadio == 2);
        CHECK(m_strCombo == _T("two"));
        CHECK(m_nList == 0);
        CHECK(m_nSlider == 7);
        // A button click reaches the message map.
        SendMessage(WM_COMMAND, MAKEWPARAM(IDC_BUTTON, BN_CLICKED),
                    (LPARAM)GetDlgItem(IDC_BUTTON)->m_hWnd);
        CHECK(m_nPressed == 1);
        break;
      }
      case 2:
        KillTimer(1);
        // OK runs the data exchange and ends the dialog.
        SetDlgItemText(IDC_NAME, _T("Hopper"));
        PostMessage(WM_COMMAND, MAKEWPARAM(IDOK, BN_CLICKED), (LPARAM)GetDlgItem(IDOK)->m_hWnd);
        break;
    }
  }
  DECLARE_MESSAGE_MAP()
};

BEGIN_MESSAGE_MAP(CTestDlg, CDialog)
  ON_WM_TIMER()
  ON_BN_CLICKED(IDC_BUTTON, OnPress)
END_MESSAGE_MAP()

class CTestApp : public CWinApp {
public:
  virtual BOOL InitInstance() {
    TestOpenLog(_T("dlgtest.log"));
    InitCommonControls();

    TestPropertySheet();
    TestWizard97();
    TestPrintDialogEx();
    TestDialogEx();
    TestTaskDialog();

    CTestDlg dlg;
    m_pMainWnd = &dlg;
    INT_PTR nResult = dlg.DoModal();
    CHECK(nResult == IDOK);
    CHECK(dlg.m_strName == _T("Hopper"));
    TestLog("dialog: result %d, name '%s'", (int)nResult, (LPCSTR)CStringA(dlg.m_strName));
    return FALSE;
  }
  virtual int ExitInstance() {
    CWinApp::ExitInstance();
    return TestFinish();
  }
};

CTestApp theApp;
