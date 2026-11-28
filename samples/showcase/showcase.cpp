// showcase.cpp - a small MFC program that shows the usual parts of an MFC
// program: a frame with a menu bar, a toolbar and a status bar, a view
// that draws with GDI, and two modal dialogs with standard and common
// controls. It builds unchanged for Windows and over the portable layer
// (Linux, macOS, Haiku).
//
// SHOWCASE_DEMO=1 in the environment opens the dialogs by itself and ends
// the program after some seconds, for screenshots in a test machine.
// SHOWCASE_SHOT names a command that the program runs (with system()) at
// each step of the demonstration, with the step's name as its argument.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include <afxwin.h>
#include <afxext.h>
#include <afxcmn.h>
#include <stdlib.h>
#include "showcase.h"

// The settings of the drawing, which the settings dialog changes.
struct Settings {
  CString title = _T("Hello from MFC");
  int shape = 0;  // 0 ellipses, 1 rectangles, 2 rounded rectangles
  BOOL filled = TRUE;
  int size = 5;
  int color = 0;
};
static Settings g_settings;

static const COLORREF kColors[] = {RGB(0x2E, 0x6E, 0xC8), RGB(0xC8, 0x3C, 0x2E), RGB(0x2E, 0x9C, 0x4A),
                                   RGB(0xD0, 0x8A, 0x10)};
static const TCHAR* const kColorNames[] = {_T("Blue"), _T("Red"), _T("Green"), _T("Orange")};

// Runs the screenshot command of SHOWCASE_SHOT, if there is one, for a
// step of the demonstration.
static void Shot(const char* step) {
  const char* cmd = getenv("SHOWCASE_SHOT");
  if (!cmd || !*cmd) return;
  CStringA line;
  line.Format("%s %s", cmd, step);
  int ignored = system(line);
  (void)ignored;
}

// The settings dialog: an edit box, a drop-down list, a check box, a
// trackbar and a list box.
class CSettingsDlg : public CDialog {
 public:
  // Makes the dialog from its template.
  CSettingsDlg(CWnd* parent) : CDialog(IDD_SETTINGS, parent), m_demo(FALSE) {}

  BOOL m_demo;

  // Fills the controls from the settings.
  virtual BOOL OnInitDialog() {
    CDialog::OnInitDialog();
    SetDlgItemText(IDC_NAME, g_settings.title);
    CComboBox* shape = (CComboBox*)GetDlgItem(IDC_SHAPE);
    shape->AddString(_T("Ellipses"));
    shape->AddString(_T("Rectangles"));
    shape->AddString(_T("Rounded"));
    shape->SetCurSel(g_settings.shape);
    CheckDlgButton(IDC_FILLED, g_settings.filled ? BST_CHECKED : BST_UNCHECKED);
    CSliderCtrl* size = (CSliderCtrl*)GetDlgItem(IDC_SHAPESIZE);
    size->SetRange(1, 10);
    size->SetPos(g_settings.size);
    CListBox* colors = (CListBox*)GetDlgItem(IDC_COLORS);
    for (const TCHAR* name : kColorNames) colors->AddString(name);
    colors->SetCurSel(g_settings.color);
    if (m_demo) SetTimer(1, 2500, NULL);
    return TRUE;
  }

  // Keeps the values of the controls in the settings.
  virtual void OnOK() {
    GetDlgItemText(IDC_NAME, g_settings.title);
    g_settings.shape = ((CComboBox*)GetDlgItem(IDC_SHAPE))->GetCurSel();
    g_settings.filled = IsDlgButtonChecked(IDC_FILLED) == BST_CHECKED;
    g_settings.size = ((CSliderCtrl*)GetDlgItem(IDC_SHAPESIZE))->GetPos();
    g_settings.color = ((CListBox*)GetDlgItem(IDC_COLORS))->GetCurSel();
    CDialog::OnOK();
  }

