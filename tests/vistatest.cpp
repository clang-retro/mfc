// vistatest.cpp - the Vista-era pieces: CSplitButton and its drop-down
// menu, command links and the other CButton additions, CNetAddressCtrl,
// the Vista-style CFileDialog (open, multiple selection, save, the
// customization calls) and CFolderPickerDialog. Where Windows lacks a
// piece (XP), the test logs that and checks the fallback instead.
#ifndef NTDDI_VERSION
#define NTDDI_VERSION 0x06000000  // the SDK's IFileDialog declarations
#endif
#include <afxwin.h>
#include <afxcmn.h>
#include <afxdlgs.h>
#include "vistatest.h"
#include "testutil.h"

// ---------------------------------------------------------------------
// The window that holds the controls and gets the split button's menu
// commands.

class CHostWnd : public CFrameWnd {
public:
  int m_nOne, m_nTwo;
  CHostWnd() : m_nOne(0), m_nTwo(0) {}
  afx_msg void OnOne() { m_nOne++; }
  afx_msg void OnTwo() { m_nTwo++; }
  DECLARE_MESSAGE_MAP()
};
BEGIN_MESSAGE_MAP(CHostWnd, CFrameWnd)
  ON_COMMAND(ID_SPLIT_ONE, OnOne)
  ON_COMMAND(ID_SPLIT_TWO, OnTwo)
END_MESSAGE_MAP()

// A split button that records its drop-downs.
class CTestSplit : public CSplitButton {
public:
  int m_nDrops;
  CTestSplit() : CSplitButton(IDR_SPLITMENU, 0), m_nDrops(0) {}
  afx_msg void OnDrop(NMHDR* pNMHDR, LRESULT* pResult) {
    m_nDrops++;
    OnDropDown(pNMHDR, pResult);
  }
  DECLARE_MESSAGE_MAP()
};
BEGIN_MESSAGE_MAP(CTestSplit, CSplitButton)
  ON_NOTIFY_REFLECT(BCN_DROPDOWN, OnDrop)
END_MESSAGE_MAP()

static void TestSplitButton(CHostWnd* pHost) {
  CTestSplit btn;
  CHECK(btn.Create(_T("Split"), WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, CRect(10, 10, 150, 40),
                   pHost, IDC_SPLIT));
  CHECK((btn.GetStyle() & BS_TYPEMASK) == BS_SPLITBUTTON);
  CHECK(btn.IsKindOf(RUNTIME_CLASS(CSplitButton)) && btn.IsKindOf(RUNTIME_CLASS(CButton)));

  // The split parts.
  CHECK(btn.SetSplitStyle(BCSS_STRETCH));
  UINT uStyle = btn.GetSplitStyle();
  CHECK(uStyle & BCSS_STRETCH);
  SIZE size = {20, 0};
  CHECK(btn.SetSplitSize(&size));
  SIZE sizeGot = {0, 0};
  CHECK(btn.GetSplitSize(&sizeGot) && sizeGot.cx == 20);
  BUTTON_SPLITINFO info;
  memset(&info, 0, sizeof(info));
  info.mask = BCSIF_STYLE | BCSIF_SIZE;
  CHECK(btn.GetSplitInfo(&info) && info.size.cx == 20);
  CHECK(btn.SetSplitGlyph(_T('u')));
  TCHAR chGlyph = btn.GetSplitGlyph();
  btn.SetDropDownState(TRUE);  // (Wine answers FALSE, but does it)
  CHECK((btn.GetState() & BST_DROPDOWNPUSHED) != 0);
  btn.SetDropDownState(FALSE);

  // The drop-down: the arrow's notification, as the button sends it.
  // Keys queued first pick the menu's first item.
  NMBCDROPDOWN nm;
  memset(&nm, 0, sizeof(nm));
  nm.hdr.hwndFrom = btn.m_hWnd;
  nm.hdr.idFrom = IDC_SPLIT;
  nm.hdr.code = BCN_DROPDOWN;
  btn.GetClientRect(&nm.rcButton);
  pHost->PostMessage(WM_KEYDOWN, VK_DOWN, 0);
  pHost->PostMessage(WM_KEYDOWN, VK_RETURN, 0);
  pHost->SendMessage(WM_NOTIFY, IDC_SPLIT, (LPARAM)&nm);
  TestPump();
  CHECK(btn.m_nDrops == 1);
  CHECK(pHost->m_nOne == 1 && pHost->m_nTwo == 0);

  // A menu of the program's own instead.
  CMenu menu;
  menu.CreatePopupMenu();
  menu.AppendMenu(MF_STRING, ID_SPLIT_TWO, _T("Two"));
  btn.SetDropDownMenu(&menu);
  pHost->PostMessage(WM_KEYDOWN, VK_DOWN, 0);
  pHost->PostMessage(WM_KEYDOWN, VK_RETURN, 0);
  pHost->SendMessage(WM_NOTIFY, IDC_SPLIT, (LPARAM)&nm);
  TestPump();
  CHECK(btn.m_nDrops == 2 && pHost->m_nTwo == 1);

  TestSaveWindowBitmap(pHost->m_hWnd, _T("vistatest-split.bmp"));
  TestLog("split button: style %x, size %d, glyph %d, commands %d %d", uStyle, (int)sizeGot.cx,
          (int)chGlyph, pHost->m_nOne, pHost->m_nTwo);
  btn.DestroyWindow();
}

