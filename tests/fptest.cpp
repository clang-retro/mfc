// fptest.cpp - the Feature Pack's base and standalone controls:
// CSettingsStore and CWinAppEx's registry settings, the visual managers,
// CMFCButton (check box, radio group, images, sizing), CMFCLinkCtrl,
// CMFCMenuButton and its menu, CMFCColorButton and its palette,
// CMFCEditBrowseCtrl's button, CMFCMaskedEdit's input, CMFCFontComboBox
// and CMFCSpinButtonCtrl.
#include <afxwinappex.h>
#include <afxcontrolbars.h>
#include "fptest.h"
#include "testutil.h"

static const TCHAR kTestKey[] = _T("Software\\clang-retro\\fptest");

// ---------------------------------------------------------------------
// A serializable object for CSettingsStore's Write(CObject&).

class CTestPoint3 : public CObject {
  DECLARE_SERIAL(CTestPoint3)
public:
  int x, y, z;
  CTestPoint3() : x(0), y(0), z(0) {}
  virtual void Serialize(CArchive& ar) {
    if (ar.IsStoring())
      ar << x << y << z;
    else
      ar >> x >> y >> z;
  }
};
IMPLEMENT_SERIAL(CTestPoint3, CObject, 1)

static void TestSettingsStore() {
  TestLog("settings store...");
  {
    CSettingsStore reg(FALSE, FALSE);
    CHECK(reg.CreateKey(CString(kTestKey) + _T("\\Store")));
    CHECK(reg.Write(_T("Int"), 42));
    CHECK(reg.Write(_T("Dword"), (DWORD)0xDEADBEEF));
    CHECK(reg.Write(_T("String"), _T("hello")));
    CHECK(reg.Write(_T("Rect"), CRect(1, 2, 3, 4)));
    CPoint pt(5, 6);
    CHECK(reg.Write(_T("Point"), pt));
    BYTE data[5] = {1, 2, 3, 4, 5};
    CHECK(reg.Write(_T("Binary"), data, sizeof(data)));
    CStringArray sa;
    sa.Add(_T("one"));
    sa.Add(_T("two"));
    CHECK(reg.Write(_T("Strings"), sa));
    CDWordArray da;
    da.Add(7);
    da.Add(8);
    da.Add(9);
    CHECK(reg.Write(_T("Dwords"), da));
    CList<DWORD, DWORD> dl;
    dl.AddTail(10);
    dl.AddTail(11);
    CHECK(reg.Write(_T("List"), dl));
    CMap<UINT, UINT, UINT, UINT> map;
    map[1] = 100;
    map[2] = 200;
    CHECK(reg.Write(_T("Map"), map));
    CTestPoint3 obj;
    obj.x = 1;
    obj.y = -2;
    obj.z = 3;
    CHECK(reg.Write(_T("Object"), obj));
    CHECK(reg.Write(_T("ObjectPtr"), &obj));  // with its class
  }
  {
    CSettingsStore reg(FALSE, TRUE);
    CHECK(reg.Open(CString(kTestKey) + _T("\\Store")));
    int n = 0;
    CHECK(reg.Read(_T("Int"), n) && n == 42);
    DWORD dw = 0;
    CHECK(reg.Read(_T("Dword"), dw) && dw == 0xDEADBEEF);
    CString str;
    CHECK(reg.Read(_T("String"), str) && str == _T("hello"));
    CRect rect;
    CHECK(reg.Read(_T("Rect"), rect) && rect == CRect(1, 2, 3, 4));
    CPoint pt;
    CHECK(reg.Read(_T("Point"), pt) && pt == CPoint(5, 6));
    BYTE* pData = NULL;
    UINT nBytes = 0;
    CHECK(reg.Read(_T("Binary"), &pData, &nBytes) && nBytes == 5 && pData != NULL && pData[4] == 5);
    delete[] pData;
    CStringArray sa;
    CHECK(reg.Read(_T("Strings"), sa) && sa.GetSize() == 2 && sa[1] == _T("two"));
    CDWordArray da;
    CHECK(reg.Read(_T("Dwords"), da) && da.GetSize() == 3 && da[2] == 9);
    CList<DWORD, DWORD> dl;
    CHECK(reg.Read(_T("List"), dl) && dl.GetCount() == 2 && dl.GetTail() == 11);
    CMap<UINT, UINT, UINT, UINT> map;
    UINT v = 0;
    CHECK(reg.Read(_T("Map"), map) && map.GetCount() == 2 && map.Lookup(2, v) && v == 200);
    CTestPoint3 obj;
    CHECK(reg.Read(_T("Object"), obj) && obj.x == 1 && obj.y == -2 && obj.z == 3);
    CObject* pObj = NULL;
    CHECK(reg.Read(_T("ObjectPtr"), pObj) && pObj != NULL && pObj->IsKindOf(RUNTIME_CLASS(CTestPoint3)) &&
          ((CTestPoint3*)pObj)->z == 3);
    delete pObj;
    // Missing values, and the wrong type.
    CHECK(!reg.Read(_T("Missing"), n));
    CHECK(!reg.Read(_T("String"), n));
    // Read-only: no writes.
    CHECK(!reg.Write(_T("Int"), 1));
  }
  {
    CSettingsStore reg(FALSE, FALSE);
    CHECK(reg.Open(CString(kTestKey) + _T("\\Store")));
    CHECK(reg.DeleteValue(_T("Int")));
    int n = 0;
    CHECK(!reg.Read(_T("Int"), n));
  }
}

