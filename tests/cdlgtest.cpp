// cdlgtest.cpp - the common dialogs, driven through their hooks with the
// control identifiers of <dlgs.h>: CFileDialog opening one file, two at
// once and none, saving with the default extension, a hook turning the
// first answer down, and a buffer too small; CColorDialog with a color
// set by SETRGBSTRING and then edited; CFontDialog starting from a
// LOGFONT; and the modeless CFindReplaceDialog telling its owner what
// was asked for.
#include <afxwin.h>
#include <afxdlgs.h>
#include <dlgs.h>
#include "testutil.h"

// ---------------------------------------------------------------------
// The file dialogs: the hook types the name it is given and presses OK.

static struct {
  CString strText;      // what to type in the name box
  BOOL bCancel;         // cancel instead
  int nReject;          // file-OK notifications to turn down
  int nInit, nFileOk;
  CString strFolder, strSpec;
} g_file;

static void ResetFile(LPCTSTR lpszText) {
  g_file.strText = lpszText;
  g_file.bCancel = FALSE;
  g_file.nReject = 0;
  g_file.nInit = g_file.nFileOk = 0;
  g_file.strFolder.Empty();
  g_file.strSpec.Empty();
}

static UINT_PTR CALLBACK FileHook(HWND hChild, UINT msg, WPARAM, LPARAM lParam) {
  if (msg != WM_NOTIFY) return 0;
  OFNOTIFY* pNotify = (OFNOTIFY*)lParam;
  HWND hDlg = ::GetParent(hChild);
  TCHAR sz[MAX_PATH];
  switch (pNotify->hdr.code) {
    case CDN_INITDONE:
      g_file.nInit++;
      sz[0] = 0;
      ::SendMessage(hDlg, CDM_GETFOLDERPATH, MAX_PATH, (LPARAM)sz);
      g_file.strFolder = sz;
      if (g_file.bCancel) {
        ::PostMessage(hDlg, WM_COMMAND, IDCANCEL, 0);
      } else {
        // The name box is cmb13 in newer dialogs, edt1 in older ones.
        ::SendMessage(hDlg, CDM_SETCONTROLTEXT, ::GetDlgItem(hDlg, cmb13) ? cmb13 : edt1,
                      (LPARAM)(LPCTSTR)g_file.strText);
        ::PostMessage(hDlg, WM_COMMAND, IDOK, 0);
      }
      break;
    case CDN_FILEOK:
      g_file.nFileOk++;
      sz[0] = 0;
      ::SendMessage(hDlg, CDM_GETSPEC, MAX_PATH, (LPARAM)sz);
      g_file.strSpec = sz;
      if (g_file.nReject > 0) {
        g_file.nReject--;
        ::SetWindowLongPtr(hChild, DWLP_MSGRESULT, 1);
        ::PostMessage(hDlg, WM_COMMAND, IDOK, 0);
        return 1;
      }
      break;
  }
  return 0;
}