  // In the demonstration: changes the settings, takes the screenshot and
  // closes the dialog.
  afx_msg void OnTimer(UINT_PTR) {
    KillTimer(1);
    SetDlgItemText(IDC_NAME, _T("MFC on Haiku"));
    ((CComboBox*)GetDlgItem(IDC_SHAPE))->SetCurSel(2);
    ((CSliderCtrl*)GetDlgItem(IDC_SHAPESIZE))->SetPos(7);
    ((CListBox*)GetDlgItem(IDC_COLORS))->SetCurSel(3);
    UpdateWindow();
    Shot("settings");
    OnOK();
  }
  DECLARE_MESSAGE_MAP()
};
BEGIN_MESSAGE_MAP(CSettingsDlg, CDialog)
  ON_WM_TIMER()
END_MESSAGE_MAP()

// The about box. In the demonstration it closes itself.
class CAboutDlg : public CDialog {
 public:
  // Makes the dialog from its template.
  CAboutDlg(CWnd* parent) : CDialog(IDD_ABOUTBOX, parent), m_demo(FALSE) {}

  BOOL m_demo;

  // Starts the timer of the demonstration.
  virtual BOOL OnInitDialog() {
    CDialog::OnInitDialog();
    if (m_demo) SetTimer(1, 2500, NULL);
    return TRUE;
  }

  // Takes the screenshot and closes the dialog.
  afx_msg void OnTimer(UINT_PTR) {
    KillTimer(1);
    Shot("about");
    EndDialog(IDOK);
  }
  DECLARE_MESSAGE_MAP()
};
BEGIN_MESSAGE_MAP(CAboutDlg, CDialog)
  ON_WM_TIMER()
END_MESSAGE_MAP()

// The document holds nothing: the drawing comes from the settings.
class CShowDoc : public CDocument {
  DECLARE_DYNCREATE(CShowDoc)
};
IMPLEMENT_DYNCREATE(CShowDoc, CDocument)

// The view draws a gradient, shapes and text.
class CShowView : public CView {
  DECLARE_DYNCREATE(CShowView)
 public:
  // Draws the view from the settings.
  virtual void OnDraw(CDC* dc) {
    CRect rc;
    GetClientRect(&rc);
    // A vertical gradient as the background.
    for (int y = 0; y < rc.Height(); y += 4) {
      int t = rc.Height() > 0 ? y * 255 / rc.Height() : 0;
      dc->FillSolidRect(0, y, rc.Width(), 4, RGB(240 - t / 8, 244 - t / 10, 250 - t / 16));
    }
    COLORREF color = kColors[g_settings.color >= 0 && g_settings.color < 4 ? g_settings.color : 0];
    CPen pen(PS_SOLID, 2, color);
    CBrush brush(color);
    CPen* oldPen = dc->SelectObject(&pen);
    CBrush* oldBrush = g_settings.filled ? dc->SelectObject(&brush) : (CBrush*)dc->SelectStockObject(NULL_BRUSH);
    int step = 12 + g_settings.size * 6;
    for (int i = 0; i < 6; i++) {
      CRect r(20 + i * (step + 10), 70 + (i % 2) * 20, 20 + i * (step + 10) + step, 70 + (i % 2) * 20 + step);
      if (g_settings.shape == 0)
        dc->Ellipse(r);
      else if (g_settings.shape == 1)
        dc->Rectangle(r);
      else
        dc->RoundRect(r, CPoint(12, 12));
    }
    dc->SelectObject(oldPen);
    dc->SelectObject(oldBrush);
    // The title in a big font.
    CFont font;
    font.CreatePointFont(200, _T("Arial"), dc);
    CFont* oldFont = dc->SelectObject(&font);
    dc->SetBkMode(TRANSPARENT);
    dc->SetTextColor(RGB(0x20, 0x20, 0x30));
    dc->TextOut(20, 20, g_settings.title);
    dc->SelectObject(oldFont);
    CString line;
    line.Format(_T("This window is %d x %d pixels."), rc.Width(), rc.Height());
    dc->TextOut(20, rc.bottom - 30, line);
  }
};
IMPLEMENT_DYNCREATE(CShowView, CView)

static const UINT kIndicators[] = {ID_SEPARATOR, ID_INDICATOR_CAPS, ID_INDICATOR_NUM};

