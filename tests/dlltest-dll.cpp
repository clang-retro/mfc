// dlltest-dll.cpp - the DLL of dlltest: an MFC regular DLL, as an .ocx
// is, with a COleControlModule, a control, the four COM exports and two
// functions of its own for the test program.
#include <afxwin.h>
#include <afxdisp.h>
#include <afxctl.h>
#include "dlltest.h"

static int g_nInitInstance = 0;

class CDllApp : public COleControlModule {
public:
  virtual BOOL InitInstance() {
    g_nInitInstance++;
    return COleControlModule::InitInstance();
  }
  virtual int ExitInstance() {
    // The program cannot ask an unloaded DLL, so this goes in the
    // process environment.
    ::SetEnvironmentVariable(_T("DLLTEST_EXITINSTANCE"), _T("1"));
    return COleControlModule::ExitInstance();
  }
};

CDllApp theApp;

static const DWORD kMiscStatus =
    OLEMISC_ACTIVATEWHENVISIBLE | OLEMISC_SETCLIENTSITEFIRST | OLEMISC_INSIDEOUT |
    OLEMISC_CANTLINKINSIDE | OLEMISC_RECOMPOSEONRESIZE;

// {7B2E4A63-1F5C-4E8B-A034-9C6F2DBE5A71}
static const IID IID_DDllCtrl = {
    0x7b2e4a63, 0x1f5c, 0x4e8b, {0xa0, 0x34, 0x9c, 0x6f, 0x2d, 0xbe, 0x5a, 0x71}};
// {7B2E4A63-1F5C-4E8B-A034-9C6F2DBE5A72}
static const IID IID_DDllCtrlEvents = {
    0x7b2e4a63, 0x1f5c, 0x4e8b, {0xa0, 0x34, 0x9c, 0x6f, 0x2d, 0xbe, 0x5a, 0x72}};

static int g_nAlive = 0;
static TCHAR g_szClass[64];

class CDllCtrl : public COleControl {
  DECLARE_DYNCREATE(CDllCtrl)
public:
  long m_nValue;
  CDllCtrl() : m_nValue(5) {
    InitializeIIDs(&IID_DDllCtrl, &IID_DDllCtrlEvents);
    g_nAlive++;
  }
  virtual ~CDllCtrl() { g_nAlive--; }

  virtual void OnDraw(CDC* pDC, const CRect& rcBounds, const CRect&) {
    pDC->FillSolidRect(rcBounds, RGB(0, 160, 80));
    CString str;
    str.Format(_T("In the DLL: %ld"), m_nValue);
    pDC->SetBkMode(TRANSPARENT);
    pDC->SetTextColor(RGB(255, 255, 255));
    CRect rc(rcBounds);
    pDC->DrawText(str, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  }
  virtual void DoPropExchange(CPropExchange* pPX) {
    COleControl::DoPropExchange(pPX);
    PX_Long(pPX, _T("Value"), m_nValue, 5);
  }
  virtual BOOL CreateControlWindow(HWND hWndParent, const CRect& rcPos, LPCRECT prcClipped) {
    if (!COleControl::CreateControlWindow(hWndParent, rcPos, prcClipped)) return FALSE;
    ::GetClassName(m_hWnd, g_szClass, _countof(g_szClass));
    return TRUE;
  }

  long GetValue() { return m_nValue; }
  void SetValue(long n) {
    m_nValue = n;
    SetModifiedFlag();
    InvalidateControl();
  }
  void Bump() {
    m_nValue++;
    FireEvent(eventidDllChanged, EVENT_PARAM(VTS_I4), m_nValue);
  }

  DECLARE_OLECREATE_EX(CDllCtrl)
  DECLARE_OLECTLTYPE(CDllCtrl)
  DECLARE_DISPATCH_MAP()
  DECLARE_EVENT_MAP()
};

IMPLEMENT_DYNCREATE(CDllCtrl, COleControl)

// {7B2E4A63-1F5C-4E8B-A034-9C6F2DBE5A70}
IMPLEMENT_OLECREATE_EX(CDllCtrl, "DllTest.DllCtrl.1", 0x7b2e4a63, 0x1f5c, 0x4e8b, 0xa0, 0x34, 0x9c,
                       0x6f, 0x2d, 0xbe, 0x5a, 0x70)
IMPLEMENT_OLECTLTYPE(CDllCtrl, IDS_DLLCTL, kMiscStatus)

BEGIN_DISPATCH_MAP(CDllCtrl, COleControl)
  DISP_PROPERTY_EX_ID(CDllCtrl, "Value", DISPID_DLLVALUE, GetValue, SetValue, VT_I4)
  DISP_FUNCTION_ID(CDllCtrl, "Bump", DISPID_DLLBUMP, Bump, VT_EMPTY, VTS_NONE)
  DISP_STOCKPROP_CAPTION()
END_DISPATCH_MAP()

BEGIN_EVENT_MAP(CDllCtrl, COleControl)
  EVENT_CUSTOM_ID("Changed", eventidDllChanged, Bump, VTS_I4)
  EVENT_STOCK_CLICK()
END_EVENT_MAP()

BOOL CDllCtrl::CDllCtrlFactory::UpdateRegistry(BOOL bRegister) {
  if (bRegister)
    return AfxOleRegisterControlClass(AfxGetInstanceHandle(), m_clsid, m_lpszProgID, IDS_DLLCTL, 0,
                                      afxRegApartmentThreading, kMiscStatus, GUID_NULL, 1, 0);
  return AfxOleUnregisterClass(m_clsid, m_lpszProgID);
}

// ---------------------------------------------------------------------
// The exports

STDAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, LPVOID* ppv) {
  AFX_MANAGE_STATE(AfxGetStaticModuleState());
  return AfxDllGetClassObject(rclsid, riid, ppv);
}