// The application's own settings: under Software\<key>\<app>\Workspace.
static void TestAppSettings(CWinAppEx* pApp) {
  TestLog("application settings...");
  CString strPath = pApp->GetRegSectionPath();
  TestLog("section path: %s", (LPCSTR)CStringA(strPath));
  CHECK(strPath.Find(_T("clang-retro")) >= 0 && strPath.Find(_T("Workspace")) >= 0);
  CHECK(pApp->WriteInt(_T("Count"), 7));
  CHECK(pApp->GetInt(_T("Count")) == 7);
  CHECK(pApp->GetInt(_T("Absent"), -5) == -5);
  CHECK(pApp->WriteString(_T("Name"), _T("fptest")));
  CHECK(pApp->GetString(_T("Name")) == _T("fptest"));
  CHECK(pApp->WriteSectionInt(_T("Sub"), _T("Value"), 99));
  CHECK(pApp->GetSectionInt(_T("Sub"), _T("Value")) == 99);
  CHECK(pApp->WriteSectionString(_T("Sub"), _T("Text"), _T("abc")));
  CHECK(pApp->GetSectionString(_T("Sub"), _T("Text")) == _T("abc"));
  BYTE data[3] = {9, 8, 7};
  CHECK(pApp->WriteBinary(_T("Bytes"), data, 3));
  LPBYTE pData = NULL;
  UINT nBytes = 0;
  CHECK(pApp->GetBinary(_T("Bytes"), &pData, &nBytes) && nBytes == 3 && pData[2] == 7);
  delete[] pData;
  CTestPoint3 obj;
  obj.z = 12;
  CHECK(pApp->WriteObject(_T("Obj"), obj));
  CTestPoint3 obj2;
  CHECK(pApp->GetObject(_T("Obj"), obj2) && obj2.z == 12);
  // Where it went: the store sees it at the section path.
  CSettingsStore reg(FALSE, TRUE);
  CHECK(reg.Open(strPath));
  int n = 0;
  CHECK(reg.Read(_T("Count"), n) && n == 7);
  CHECK(reg.Open(pApp->GetRegSectionPath(_T("Sub"))));
  CHECK(reg.Read(_T("Value"), n) && n == 99);

  // The managers.
  CHECK(pApp->InitShellManager() && pApp->GetShellManager() != NULL && afxShellManager != NULL);
  CHECK(pApp->InitContextMenuManager() && pApp->GetContextMenuManager() != NULL);
  CHECK(pApp->InitKeyboardManager() && pApp->GetKeyboardManager() != NULL);
  CHECK(pApp->InitTooltipManager() && pApp->GetTooltipManager() != NULL);
  CHECK(pApp->InitMouseManager() && pApp->GetMouseManager() != NULL);
}

