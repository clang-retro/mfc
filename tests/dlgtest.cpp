// dlgtest.cpp - a dialog-based program: dialog data exchange, controls,
// common controls and a property sheet.
#include <afxwin.h>
#include <afxext.h>
#include <afxdlgs.h>
#include <afxcmn.h>
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