STDAPI DllCanUnloadNow() {
  AFX_MANAGE_STATE(AfxGetStaticModuleState());
  return AfxDllCanUnloadNow();
}

STDAPI DllRegisterServer() {
  AFX_MANAGE_STATE(AfxGetStaticModuleState());
  return COleObjectFactoryEx::UpdateRegistryAll(TRUE) ? S_OK : SELFREG_E_CLASS;
}

STDAPI DllUnregisterServer() {
  AFX_MANAGE_STATE(AfxGetStaticModuleState());
  return COleObjectFactoryEx::UpdateRegistryAll(FALSE) ? S_OK : SELFREG_E_CLASS;
}

extern "C" void WINAPI DllTestInfo(DLLTESTINFO* pInfo) {
  AFX_MANAGE_STATE(AfxGetStaticModuleState());
  pInfo->nInitInstance = g_nInitInstance;
  pInfo->nAlive = g_nAlive;
  pInfo->hInstance = AfxGetInstanceHandle();
  pInfo->hResource = AfxGetResourceHandle();
  pInfo->bAppIsModule = AfxGetApp() == &theApp && theApp.IsKindOf(RUNTIME_CLASS(COleControlModule));
  CString str;
  str.LoadString(IDS_FROMDLL);
  lstrcpyn(pInfo->szString, str, _countof(pInfo->szString));
  lstrcpyn(pInfo->szClass, g_szClass, _countof(pInfo->szClass));
}

// A modeless dialog from the DLL's own template, in the program's window.
class CDllDialog : public CDialog {
public:
  virtual BOOL OnInitDialog() {
    CDialog::OnInitDialog();
    CString str;
    str.LoadString(IDS_FROMDLL);
    SetDlgItemText(IDC_DLLTEXT, str);
    return TRUE;
  }
  virtual void PostNcDestroy() { delete this; }
  // Asked from the program: the dialog's messages run in the DLL's state.
  virtual LRESULT WindowProc(UINT nMsg, WPARAM wParam, LPARAM lParam) {
    if (nMsg == WM_DLLTEST_STATE) {
      CString str;
      str.LoadString(IDS_FROMDLL);
      return AfxGetApp() == &theApp && str == _T("From the DLL") &&
             CWnd::FromHandlePermanent(m_hWnd) == this;
    }
    return CDialog::WindowProc(nMsg, wParam, lParam);
  }
};

extern "C" HWND WINAPI DllTestDialog(HWND hWndParent) {
  AFX_MANAGE_STATE(AfxGetStaticModuleState());
  CDllDialog* pDlg = new CDllDialog;
  if (!pDlg->Create(IDD_DLLDLG, CWnd::FromHandle(hWndParent))) {
    delete pDlg;
    return NULL;
  }
  return pDlg->m_hWnd;
}