static void TestVisualManagers(CWnd* pHost) {
  TestLog("visual managers...");
  CMFCVisualManager* pVM = CMFCVisualManager::GetInstance();
  CHECK(pVM != NULL && pVM->GetRuntimeClass() == RUNTIME_CLASS(CMFCVisualManager));
  CHECK(!pVM->IsFlat());
  CRuntimeClass* classes[] = {RUNTIME_CLASS(CMFCVisualManagerOfficeXP),  RUNTIME_CLASS(CMFCVisualManagerWindows),
                              RUNTIME_CLASS(CMFCVisualManagerOffice2003), RUNTIME_CLASS(CMFCVisualManagerVS2005),
                              RUNTIME_CLASS(CMFCVisualManagerOffice2007), RUNTIME_CLASS(CMFCVisualManagerVS2008),
                              RUNTIME_CLASS(CMFCVisualManagerWindows7)};
  for (size_t i = 0; i < sizeof(classes) / sizeof(classes[0]); i++) {
    CMFCVisualManager::SetDefaultManager(classes[i]);
    pVM = CMFCVisualManager::GetInstance();
    CHECK(pVM != NULL && pVM->GetRuntimeClass() == classes[i]);
    CHECK(pVM->IsKindOf(RUNTIME_CLASS(CMFCVisualManagerOfficeXP)));
    CHECK(pVM->IsFlat());
    TestLog("  %s: highlight %06lx", classes[i]->m_lpszClassName, (unsigned long)pVM->GetHighlightColor());
  }
  CMFCVisualManagerOffice2007::SetStyle(CMFCVisualManagerOffice2007::Office2007_ObsidianBlack);
  CHECK(CMFCVisualManagerOffice2007::GetStyle() == CMFCVisualManagerOffice2007::Office2007_ObsidianBlack);
  CMFCVisualManagerOffice2007::SetStyle(CMFCVisualManagerOffice2007::Office2007_LunaBlue);
  // Back to Office XP's flat look for the controls below.
  CMFCVisualManager::SetDefaultManager(RUNTIME_CLASS(CMFCVisualManagerOfficeXP));
  CHECK(CMFCVisualManager::GetInstance()->IsKindOf(RUNTIME_CLASS(CMFCVisualManagerOfficeXP)));
  // The global data.
  CHECK(afxGlobalData.clrBtnFace == ::GetSysColor(COLOR_BTNFACE));
  CHECK(afxGlobalData.fontRegular.GetSafeHandle() != NULL);
  CMFCVisualManager::RedrawAll();
  pHost->UpdateWindow();
}

// ---------------------------------------------------------------------
// The host window, which records the controls' notifications.

class CHostWnd : public CFrameWnd {
public:
  int m_nClicks[16];
  CHostWnd() { memset(m_nClicks, 0, sizeof(m_nClicks)); }
  virtual BOOL OnCommand(WPARAM wParam, LPARAM lParam) {
    UINT nID = LOWORD(wParam);
    if (HIWORD(wParam) == BN_CLICKED && nID >= 1000 && nID < 1016) m_nClicks[nID - 1000]++;
    return CFrameWnd::OnCommand(wParam, lParam);
  }
};

static void Click(CWnd* pWnd, CPoint pt = CPoint(-1, -1)) {
  if (pt.x < 0) {
    CRect rect;
    pWnd->GetClientRect(rect);
    pt = rect.CenterPoint();
  }
  pWnd->SendMessage(WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(pt.x, pt.y));
  pWnd->SendMessage(WM_LBUTTONUP, 0, MAKELPARAM(pt.x, pt.y));
}

