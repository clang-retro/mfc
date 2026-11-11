// dlltest.cpp - MFC in a DLL: the test program hosts a control that
// lives in an MFC regular DLL (dlltest-dll, built like an .ocx), as COM
// finds it through the registry:
//   - DllRegisterServer writes the control's keys, with the DLL as its
//     in-process server, and DllUnregisterServer takes them away;
//   - the DLL's COleControlModule starts when the DLL loads, with the
//     DLL's own instance and resources, apart from the program's;
//   - CWnd::CreateControl makes the control through DllGetClassObject;
//     its properties, methods and events cross from one copy of the
//     library to the other;
//   - a modeless dialog from the DLL's template sits in the program's
//     window;
//   - once the last control is gone, DllCanUnloadNow lets COM unload the
//     DLL, whose ExitInstance runs and whose window classes go.
//   Built with _AFXDLL, the program and the DLL share mfc.dll, and the
//   DLL keeps its own module state there (AFX_MANAGE_STATE).
#include <afxwin.h>
#include <afxdisp.h>
#include <afxocc.h>
#include <afxctl.h>
#include <olectl.h>
#include "dlltest.h"
#include "testutil.h"

#ifdef _UNICODE
static const TCHAR kDllName[] = _T("dlltest-dll-u.dll");
#else
static const TCHAR kDllName[] = _T("dlltest-dll-a.dll");
#endif

// {7B2E4A63-1F5C-4E8B-A034-9C6F2DBE5A70}
static const CLSID CLSID_DllCtrl = {
    0x7b2e4a63, 0x1f5c, 0x4e8b, {0xa0, 0x34, 0x9c, 0x6f, 0x2d, 0xbe, 0x5a, 0x70}};
static const TCHAR kClsidKey[] = _T("CLSID\\{7B2E4A63-1F5C-4E8B-A034-9C6F2DBE5A70}");

static BOOL ReadKey(LPCTSTR lpszKey, LPCTSTR lpszValueName, CString& str) {
  HKEY hKey;
  str.Empty();
  if (::RegOpenKeyEx(HKEY_CLASSES_ROOT, lpszKey, 0, KEY_READ, &hKey) != ERROR_SUCCESS)
    return FALSE;
  TCHAR sz[512];
  DWORD cb = sizeof(sz);
  LONG l = ::RegQueryValueEx(hKey, lpszValueName, NULL, NULL, (LPBYTE)sz, &cb);
  ::RegCloseKey(hKey);
  if (l != ERROR_SUCCESS) return FALSE;
  str = sz;
  return TRUE;
}

// Calls an export of the DLL, loading it for the call.
static HRESULT CallRegistration(LPCSTR lpszExport) {
  HMODULE hDll = ::LoadLibrary(kDllName);
  CHECK(hDll != NULL);
  if (hDll == NULL) return E_FAIL;
  typedef HRESULT(STDAPICALLTYPE * PFN)();
  PFN pfn = (PFN)::GetProcAddress(hDll, lpszExport);
  CHECK(pfn != NULL);
  HRESULT hr = pfn != NULL ? pfn() : E_FAIL;
  ::FreeLibrary(hDll);
  return hr;
}

static void PumpMessages() {
  MSG msg;
  while (::PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
    ::TranslateMessage(&msg);
    ::DispatchMessage(&msg);
  }
}

class CMainFrame : public CFrameWnd {
public:
  CWnd m_ctl;
  int m_nClicks;
  int m_nChanged;
  long m_nLastChanged;

  CMainFrame() : m_nClicks(0), m_nChanged(0), m_nLastChanged(0) {}

  void OnClick() { m_nClicks++; }
  void OnChanged(long n) {
    m_nChanged++;
    m_nLastChanged = n;
  }

  void RunTests();

  afx_msg int OnCreate(LPCREATESTRUCT lpcs) {
    if (CFrameWnd::OnCreate(lpcs) == -1) return -1;
    SetTimer(1, 100, NULL);
    return 0;
  }
  afx_msg void OnTimer(UINT_PTR) {
    KillTimer(1);
    RunTests();
    PostMessage(WM_CLOSE);
  }
  DECLARE_MESSAGE_MAP()
  DECLARE_EVENTSINK_MAP()
};

BEGIN_MESSAGE_MAP(CMainFrame, CFrameWnd)
  ON_WM_CREATE()
  ON_WM_TIMER()
END_MESSAGE_MAP()