static void TestCommandLink(CHostWnd* pHost) {
  CButton btn;
  CHECK(btn.Create(_T("Do the thing"), WS_CHILD | WS_VISIBLE | BS_COMMANDLINK,
                   CRect(10, 50, 300, 120), pHost, IDC_CMDLINK));
  CHECK(btn.SetNote(_T("A longer note under it")));
  CHECK(btn.GetNoteLength() == 22);
  CHECK(btn.GetNote() == _T("A longer note under it"));
  TCHAR sz[64];
  UINT cch = 64;
  CHECK(btn.GetNote(sz, &cch) && lstrcmp(sz, _T("A longer note under it")) == 0);
  TCHAR szSmall[4];
  cch = 4;
  CHECK(!btn.GetNote(szSmall, &cch));  // too small
  btn.SetShield(TRUE);

  SIZE size = {0, 0};
  CHECK(btn.GetIdealSize(&size) && size.cy > 0);  // (Wine gives a command link no width)
  RECT rcMargin = {4, 3, 4, 3}, rcGot = {0, 0, 0, 0};
  CHECK(btn.SetTextMargin(&rcMargin));
  CHECK(btn.GetTextMargin(&rcGot) && rcGot.left == 4 && rcGot.top == 3);

  CImageList il;
  il.Create(16, 16, ILC_COLOR32, 1, 1);
  BUTTON_IMAGELIST bil;
  memset(&bil, 0, sizeof(bil));
  bil.himl = il.m_hImageList;
  bil.uAlign = BUTTON_IMAGELIST_ALIGN_LEFT;
  CHECK(btn.SetImageList(&bil));
  BUTTON_IMAGELIST bilGot;
  memset(&bilGot, 0, sizeof(bilGot));
  CHECK(btn.GetImageList(&bilGot) && bilGot.himl == il.m_hImageList);

  TestSaveWindowBitmap(pHost->m_hWnd, _T("vistatest-cmdlink.bmp"));
  TestLog("command link: ideal %dx%d, note %d", (int)size.cx, (int)size.cy,
          (int)btn.GetNoteLength());
  btn.DestroyWindow();
}

static void TestNetAddress(CHostWnd* pHost) {
  CNetAddressCtrl ctl;
  if (!ctl.Create(WS_CHILD | WS_VISIBLE | WS_BORDER, CRect(10, 130, 300, 155), pHost,
                  IDC_NETADDR)) {
    CHECK(ctl.m_hWnd == NULL);
    TestLog("net address: not available here");
    return;
  }
  CHECK(SUCCEEDED(ctl.SetAllowType(NET_STRING_IPV4_ADDRESS)));
  CHECK(ctl.GetAllowType() == NET_STRING_IPV4_ADDRESS);
  ctl.SetWindowText(_T("192.168.10.20"));
  // NET_ADDRESS_INFO is large; the control fills it in.
  static BYTE info[1024];
  NC_ADDRESS addr;
  memset(&addr, 0, sizeof(addr));
  addr.pAddrInfo = (struct NET_ADDRESS_INFO_*)info;
  HRESULT hr = ctl.GetAddress(&addr);
  CHECK(hr == S_OK);
  TestLog("net address: GetAddress %08lx", (unsigned long)hr);
  ctl.DestroyWindow();
}