static void TestButtons(CHostWnd* pHost) {
  TestLog("buttons...");
  // A push button: owner drawn, sized to its text, clicks reach the parent.
  CMFCButton push;
  CHECK(push.Create(_T("Push me"), WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, CRect(10, 10, 110, 40), pHost, IDC_PUSH));
  CHECK((push.GetStyle() & BS_TYPEMASK) == BS_OWNERDRAW);
  CHECK(!push.IsCheckBox() && !push.IsRadioButton());
  push.m_nFlatStyle = CMFCButton::BUTTONSTYLE_FLAT;
  push.SetTooltip(_T("A tip"));
  push.SetFaceColor(RGB(200, 220, 255));
  push.SetTextColor(RGB(0, 0, 128));
  Click(&push);
  CHECK(pHost->m_nClicks[IDC_PUSH - 1000] == 1);
  CSize size = push.SizeToContent(TRUE);
  TestLog("push size %dx%d", (int)size.cx, (int)size.cy);
  CHECK(size.cx > 20 && size.cy > 10);
  CHECK(!push.HasImage());
  push.SetImage(::LoadIcon(NULL, IDI_APPLICATION), FALSE);
  CHECK(push.HasImage() && push.GetImageSize().cx > 0);
  CSize sizeImage = push.SizeToContent(TRUE);
  CHECK(sizeImage.cx > size.cx);
  push.SizeToContent();
  CRect rect;
  push.GetWindowRect(rect);
  CHECK(rect.Width() == sizeImage.cx && rect.Height() == sizeImage.cy);
  push.SetWindowText(_T("Pushed"));
  CString str;
  push.GetWindowText(str);
  CHECK(str == _T("Pushed"));
  push.UpdateWindow();

  // A check box: the button keeps the check.
  CMFCButton check;
  CHECK(check.Create(_T("Check"), WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, CRect(10, 50, 110, 75), pHost,
                     IDC_CHECK));
  CHECK(check.IsCheckBox() && check.IsAutoCheck() && !check.IsChecked());
  Click(&check);
  CHECK(check.IsChecked() && check.GetCheck() == BST_CHECKED);
  CHECK(check.SendMessage(BM_GETCHECK) == BST_CHECKED);
  Click(&check);
  CHECK(!check.IsChecked());
  check.SendMessage(BM_SETCHECK, BST_CHECKED);
  CHECK(check.IsChecked());
  CHECK(pHost->m_nClicks[IDC_CHECK - 1000] == 2);
  // The space bar toggles it too.
  check.SendMessage(WM_KEYDOWN, VK_SPACE, 0);
  check.SendMessage(WM_KEYUP, VK_SPACE, 0);
  CHECK(!check.IsChecked());

  // A radio group: checking one unchecks the other.
  CMFCButton radio1, radio2;
  CHECK(radio1.Create(_T("One"), WS_CHILD | WS_VISIBLE | WS_GROUP | BS_AUTORADIOBUTTON, CRect(10, 80, 110, 105),
                      pHost, IDC_RADIO1));
  CHECK(radio2.Create(_T("Two"), WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON, CRect(10, 110, 110, 135), pHost,
                      IDC_RADIO2));
  CHECK(radio1.IsRadioButton() && radio2.IsRadioButton());
  Click(&radio1);
  CHECK(radio1.IsChecked() && !radio2.IsChecked());
  Click(&radio2);
  CHECK(!radio1.IsChecked() && radio2.IsChecked());
  Click(&radio2);
  CHECK(radio2.IsChecked());  // a radio button stays checked
  pHost->UpdateWindow();
  TestPump();
  TestSaveWindowBitmap(pHost->m_hWnd, _T("fptest-buttons.bmp"));

  // The link: drawn as one, the click without opening anything.
  CMFCLinkCtrl link;
  CHECK(link.Create(_T("clang-retro"), WS_CHILD | WS_VISIBLE, CRect(120, 10, 250, 30), pHost, IDC_LINK));
  link.SetURL(_T("example.com"));
  link.SetURLPrefix(_T("https://"));
  CHECK(link.GetURL() == _T("example.com") && link.GetURLPrefix() == _T("https://"));
  link.m_bDefaultClickProcess = FALSE;
  CHECK(!link.m_bVisited);
  Click(&link);
  CHECK(link.m_bVisited);
  CHECK(pHost->m_nClicks[IDC_LINK - 1000] == 1);
  CSize sizeLink = link.SizeToContent();
  CHECK(sizeLink.cx > 0 && sizeLink.cy > 0);
  link.UpdateWindow();
}