// The frame: toolbar, status bar, and the commands of the View menu.
class CMainFrame : public CFrameWnd {
  DECLARE_DYNCREATE(CMainFrame)
 public:
  CToolBar m_toolBar;
  CStatusBar m_statusBar;
  int m_step = 0;

  // Makes the bars, and starts the demonstration when it is asked for.
  afx_msg int OnCreate(LPCREATESTRUCT cs) {
    if (CFrameWnd::OnCreate(cs) == -1) return -1;
    if (!m_toolBar.CreateEx(this, TBSTYLE_FLAT, WS_CHILD | WS_VISIBLE | CBRS_TOP | CBRS_TOOLTIPS | CBRS_FLYBY) ||
        !m_toolBar.LoadToolBar(IDR_MAINFRAME))
      return -1;
    if (!m_statusBar.Create(this) || !m_statusBar.SetIndicators(kIndicators, 3)) return -1;
    const char* demo = getenv("SHOWCASE_DEMO");
    if (demo && *demo) SetTimer(1, 3000, NULL);
    return 0;
  }

  // Opens the settings dialog and redraws the view with its settings.
  afx_msg void OnSettings() { Settings(FALSE); }

  // Opens the settings dialog; in the demonstration it closes itself.
  void Settings(BOOL demo) {
    CSettingsDlg dlg(this);
    dlg.m_demo = demo;
    if (dlg.DoModal() == IDOK) {
      SetWindowText(g_settings.title);
      if (CView* v = GetActiveView()) v->Invalidate();
    }
  }

  // The steps of the demonstration: the main window, the settings
  // dialog, the window with the new settings, the about box, the end.
  afx_msg void OnTimer(UINT_PTR id) {
    if (id != 1) return;
    switch (m_step++) {
      case 0:
        Shot("main");
        break;
      case 1:
        KillTimer(1);
        Settings(TRUE);
        SetTimer(1, 2000, NULL);
        break;
      case 2:
        UpdateWindow();
        Shot("changed");
        break;
      case 3: {
        KillTimer(1);
        CAboutDlg dlg(this);
        dlg.m_demo = TRUE;
        dlg.DoModal();
        SetTimer(1, 1000, NULL);
        break;
      }
      default:
        KillTimer(1);
        PostMessage(WM_CLOSE);
    }
  }
  DECLARE_MESSAGE_MAP()
};
IMPLEMENT_DYNCREATE(CMainFrame, CFrameWnd)
BEGIN_MESSAGE_MAP(CMainFrame, CFrameWnd)
  ON_WM_CREATE()
  ON_WM_TIMER()
  ON_COMMAND(ID_VIEW_SETTINGS, OnSettings)
END_MESSAGE_MAP()

// The application: one document template, and the about box.
class CShowApp : public CWinApp {
 public:
  // Makes the main frame with its document and view.
  virtual BOOL InitInstance() {
    CWinApp::InitInstance();
    AddDocTemplate(new CSingleDocTemplate(IDR_MAINFRAME, RUNTIME_CLASS(CShowDoc), RUNTIME_CLASS(CMainFrame),
                                          RUNTIME_CLASS(CShowView)));
    CCommandLineInfo info;
    ParseCommandLine(info);
    if (!ProcessShellCommand(info)) return FALSE;
    m_pMainWnd->SetWindowPos(NULL, 120, 90, 640, 420, SWP_NOZORDER);
    m_pMainWnd->SetWindowText(_T("Showcase"));
    m_pMainWnd->ShowWindow(SW_SHOW);
    m_pMainWnd->UpdateWindow();
    return TRUE;
  }

  // Opens the about box.
  afx_msg void OnAppAbout() {
    CAboutDlg dlg(m_pMainWnd);
    dlg.DoModal();
  }
  DECLARE_MESSAGE_MAP()
};
BEGIN_MESSAGE_MAP(CShowApp, CWinApp)
  ON_COMMAND(ID_APP_ABOUT, OnAppAbout)
  ON_COMMAND(ID_FILE_NEW, CWinApp::OnFileNew)
END_MESSAGE_MAP()

CShowApp theApp;