BEGIN_EVENTSINK_MAP(CMainFrame, CFrameWnd)
  ON_EVENT(CMainFrame, IDC_CTL, DISPID_CLICK, OnClick, VTS_NONE)
  ON_EVENT(CMainFrame, IDC_CTL, eventidDllChanged, OnChanged, VTS_I4)
END_EVENTSINK_MAP()

void CMainFrame::RunTests() {
  // ----- Registration
  TestLog("registration");
  CHECK(::GetModuleHandle(kDllName) == NULL);
  // Keys from an earlier run in the same Wine prefix go first.
  CallRegistration("DllUnregisterServer");
  CHECK(CallRegistration("DllRegisterServer") == S_OK);
  CString str;
  CHECK(ReadKey(kClsidKey, NULL, str) && str == _T("DllTest Control"));
  CHECK(ReadKey(CString(kClsidKey) + _T("\\InprocServer32"), NULL, str));
  CHECK(str.Right(lstrlen(kDllName)).CompareNoCase(kDllName) == 0);
  CHECK(ReadKey(CString(kClsidKey) + _T("\\InprocServer32"), _T("ThreadingModel"), str) &&
        str == _T("Apartment"));
  CHECK(!ReadKey(CString(kClsidKey) + _T("\\LocalServer32"), NULL, str));
  CHECK(ReadKey(CString(kClsidKey) + _T("\\Control"), NULL, str));
  CHECK(ReadKey(_T("DllTest.DllCtrl.1\\CLSID"), NULL, str));
  // Loading and freeing the DLL ran its InitInstance and ExitInstance.
  TCHAR szExit[8];
  CHECK(::GetEnvironmentVariable(_T("DLLTEST_EXITINSTANCE"), szExit, _countof(szExit)) > 0);
  ::SetEnvironmentVariable(_T("DLLTEST_EXITINSTANCE"), NULL);
  CHECK(::GetModuleHandle(kDllName) == NULL);

  // ----- The control, made by COM from the registry
  TestLog("control");
  CRect rect(10, 10, 210, 60);
  BOOL bMade = m_ctl.CreateControl(CLSID_DllCtrl, _T("Hosted"), WS_VISIBLE, rect, this, IDC_CTL);
  CHECK(bMade);
  HMODULE hDll = ::GetModuleHandle(kDllName);
  CHECK(hDll != NULL);
  if (!bMade || hDll == NULL) return;
  PFNDLLTESTINFO pfnInfo = (PFNDLLTESTINFO)::GetProcAddress(hDll, "DllTestInfo");
  PFNDLLTESTDIALOG pfnDialog = (PFNDLLTESTDIALOG)::GetProcAddress(hDll, "DllTestDialog");
  CHECK(pfnInfo != NULL && pfnDialog != NULL);
  if (pfnInfo == NULL || pfnDialog == NULL) return;

  // The DLL's library knows its own module, apart from the program's.
  DLLTESTINFO info;
  memset(&info, 0, sizeof(info));
  pfnInfo(&info);
  CHECK(info.nInitInstance == 1 && info.nAlive == 1);
  CHECK(info.hInstance == (HINSTANCE)hDll && info.hResource == (HINSTANCE)hDll);
  CHECK(info.bAppIsModule);
  CHECK(lstrcmp(info.szString, _T("From the DLL")) == 0);
  CHECK(AfxGetInstanceHandle() == ::GetModuleHandle(NULL));
  CString strExe;
  strExe.LoadString(IDS_FROMEXE);
  CHECK(strExe == _T("From the program"));
  CHECK(AfxGetApp() != NULL && !AfxGetApp()->IsKindOf(RUNTIME_CLASS(COleControlModule)));

  // The control's window is the DLL's, in the program's frame.
  CHECK(m_ctl.m_hWnd != NULL && ::GetParent(m_ctl.m_hWnd) == m_hWnd);
  CHECK((HINSTANCE)::GetWindowLongPtr(m_ctl.m_hWnd, GWLP_HINSTANCE) == (HINSTANCE)hDll);
  CHECK(CWnd::FromHandlePermanent(m_ctl.m_hWnd) == &m_ctl);
  CRect rectCtl;
  m_ctl.GetWindowRect(&rectCtl);
  ScreenToClient(&rectCtl);
  CHECK(rectCtl == rect);

  // Properties, methods and events across the two libraries.
  CString strCaption;
  m_ctl.GetWindowText(strCaption);
  CHECK(strCaption == _T("Hosted"));
  long n = 0;
  m_ctl.GetProperty(DISPID_DLLVALUE, VT_I4, &n);
  CHECK(n == 5);
  m_ctl.SetProperty(DISPID_DLLVALUE, VT_I4, 40L);
  m_ctl.InvokeHelper(DISPID_DLLBUMP, DISPATCH_METHOD, VT_EMPTY, NULL, NULL);
  CHECK(m_nChanged == 1 && m_nLastChanged == 41);
  m_ctl.SendMessage(WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(5, 5));
  m_ctl.SendMessage(WM_LBUTTONUP, 0, MAKELPARAM(5, 5));
  CHECK(m_nClicks == 1);
  m_ctl.SetFocus();
  CHECK(::GetFocus() == m_ctl.m_hWnd);

  // A dialog from the DLL's template.
  HWND hDlg = pfnDialog(m_hWnd);
  CHECK(hDlg != NULL && ::GetParent(hDlg) == m_hWnd);
  if (hDlg != NULL) {
    ::SetWindowPos(hDlg, NULL, 10, 80, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
    TCHAR szText[64];
    ::GetDlgItemText(hDlg, IDC_DLLTEXT, szText, _countof(szText));
    CHECK(lstrcmp(szText, _T("From the DLL")) == 0);
    CHECK(::SendMessage(hDlg, WM_DLLTEST_STATE, 0, 0) == 1);
    CHECK(!AfxGetApp()->IsKindOf(RUNTIME_CLASS(COleControlModule)));
    // The program does not know the DLL's window objects.
    CHECK(CWnd::FromHandlePermanent(hDlg) == NULL);
  }
  PumpMessages();
  m_ctl.UpdateWindow();
  TestSaveWindowBitmap(m_hWnd, _T("dlltest-frame.bmp"));
  if (hDlg != NULL) ::DestroyWindow(hDlg);

  // While the control lives, the DLL stays.
  typedef HRESULT(STDAPICALLTYPE * PFNCANUNLOAD)();
  PFNCANUNLOAD pfnCanUnload = (PFNCANUNLOAD)::GetProcAddress(hDll, "DllCanUnloadNow");
  CHECK(pfnCanUnload != NULL && pfnCanUnload() == S_FALSE);
  ::CoFreeUnusedLibraries();
  CHECK(::GetModuleHandle(kDllName) == hDll);

  // ----- Unloading
  TestLog("unloading");
  m_ctl.DestroyWindow();
  memset(&info, 0, sizeof(info));
  pfnInfo(&info);
  CHECK(info.nAlive == 0);
  CHECK(pfnCanUnload() == S_OK);
  CString strClass = info.szClass;
  CHECK(!strClass.IsEmpty());
  WNDCLASS wc;
  CHECK(::GetClassInfo((HINSTANCE)hDll, strClass, &wc));
  // The program still runs after the DLL's objects are gone.
  CHECK(::IsWindow(m_hWnd));
  ::CoFreeUnusedLibraries();
  ::CoFreeUnusedLibraries();
  CHECK(::GetModuleHandle(kDllName) == NULL);
  CHECK(::GetEnvironmentVariable(_T("DLLTEST_EXITINSTANCE"), szExit, _countof(szExit)) > 0);
  // The DLL's window classes went with it.
  CHECK(!::GetClassInfo((HINSTANCE)hDll, strClass, &wc));

  // ----- Unregistering
  TestLog("unregistering");
  CHECK(CallRegistration("DllUnregisterServer") == S_OK);
  CHECK(!ReadKey(kClsidKey, NULL, str));
  CHECK(!ReadKey(_T("DllTest.DllCtrl.1"), NULL, str));
  CWnd wnd;
  CHECK(!wnd.CreateControl(CLSID_DllCtrl, NULL, WS_VISIBLE, rect, this, IDC_CTL));
}

class CTestApp : public CWinApp {
public:
  virtual BOOL InitInstance() {
    TestOpenLog(_T("dlltest.log"));
    if (!AfxOleInit()) {
      TestLog("AfxOleInit failed");
      return FALSE;
    }
    AfxEnableControlContainer();
    CMainFrame* pFrame = new CMainFrame;
    if (!pFrame->Create(NULL, _T("dlltest"), WS_OVERLAPPEDWINDOW, CRect(0, 0, 400, 300)))
      return FALSE;
    m_pMainWnd = pFrame;
    pFrame->ShowWindow(SW_SHOW);
    pFrame->UpdateWindow();
    return TRUE;
  }
  virtual int ExitInstance() {
    CWinApp::ExitInstance();
    return TestFinish();
  }
};

CTestApp theApp;