static void TestMenuButton(CHostWnd* pHost) {
  TestLog("menu button...");
  CMenu menu;
  CHECK(menu.LoadMenu(IDR_POPUPMENU));
  CMFCMenuButton btn;
  CHECK(btn.Create(_T("Menu"), WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, CRect(120, 40, 250, 70), pHost, IDC_MENUBTN));
  btn.m_hMenu = menu.GetSubMenu(0)->GetSafeHmenu();
  CSize size = btn.SizeToContent(TRUE);
  CHECK(size.cx > 0);
  btn.UpdateWindow();
  // The menu's loop picks the second item from keys queued first.
  pHost->PostMessage(WM_KEYDOWN, VK_DOWN, 0);
  pHost->PostMessage(WM_KEYDOWN, VK_DOWN, 0);
  pHost->PostMessage(WM_KEYDOWN, VK_RETURN, 0);
  Click(&btn);
  TestLog("menu result %d", btn.m_nMenuResult);
  CHECK(btn.m_nMenuResult == ID_MENU_TWO);
  CHECK(pHost->m_nClicks[IDC_MENUBTN - 1000] == 1);
  // Escape: no command.
  pHost->PostMessage(WM_KEYDOWN, VK_ESCAPE, 0);
  Click(&btn);
  CHECK(btn.m_nMenuResult == 0);
  CHECK(pHost->m_nClicks[IDC_MENUBTN - 1000] == 1);
}

class CTestColorButton : public CMFCColorButton {
public:
  int m_nOther;
  CTestColorButton() : m_nOther(0) {}
  virtual BOOL OnOtherColor(COLORREF& color) {
    m_nOther++;
    color = RGB(1, 2, 3);
    return TRUE;
  }
  void ShowPopup() { OnShowColorPopup(); }
};

static void TestColorButton(CHostWnd* pHost) {
  TestLog("color button...");
  CTestColorButton btn;
  CHECK(btn.Create(_T(""), WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, CRect(120, 80, 200, 105), pHost, IDC_COLORBTN));
  CHECK(btn.GetColors().GetSize() >= 40);
  btn.SetColor(RGB(255, 0, 0));
  CHECK(btn.GetColor() == RGB(255, 0, 0));
  btn.EnableAutomaticButton(_T("Automatic"), RGB(0, 0, 0));
  btn.EnableOtherButton(_T("More..."));
  CHECK(btn.GetAutomaticColor() == RGB(0, 0, 0));
  CString strName;
  CMFCColorButton::SetColorName(RGB(255, 0, 0), _T("Red"));
  CHECK(CMFCColorButton::GetColorName(RGB(255, 0, 0), strName) && strName == _T("Red"));
  btn.UpdateWindow();

  // The palette: Down from the automatic button goes to the first color.
  COLORREF clrFirst = btn.GetColors()[0];
  pHost->PostMessage(WM_KEYDOWN, VK_DOWN, 0);
  pHost->PostMessage(WM_KEYDOWN, VK_RETURN, 0);
  btn.ShowPopup();
  CHECK(btn.GetColor() == clrFirst);
  CHECK(pHost->m_nClicks[IDC_COLORBTN - 1000] == 1);
  // The second color, by Right.
  COLORREF clrSecond = btn.GetColors()[1];
  pHost->PostMessage(WM_KEYDOWN, VK_DOWN, 0);
  pHost->PostMessage(WM_KEYDOWN, VK_RIGHT, 0);
  pHost->PostMessage(WM_KEYDOWN, VK_RETURN, 0);
  btn.ShowPopup();
  CHECK(btn.GetColor() == clrSecond);
  // Up from the first row: the automatic color.
  pHost->PostMessage(WM_KEYDOWN, VK_DOWN, 0);
  pHost->PostMessage(WM_KEYDOWN, VK_UP, 0);
  pHost->PostMessage(WM_KEYDOWN, VK_RETURN, 0);
  btn.ShowPopup();
  CHECK(btn.GetColor() == (COLORREF)-1);
  // The other color.
  // (Down to the first color, Left round to the last, Down to the button.)
  pHost->PostMessage(WM_KEYDOWN, VK_DOWN, 0);
  pHost->PostMessage(WM_KEYDOWN, VK_LEFT, 0);
  pHost->PostMessage(WM_KEYDOWN, VK_DOWN, 0);
  pHost->PostMessage(WM_KEYDOWN, VK_RETURN, 0);
  btn.ShowPopup();
  CHECK(btn.m_nOther == 1 && btn.GetColor() == RGB(1, 2, 3));
  // Escape: unchanged.
  btn.SetColor(RGB(0, 255, 0));
  pHost->PostMessage(WM_KEYDOWN, VK_ESCAPE, 0);
  btn.ShowPopup();
  CHECK(btn.GetColor() == RGB(0, 255, 0));
}