// ---------------------------------------------------------------------
// The Vista file dialog, driven by a thread timer: once the dialog's
// window is up, the step function fills it in and presses a button.

class CTestFileDialog;
static CTestFileDialog* g_pDriven = NULL;

static HWND DialogWindow(CFileDialog* pDlg) {
  HWND hWnd = NULL;
  IFileOpenDialog* pOpen = pDlg->GetIFileOpenDialog();
  IFileSaveDialog* pSave = pOpen == NULL ? pDlg->GetIFileSaveDialog() : NULL;
  IUnknown* pUnk = pOpen != NULL ? (IUnknown*)pOpen : (IUnknown*)pSave;
  IOleWindow* pWin = NULL;
  if (pUnk != NULL && SUCCEEDED(pUnk->QueryInterface(IID_IOleWindow, (void**)&pWin))) {
    pWin->GetWindow(&hWnd);
    pWin->Release();
  }
  if (pUnk != NULL) pUnk->Release();
  return hWnd;
}

class CTestFileDialog : public CFileDialog {
public:
  int m_nFolderChanges, m_nFileOK, m_nRefuse, m_nTicks;
  const WCHAR* m_pszName;  // what to type, or NULL to keep what is there
  UINT m_nButton;          // IDOK or IDCANCEL
  CTestFileDialog(BOOL bOpen, LPCTSTR lpszDefExt, LPCTSTR lpszFileName, DWORD dwFlags,
                  LPCTSTR lpszFilter = NULL)
      : CFileDialog(bOpen, lpszDefExt, lpszFileName, dwFlags, lpszFilter, NULL, 0, TRUE),
        m_nFolderChanges(0), m_nFileOK(0), m_nRefuse(0), m_nTicks(0), m_pszName(NULL),
        m_nButton(IDOK) {}
  virtual void OnFolderChange() { m_nFolderChanges++; }
  virtual BOOL OnFileNameOK() {
    m_nFileOK++;
    if (m_nRefuse > 0) {
      // Turned down: the dialog stays, and OK is pressed again.
      m_nRefuse--;
      ::PostMessage(DialogWindow(this), WM_COMMAND, IDOK, 0);
      return TRUE;
    }
    return FALSE;
  }
  virtual void Step(HWND hWnd) {
    if (m_pszName != NULL) {
      IFileDialog* pfd = m_bOpenFileDialog ? (IFileDialog*)GetIFileOpenDialog()
                                           : (IFileDialog*)GetIFileSaveDialog();
      if (pfd != NULL) {
        pfd->SetFileName(m_pszName);
        pfd->Release();
      }
    }
    ::PostMessage(hWnd, WM_COMMAND, m_nButton, 0);
  }
  INT_PTR Run();
};

static void CALLBACK DriveTimer(HWND, UINT, UINT_PTR nIDEvent, DWORD) {
  CTestFileDialog* pDlg = g_pDriven;
  if (pDlg == NULL) return;
  HWND hWnd = DialogWindow(pDlg);
  if (++pDlg->m_nTicks > 100) {  // stuck: give up
    if (hWnd != NULL) ::PostMessage(hWnd, WM_COMMAND, IDCANCEL, 0);
    return;
  }
  if (hWnd == NULL || !::IsWindowVisible(hWnd)) return;
  ::KillTimer(NULL, nIDEvent);
  g_pDriven = NULL;
  pDlg->Step(hWnd);
}

INT_PTR CTestFileDialog::Run() {
  g_pDriven = this;
  UINT_PTR nTimer = ::SetTimer(NULL, 0, 100, DriveTimer);
  INT_PTR nResult = DoModal();
  ::KillTimer(NULL, nTimer);
  g_pDriven = NULL;
  return nResult;
}

static BOOL EndsWith(const CString& str, LPCTSTR lpszEnd) {
  int n = lstrlen(lpszEnd);
  return str.GetLength() >= n && str.Right(n).CompareNoCase(lpszEnd) == 0;
}

static void MakeFile(const CString& strPath) {
  CFile f;
  if (f.Open(strPath, CFile::modeCreate | CFile::modeWrite)) {
    f.Write("x", 1);
    f.Close();
  }
}