static void Hook(CFileDialog& dlg, LPCTSTR lpszDir) {
  dlg.m_ofn.Flags |= OFN_ENABLEHOOK;
  dlg.m_ofn.lpfnHook = FileHook;
  dlg.m_ofn.lpstrInitialDir = lpszDir;
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

static void TestFileDialogs() {
  TestLog("%s...", "file dialogs");
  CString strDir = TestOutPath(_T("cdlg"));
  ::CreateDirectory(strDir, NULL);
  CString strSep = strDir.Find(_T('\\')) >= 0 ? _T("\\") : _T("/");
  MakeFile(strDir + strSep + _T("a.txt"));
  MakeFile(strDir + strSep + _T("b.txt"));
  MakeFile(strDir + strSep + _T("c.dat"));
  ::DeleteFile(strDir + strSep + _T("newfile.txt"));

  // Open one file, the hook turning the first OK down.
  {
    CFileDialog dlg(TRUE, NULL, NULL, OFN_FILEMUSTEXIST | OFN_HIDEREADONLY | OFN_NOCHANGEDIR,
                    _T("Text files (*.txt)|*.txt|Data files (*.dat)|*.dat|All files|*.*||"));
    Hook(dlg, strDir);
    ResetFile(_T("a.txt"));
    g_file.nReject = 1;
    CHECK(dlg.DoModal() == IDOK);
    CHECK(g_file.nInit == 1);
    CHECK(g_file.nFileOk == 2);
    CHECK(EndsWith(g_file.strFolder, _T("cdlg")));
    CHECK(g_file.strSpec == _T("a.txt"));
    CHECK(dlg.GetFileName() == _T("a.txt"));
    CHECK(dlg.GetFileExt() == _T("txt"));
    CHECK(dlg.GetFileTitle() == _T("a"));
    CHECK(EndsWith(dlg.GetFolderPath(), _T("cdlg")));
    CHECK(EndsWith(dlg.GetPathName(), _T("a.txt")));
    CHECK(dlg.m_ofn.nFileOffset == dlg.GetPathName().GetLength() - 5);
    CHECK(dlg.m_ofn.nFileExtension == dlg.GetPathName().GetLength() - 3);
    CHECK(::GetFileAttributes(dlg.GetPathName()) != INVALID_FILE_ATTRIBUTES);
    TestLog("open: %d init, %d file-ok, path %s", g_file.nInit, g_file.nFileOk,
            (const char*)CStringA(dlg.GetPathName()));
  }

  // Two files at once.
  {
    CFileDialog dlg(TRUE, NULL, NULL, OFN_ALLOWMULTISELECT | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR, NULL);
    Hook(dlg, strDir);
    ResetFile(_T("\"a.txt\" \"b.txt\""));
    CHECK(dlg.DoModal() == IDOK);
    POSITION pos = dlg.GetStartPosition();
    CStringArray names;
    while (pos != NULL) names.Add(dlg.GetNextPathName(pos));
    CHECK(names.GetSize() == 2);
    if (names.GetSize() == 2) {
      CHECK(EndsWith(names[0], strSep + _T("a.txt")) || EndsWith(names[0], _T("\\a.txt")));
      CHECK(EndsWith(names[1], _T("b.txt")));
      CHECK(::GetFileAttributes(names[0]) != INVALID_FILE_ATTRIBUTES);
      CHECK(::GetFileAttributes(names[1]) != INVALID_FILE_ATTRIBUTES);
    }
  }

  // Save, the default extension added.
  {
    CFileDialog dlg(FALSE, _T("txt"), NULL, OFN_OVERWRITEPROMPT | OFN_HIDEREADONLY | OFN_NOCHANGEDIR, NULL);
    Hook(dlg, strDir);
    ResetFile(_T("newfile"));
    CHECK(dlg.DoModal() == IDOK);
    CHECK(dlg.GetFileName() == _T("newfile.txt"));
    CHECK(EndsWith(dlg.GetFolderPath(), _T("cdlg")));
  }

  // Cancel.
  {
    CFileDialog dlg(TRUE, NULL, NULL, OFN_NOCHANGEDIR, NULL);
    Hook(dlg, strDir);
    ResetFile(_T(""));
    g_file.bCancel = TRUE;
    CHECK(dlg.DoModal() == IDCANCEL);
    CHECK(g_file.nInit == 1);
    CHECK(::CommDlgExtendedError() == 0);
  }

  // A buffer too small for the answer.
  {
    TCHAR szFile[4] = _T("");
    OPENFILENAME ofn;
    memset(&ofn, 0, sizeof(ofn));
    ofn.lStructSize = OPENFILENAME_SIZE_VERSION_400;
    ofn.hwndOwner = AfxGetMainWnd()->GetSafeHwnd();
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = 4;
    ofn.lpstrInitialDir = strDir;
    ofn.Flags = OFN_EXPLORER | OFN_ENABLEHOOK | OFN_NOCHANGEDIR;
    ofn.lpfnHook = FileHook;
    ResetFile(_T("a.txt"));
    CHECK(!::GetOpenFileName(&ofn));
    CHECK(::CommDlgExtendedError() == FNERR_BUFFERTOOSMALL);
  }
}

// ---------------------------------------------------------------------
// The color dialog: a color set by SETRGBSTRING, then its blue edited.

static UINT g_msgSetRgb;
static int g_nColorInit;

static UINT_PTR CALLBACK ColorHook(HWND hDlg, UINT msg, WPARAM, LPARAM) {
  if (msg == WM_INITDIALOG) {
    g_nColorInit++;
    ::PostMessage(hDlg, WM_APP, 0, 0);
    return TRUE;
  }
  if (msg == WM_APP) {
    ::SendMessage(hDlg, g_msgSetRgb, 0, RGB(0, 128, 0));
    ::SetDlgItemInt(hDlg, 708, 255, FALSE);  // blue
    ::PostMessage(hDlg, WM_COMMAND, IDOK, 0);
    return TRUE;
  }
  return 0;
}

static void TestColorDialog() {
  TestLog("%s...", "color dialog");
  g_msgSetRgb = ::RegisterWindowMessage(SETRGBSTRING);
  CColorDialog dlg(RGB(10, 20, 30), CC_FULLOPEN);
  dlg.m_cc.Flags |= CC_ENABLEHOOK;
  dlg.m_cc.lpfnHook = ColorHook;
  CHECK(dlg.DoModal() == IDOK);
  CHECK(g_nColorInit == 1);
  CHECK(dlg.GetColor() == RGB(0, 128, 255));
  TestLog("color: %06lx", (unsigned long)dlg.GetColor());
}

// ---------------------------------------------------------------------
// The font dialog: a bold italic face, underlined by the hook.

static LOGFONT g_lfSeen;

static int CALLBACK FirstFace(const LOGFONT* plf, const TEXTMETRIC*, DWORD dwType, LPARAM lParam) {
  LOGFONT* pFound = (LOGFONT*)lParam;
  if (plf->lfFaceName[0] == _T('@')) return 1;
  if (pFound->lfFaceName[0] == 0 || (dwType & TRUETYPE_FONTTYPE)) {
    lstrcpyn(pFound->lfFaceName, plf->lfFaceName, LF_FACESIZE);
    if (dwType & TRUETYPE_FONTTYPE) return 0;
  }
  return 1;
}

static UINT_PTR CALLBACK FontHook(HWND hDlg, UINT msg, WPARAM, LPARAM) {
  if (msg == WM_INITDIALOG) {
    ::PostMessage(hDlg, WM_APP, 0, 0);
    return TRUE;
  }
  if (msg == WM_APP) {
    ::CheckDlgButton(hDlg, chx2, BST_CHECKED);  // underline
    memset(&g_lfSeen, 0, sizeof(g_lfSeen));
    ::SendMessage(hDlg, WM_CHOOSEFONT_GETLOGFONT, 0, (LPARAM)&g_lfSeen);
    ::PostMessage(hDlg, WM_COMMAND, IDOK, 0);
    return TRUE;
  }
  return 0;
}

static void TestFontDialog() {
  TestLog("%s...", "font dialog");
  LOGFONT lfFace;
  memset(&lfFace, 0, sizeof(lfFace));
  lfFace.lfCharSet = DEFAULT_CHARSET;
  HDC hdc = ::GetDC(NULL);
  LOGFONT lfAll = lfFace;
  ::EnumFontFamiliesEx(hdc, &lfAll, (FONTENUMPROC)FirstFace, (LPARAM)&lfFace, 0);
  LOGFONT lf;
  memset(&lf, 0, sizeof(lf));
  lf.lfHeight = -::MulDiv(12, ::GetDeviceCaps(hdc, LOGPIXELSY), 72);
  ::ReleaseDC(NULL, hdc);
  lf.lfWeight = FW_BOLD;
  lf.lfItalic = TRUE;
  lf.lfCharSet = DEFAULT_CHARSET;
  lstrcpyn(lf.lfFaceName, lfFace.lfFaceName, LF_FACESIZE);
  CHECK(lf.lfFaceName[0] != 0);

  CFontDialog dlg(&lf, CF_EFFECTS | CF_SCREENFONTS | CF_ENABLEHOOK);
  dlg.m_cf.lpfnHook = FontHook;
  CHECK(dlg.DoModal() == IDOK);
  CHECK(dlg.GetFaceName() == lf.lfFaceName);
  CHECK(dlg.IsBold());
  CHECK(dlg.IsItalic());
  CHECK(dlg.IsUnderline());
  CHECK(!dlg.IsStrikeOut());
  CHECK(dlg.GetSize() == 120);
  CHECK(g_lfSeen.lfUnderline);
  CHECK(lstrcmp(g_lfSeen.lfFaceName, lf.lfFaceName) == 0);
  TestLog("font: %s, %d tenths of a point", (const char*)CStringA(dlg.GetFaceName()), dlg.GetSize());
}

// ---------------------------------------------------------------------
// Find and Replace: what the owner is told.

static const UINT WM_FINDREPLACE_MSG = ::RegisterWindowMessage(FINDMSGSTRING);

static struct {
  int nCalls, nTerm;
  BOOL bNext, bReplace, bReplaceAll, bCase, bWhole, bDown;
  CString strFind, strReplace;
} g_find;

class CMainFrame : public CFrameWnd {
public:
  afx_msg void OnTimer(UINT_PTR) {
    KillTimer(1);
    TestFileDialogs();
    TestColorDialog();
    TestFontDialog();
    TestFindReplace();
    PostMessage(WM_CLOSE);
  }
  afx_msg LRESULT OnFindReplace(WPARAM, LPARAM lParam) {
    CFindReplaceDialog* pDlg = CFindReplaceDialog::GetNotifier(lParam);
    g_find.nCalls++;
    if (pDlg->IsTerminating()) {
      g_find.nTerm++;
      return 0;
    }
    g_find.bNext = pDlg->FindNext();
    g_find.bReplace = pDlg->ReplaceCurrent();
    g_find.bReplaceAll = pDlg->ReplaceAll();
    g_find.bCase = pDlg->MatchCase();
    g_find.bWhole = pDlg->MatchWholeWord();
    g_find.bDown = pDlg->SearchDown();
    g_find.strFind = pDlg->GetFindString();
    g_find.strReplace = pDlg->GetReplaceString();
    return 0;
  }
  static void Click(HWND hDlg, int nID) {
    ::SendMessage(hDlg, WM_COMMAND, MAKEWPARAM(nID, BN_CLICKED), (LPARAM)::GetDlgItem(hDlg, nID));
  }
  void TestFindReplace() {
  TestLog("%s...", "find and replace");
    // Replace All, matching case.
    CFindReplaceDialog* pDlg = new CFindReplaceDialog;
    CHECK(pDlg->Create(FALSE, _T("needle"), _T("pin"), FR_DOWN, this));
    HWND hDlg = pDlg->GetSafeHwnd();
    CHECK(::IsWindow(hDlg) && ::IsWindowVisible(hDlg));
    TCHAR sz[64] = _T("");
    ::GetDlgItemText(hDlg, edt1, sz, 64);
    CHECK(lstrcmp(sz, _T("needle")) == 0);
    ::SetDlgItemText(hDlg, edt2, _T("thread"));
    ::CheckDlgButton(hDlg, chx2, BST_CHECKED);
    Click(hDlg, psh2);
    CHECK(g_find.nCalls == 1);
    CHECK(g_find.bReplaceAll && !g_find.bNext && !g_find.bReplace);
    CHECK(g_find.bCase && !g_find.bWhole);
    CHECK(g_find.strFind == _T("needle"));
    CHECK(g_find.strReplace == _T("thread"));
    Click(hDlg, psh1);
    CHECK(g_find.nCalls == 2 && g_find.bReplace && !g_find.bReplaceAll);
    Click(hDlg, IDCANCEL);
    CHECK(g_find.nCalls == 3 && g_find.nTerm == 1);
    CHECK(!::IsWindow(hDlg));

    // Find Next, searching down.
    pDlg = new CFindReplaceDialog;
    CHECK(pDlg->Create(TRUE, _T("hay"), NULL, 0, this));
    hDlg = pDlg->GetSafeHwnd();
    CHECK(::IsDlgButtonChecked(hDlg, rad1) == BST_CHECKED);
    ::CheckRadioButton(hDlg, rad1, rad2, rad2);
    ::CheckDlgButton(hDlg, chx1, BST_CHECKED);
    Click(hDlg, IDOK);
    CHECK(g_find.nCalls == 4 && g_find.bNext && !g_find.bReplaceAll);
    CHECK(g_find.bDown && g_find.bWhole && !g_find.bCase);
    CHECK(g_find.strFind == _T("hay"));
    ::SendMessage(hDlg, WM_CLOSE, 0, 0);
    CHECK(g_find.nTerm == 2);
    CHECK(!::IsWindow(hDlg));
    TestLog("find: %d notifications", g_find.nCalls);
  }
  DECLARE_MESSAGE_MAP()
};

BEGIN_MESSAGE_MAP(CMainFrame, CFrameWnd)
  ON_WM_TIMER()
  ON_REGISTERED_MESSAGE(WM_FINDREPLACE_MSG, OnFindReplace)
END_MESSAGE_MAP()

class CCdlgApp : public CWinApp {
public:
  virtual BOOL InitInstance() {
    TestOpenLog(_T("cdlgtest.log"));
    CMainFrame* pFrame = new CMainFrame;
    if (!pFrame->Create(NULL, _T("cdlgtest"), WS_OVERLAPPEDWINDOW, CRect(40, 40, 440, 340))) {
      TestLog("frame creation failed");
      g_nFailures++;
      return FALSE;
    }
    m_pMainWnd = pFrame;
    pFrame->ShowWindow(SW_SHOW);
    pFrame->UpdateWindow();
    pFrame->SetTimer(1, 50, NULL);
    return TRUE;
  }
  virtual int ExitInstance() {
    CWinApp::ExitInstance();
    return TestFinish();
  }
};

CCdlgApp theApp;