// ---------------------------------------------------------------------
// The edit controls.

class CTestBrowse : public CMFCEditBrowseCtrl {
public:
  int m_nBrowse;
  CTestBrowse() : m_nBrowse(0) {}
  virtual void OnBrowse() {
    if (GetMode() == BrowseMode_Default) {
      m_nBrowse++;
      _SetText(_T("browsed"));
      return;
    }
    CMFCEditBrowseCtrl::OnBrowse();
  }
};

static void TestEditBrowse(CHostWnd* pHost) {
  TestLog("edit browse...");
  CTestBrowse edit;
  CHECK(edit.CreateEx(WS_EX_CLIENTEDGE, _T("EDIT"), _T(""), WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                      CRect(10, 150, 210, 175), pHost, IDC_BROWSE));
  edit.SetFont(&afxGlobalData.fontRegular);
  CRect rcBefore;
  edit.GetClientRect(rcBefore);
  CHECK(edit.GetBrowseButtonRect().IsRectEmpty());
  edit.EnableBrowseButton();
  CHECK(edit.GetMode() == CMFCEditBrowseCtrl::BrowseMode_Default);
  CRect rcAfter;
  edit.GetClientRect(rcAfter);
  CRect rcButton = edit.GetBrowseButtonRect();
  TestLog("client %d -> %d, button %d", rcBefore.Width(), rcAfter.Width(), rcButton.Width());
  CHECK(rcButton.Width() > 0 && rcAfter.Width() == rcBefore.Width() - rcButton.Width());
  edit.UpdateWindow();

  // A click on the button, as the mouse makes it.
  CRect rcWnd;
  edit.GetWindowRect(rcWnd);
  CPoint pt = rcButton.CenterPoint() + rcWnd.TopLeft();
  CHECK(edit.SendMessage(WM_NCHITTEST, 0, MAKELPARAM(pt.x, pt.y)) == HTBORDER);
  edit.SendMessage(WM_NCLBUTTONDOWN, HTBORDER, MAKELPARAM(pt.x, pt.y));
  CPoint ptClient = pt;
  edit.ScreenToClient(&ptClient);
  edit.SendMessage(WM_LBUTTONUP, 0, MAKELPARAM(ptClient.x, ptClient.y));
  CHECK(edit.m_nBrowse == 1);
  CString str;
  edit.GetWindowText(str);
  CHECK(str == _T("browsed") && edit.GetModify());
  // Off the button: no browse.
  edit.SendMessage(WM_NCLBUTTONDOWN, HTBORDER, MAKELPARAM(pt.x, pt.y));
  CPoint ptAway(5, 5);
  edit.SendMessage(WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(ptAway.x, ptAway.y));
  edit.SendMessage(WM_LBUTTONUP, 0, MAKELPARAM(ptAway.x, ptAway.y));
  CHECK(edit.m_nBrowse == 1);
  // F4.
  edit.SendMessage(WM_KEYDOWN, VK_F4, 0);
  CHECK(edit.m_nBrowse == 2);
  // A label and an image change the width.
  edit.EnableBrowseButton(TRUE, _T("Browse for it"));
  CHECK(edit.GetBrowseButtonRect().Width() > rcButton.Width());
  edit.SetBrowseButtonImage(::LoadIcon(NULL, IDI_APPLICATION), FALSE);
  CHECK(edit.GetBrowseButtonRect().Width() > 0);
  edit.EnableFolderBrowseButton();
  CHECK(edit.GetMode() == CMFCEditBrowseCtrl::BrowseMode_Folder);
  edit.EnableFileBrowseButton(_T("txt"), _T("Text files (*.txt)|*.txt||"));
  CHECK(edit.GetMode() == CMFCEditBrowseCtrl::BrowseMode_File);
  edit.UpdateWindow();
  TestPump();
  // No button.
  edit.EnableBrowseButton(FALSE);
  edit.GetClientRect(rcAfter);
  CHECK(edit.GetMode() == CMFCEditBrowseCtrl::BrowseMode_None && rcAfter.Width() == rcBefore.Width());
}