// A customized open dialog: a check box and an edit box set while it
// runs, read back after; the first OK turned down.
class CCustomOpen : public CTestFileDialog {
public:
  CCustomOpen(LPCTSTR lpszFilter)
      : CTestFileDialog(TRUE, NULL, NULL, OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR, lpszFilter) {}
  virtual void Step(HWND hWnd) {
    CHECK(SUCCEEDED(SetCheckButtonState(500, TRUE)));
    CHECK(SUCCEEDED(SetEditBoxText(501, _T("typed"))));
    CTestFileDialog::Step(hWnd);
  }
};

// The folder picker: OK on the folder it starts in; if that leaves it
// open, the folder's path typed in and OK again; then Cancel.
static CFileDialog* g_pFolderDlg = NULL;
static CString g_strFolder;
static int g_nFolderTicks = 0, g_nFolderStep = 0;
static void CALLBACK FolderTimer(HWND, UINT, UINT_PTR, DWORD) {
  HWND hWnd = DialogWindow(g_pFolderDlg);
  if (hWnd == NULL || !::IsWindowVisible(hWnd)) return;
  if (g_nFolderTicks++ % 10 != 0) return;
  switch (g_nFolderStep++) {
    case 0: break;
    case 1: {
      IFileOpenDialog* pOpen = g_pFolderDlg->GetIFileOpenDialog();
      if (pOpen != NULL) {
        pOpen->SetFileName(CStringW(g_strFolder));
        pOpen->Release();
      }
      break;
    }
    default:
      ::PostMessage(hWnd, WM_COMMAND, IDCANCEL, 0);
      return;
  }
  ::PostMessage(hWnd, WM_COMMAND, IDOK, 0);
}