static void TypeChars(CWnd* pWnd, LPCTSTR psz) {
  for (; *psz; psz++) pWnd->SendMessage(WM_CHAR, (WPARAM)(TBYTE)*psz, 1);
}

static void TestMaskedEdit(CHostWnd* pHost) {
  TestLog("masked edit...");
  CMFCMaskedEdit edit;
  CHECK(edit.CreateEx(WS_EX_CLIENTEDGE, _T("EDIT"), _T(""), WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                      CRect(10, 185, 210, 210), pHost, IDC_MASKED));
  edit.EnableMask(_T(" ddd  ddd dddd"), _T("(___) ___-____"), _T('_'));
  CString str;
  edit.EnableGetMaskedCharsOnly(FALSE);
  edit.GetWindowText(str);
  CHECK(str == _T("(___) ___-____"));
  edit.SetFocus();
  edit.SetSel(0, 0);
  TypeChars(&edit, _T("425x5551234"));  // the x is refused
  edit.GetWindowText(str);
  TestLog("masked: %s", (LPCSTR)CStringA(str));
  CHECK(str == _T("(425) 555-1234"));
  edit.EnableGetMaskedCharsOnly(TRUE);
  edit.GetWindowText(str);
  CHECK(str == _T("4255551234"));
  // WM_GETTEXT, as DDX_Text reads it, gives the same.
  TCHAR sz[32];
  ::GetWindowText(edit.m_hWnd, sz, 32);
  CHECK(CString(sz) == _T("4255551234"));
  CHECK(edit.GetWindowTextLength() == 10);
  // Backspace from the end clears the last digit.
  edit.SetSel(14, 14);
  edit.SendMessage(WM_CHAR, VK_BACK, 1);
  edit.GetWindowText(str);
  CHECK(str == _T("425555123"));
  // Delete at the start clears the first.
  edit.SetSel(0, 0);
  edit.SendMessage(WM_KEYDOWN, VK_DELETE, 0);
  edit.GetWindowText(str);
  CHECK(str == _T("25555123"));
  // The whole text: it must fit the template.
  CHECK(!edit.SetWindowText(_T("12")));
  CHECK(!edit.SetWindowText(_T("[123] 456-7890")));
  CHECK(edit.SetWindowText(_T("(123) 456-7890")));
  edit.GetWindowText(str);
  CHECK(str == _T("1234567890"));
  // Only the typed characters.
  edit.EnableSetMaskedCharsOnly(TRUE);
  CHECK(edit.SetWindowText(_T("9876543210")));
  CHECK(!edit.SetWindowText(_T("98a")));
  edit.EnableGetMaskedCharsOnly(FALSE);
  edit.GetWindowText(str);
  CHECK(str == _T("(987) 654-3210"));
  // Valid characters only.
  edit.EnableMask(_T("AAAA"), _T("____"), _T('_'), _T("ABC123"));
  edit.SetSel(0, 0);
  TypeChars(&edit, _T("AzB1"));
  edit.EnableGetMaskedCharsOnly(TRUE);
  edit.GetWindowText(str);
  CHECK(str == _T("AB1"));
  // Without a mask, an ordinary edit.
  edit.DisableMask();
  edit.SetWindowText(_T("free text"));
  edit.GetWindowText(str);
  CHECK(str == _T("free text"));
  edit.UpdateWindow();
}

static void TestFontCombo(CHostWnd* pHost) {
  TestLog("font combo...");
  CMFCFontComboBox combo;
  CHECK(combo.Create(WS_CHILD | WS_VISIBLE | WS_VSCROLL | CBS_DROPDOWNLIST, CRect(220, 150, 380, 350), pHost,
                     IDC_FONTS));
  CHECK((combo.GetStyle() & CBS_OWNERDRAWFIXED) != 0);
  combo.SetFont(&afxGlobalData.fontRegular);
  CHECK(combo.Setup());
  int nCount = combo.GetCount();
  TestLog("fonts: %d", nCount);
  CHECK(nCount > 0);
  CMFCFontInfo* pFirst = (CMFCFontInfo*)combo.GetItemDataPtr(0);
  CHECK(pFirst != NULL && !pFirst->m_strName.IsEmpty());
  CHECK(combo.GetSelFont() == NULL);
  CString strLast = ((CMFCFontInfo*)combo.GetItemDataPtr(nCount - 1))->m_strName;
  CHECK(combo.SelectFont(strLast));
  CHECK(combo.GetSelFont() != NULL && combo.GetSelFont()->m_strName == strLast);
  CHECK(!combo.SelectFont(_T("No Such Font Anywhere")));
  // Sorted by name.
  BOOL bSorted = TRUE;
  for (int i = 1; i < nCount; i++)
    if (((CMFCFontInfo*)combo.GetItemDataPtr(i - 1))->m_strName.CompareNoCase(
            ((CMFCFontInfo*)combo.GetItemDataPtr(i))->m_strName) > 0)
      bSorted = FALSE;
  CHECK(bSorted);
  // TrueType only.
  CHECK(combo.Setup(TRUETYPE_FONTTYPE) || combo.GetCount() == 0);
  for (int i = 0; i < combo.GetCount(); i++)
    if (!(((CMFCFontInfo*)combo.GetItemDataPtr(i))->m_nType & TRUETYPE_FONTTYPE)) {
      CHECK(!"a font that is not TrueType");
      break;
    }
  TestLog("TrueType fonts: %d", combo.GetCount());
  combo.UpdateWindow();
}

static void TestSpin(CHostWnd* pHost) {
  TestLog("spin...");
  CMFCSpinButtonCtrl spin;
  CHECK(spin.Create(WS_CHILD | WS_VISIBLE | UDS_SETBUDDYINT, CRect(390, 150, 410, 180), pHost, IDC_SPIN));
  spin.SetRange(0, 10);
  spin.SetPos(5);
  CHECK(LOWORD(spin.GetPos()) == 5);
  spin.Invalidate();
  spin.UpdateWindow();
  CRect rect;
  spin.GetClientRect(rect);
  Click(&spin, CPoint(rect.Width() / 2, 2));
  TestLog("spin pos after up: %d", (int)LOWORD(spin.GetPos()));
}

// ---------------------------------------------------------------------

class CFPTestApp : public CWinAppEx {
public:
  virtual BOOL InitInstance() {
    TestOpenLog(_T("fptest.log"));
    CWinAppEx::InitInstance();
    ::InitCommonControls();
    SetRegistryKey(_T("clang-retro"));
    {
      CSettingsStore reg(FALSE, FALSE);
      reg.DeleteKey(kTestKey);
    }
    CHostWnd* pHost = new CHostWnd;
    if (!pHost->Create(NULL, _T("fptest"), WS_OVERLAPPEDWINDOW, CRect(0, 0, 440, 400))) {
      TestLog("Create failed");
      g_nFailures++;
      return FALSE;
    }
    m_pMainWnd = pHost;
    pHost->ShowWindow(SW_SHOW);
    pHost->UpdateWindow();

    TestSettingsStore();
    TestAppSettings(this);
    TestVisualManagers(pHost);
    TestButtons(pHost);
    TestMenuButton(pHost);
    TestColorButton(pHost);
    TestEditBrowse(pHost);
    TestMaskedEdit(pHost);
    TestFontCombo(pHost);
    TestSpin(pHost);

    {
      CSettingsStore reg(FALSE, FALSE);
      CHECK(reg.DeleteKey(kTestKey));
      CHECK(!reg.Open(kTestKey));
      reg.DeleteKey(CString(_T("Software\\clang-retro\\")) + m_pszProfileName);
    }
    pHost->DestroyWindow();
    m_pMainWnd = NULL;
    return FALSE;
  }
  virtual int ExitInstance() {
    CWinAppEx::ExitInstance();
    return TestFinish();
  }
};

CFPTestApp theApp;