static void TestFileDialogs() {
  CString strDir = TestOutPath(_T("vistadlg"));
  ::CreateDirectory(strDir, NULL);
  TCHAR szFull[MAX_PATH];
  ::GetFullPathName(strDir, MAX_PATH, szFull, NULL);
  strDir = szFull;
  MakeFile(strDir + _T("\\a.txt"));
  MakeFile(strDir + _T("\\b.txt"));
  ::DeleteFile(strDir + _T("\\newfile.txt"));

  {
    CFileDialog probe(TRUE);
    IFileOpenDialog* pOpen = probe.GetIFileOpenDialog();
    if (pOpen == NULL) {
      // No IFileDialog (XP): the classic dialog does the work, and the
      // customization calls say so.
      CHECK(probe.GetIFileDialogCustomize() == NULL);
      CHECK(probe.AddCheckButton(500, _T("x"), FALSE) == E_NOTIMPL);
      TestLog("file dialogs: no Vista dialog here");
      return;
    }
    pOpen->Release();
  }

  // Open one file, customized.
  {
    CCustomOpen dlg(_T("Text files (*.txt)|*.txt|All files|*.*||"));
    dlg.m_ofn.lpstrInitialDir = strDir;
    CHECK(SUCCEEDED(dlg.AddCheckButton(500, _T("Read only"), FALSE)));
    CHECK(SUCCEEDED(dlg.AddEditBox(501, _T("start"))));
    CHECK(SUCCEEDED(dlg.StartVisualGroup(502, _T("Group"))));
    CHECK(SUCCEEDED(dlg.AddText(503, _T("Some text"))));
    CHECK(SUCCEEDED(dlg.EndVisualGroup()));
    dlg.m_pszName = L"a.txt";
    dlg.m_nRefuse = 1;
    INT_PTR nResult = dlg.Run();
    CHECK(nResult == IDOK);
    CHECK(dlg.m_nFileOK == 2);
    CHECK(dlg.m_nFolderChanges >= 1);
    CHECK(EndsWith(dlg.GetPathName(), _T("\\a.txt")));
    CHECK(dlg.GetFileName() == _T("a.txt"));
    CHECK(dlg.GetFileExt() == _T("txt") && dlg.GetFileTitle() == _T("a"));
    CHECK(dlg.GetFolderPath().CompareNoCase(strDir) == 0);
    CHECK(dlg.m_ofn.nFilterIndex == 1);
    CHECK(lstrcmp(dlg.m_ofn.lpstrFile + dlg.m_ofn.nFileOffset, _T("a.txt")) == 0);
    CHECK(lstrcmp(dlg.m_ofn.lpstrFile + dlg.m_ofn.nFileExtension, _T("txt")) == 0);
    CHECK(lstrcmp(dlg.m_szFileTitle, _T("a.txt")) == 0);
    BOOL bChecked = FALSE;
    CHECK(SUCCEEDED(dlg.GetCheckButtonState(500, bChecked)) && bChecked);
    CString strText;
    CHECK(SUCCEEDED(dlg.GetEditBoxText(501, strText)) && strText == _T("typed"));
    TestLog("open: %d, %s, OK %d, folder changes %d, check %d, edit %s", (int)nResult,
            (LPCSTR)CStringA(dlg.GetPathName()), dlg.m_nFileOK, dlg.m_nFolderChanges,
            (int)bChecked, (LPCSTR)CStringA(strText));
  }

  // Two files.
  {
    CTestFileDialog dlg(TRUE, NULL, NULL,
                        OFN_ALLOWMULTISELECT | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR);
    dlg.m_ofn.lpstrInitialDir = strDir;
    dlg.m_pszName = L"\"a.txt\" \"b.txt\"";
    INT_PTR nResult = dlg.Run();
    CHECK(nResult == IDOK);
    int nFiles = 0;
    BOOL bA = FALSE, bB = FALSE;
    POSITION pos = dlg.GetStartPosition();
    while (pos != NULL) {
      CString str = dlg.GetNextPathName(pos);
      nFiles++;
      if (EndsWith(str, _T("\\a.txt"))) bA = TRUE;
      if (EndsWith(str, _T("\\b.txt"))) bB = TRUE;
    }
    CHECK(nFiles == 2 && bA && bB);
    TestLog("open two: %d, %d files", (int)nResult, nFiles);
  }

  // Save, with the default extension added.
  {
    CTestFileDialog dlg(FALSE, _T("txt"), _T("newfile"), OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR);
    dlg.m_ofn.lpstrInitialDir = strDir;
    INT_PTR nResult = dlg.Run();
    CHECK(nResult == IDOK);
    CHECK(dlg.GetIFileSaveDialog() != NULL);
    CHECK(EndsWith(dlg.GetPathName(), _T("\\newfile.txt")));
    TestLog("save: %d, %s", (int)nResult, (LPCSTR)CStringA(dlg.GetFileName()));
  }

  // Cancel.
  TestLog("cancel...");
  {
    CTestFileDialog dlg(TRUE, NULL, NULL, OFN_NOCHANGEDIR);
    dlg.m_ofn.lpstrInitialDir = strDir;
    dlg.m_nButton = IDCANCEL;
    CHECK(dlg.Run() == IDCANCEL);
  }

  // A folder.
  TestLog("folder...");
  {
    CFolderPickerDialog dlg(strDir);
    g_pFolderDlg = &dlg;
    g_strFolder = strDir;
    UINT_PTR nTimer = ::SetTimer(NULL, 0, 100, FolderTimer);
    INT_PTR nResult = dlg.DoModal();
    ::KillTimer(NULL, nTimer);
    CHECK(nResult == IDOK);
    CHECK(dlg.GetPathName().CompareNoCase(strDir) == 0);
    CHECK(dlg.GetFolderPath().CompareNoCase(strDir) == 0);
    TestLog("folder: %d, %s, step %d", (int)nResult, (LPCSTR)CStringA(dlg.GetPathName()),
            g_nFolderStep);
  }
}

class CVistaApp : public CWinApp {
public:
  virtual BOOL InitInstance() {
    TestOpenLog(_T("vistatest.log"));
    ::InitCommonControls();
    CHostWnd* pHost = new CHostWnd;
    if (!pHost->Create(NULL, _T("vistatest"), WS_OVERLAPPEDWINDOW, CRect(0, 0, 400, 260))) {
      TestLog("Create failed");
      g_nFailures++;
      return FALSE;
    }
    m_pMainWnd = pHost;
    pHost->ShowWindow(SW_SHOW);
    pHost->UpdateWindow();

    TestSplitButton(pHost);
    TestCommandLink(pHost);
    TestNetAddress(pHost);
    TestFileDialogs();

    pHost->DestroyWindow();
    m_pMainWnd = NULL;
    return FALSE;
  }
  virtual int ExitInstance() {
    CWinApp::ExitInstance();
    return TestFinish();
  }
};

CVistaApp theApp;
