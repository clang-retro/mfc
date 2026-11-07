// dlltest.h - resource IDs and the interface between dlltest and its
// DLL, dlltest-dll.
#define IDS_DLLCTL 100
#define IDS_FROMDLL 101
#define IDD_DLLDLG 200
#define IDC_DLLTEXT 201
#define IDS_FROMEXE 101  // the program's string of the same number
#define IDC_CTL 2001

// The control's members and events.
enum { DISPID_DLLVALUE = 1, DISPID_DLLBUMP = 2 };
enum { eventidDllChanged = 1 };

// What the DLL tells the program about itself (DllTestInfo).
struct DLLTESTINFO {
  int nInitInstance;      // times its InitInstance ran
  int nAlive;             // its controls
  HINSTANCE hInstance;    // AfxGetInstanceHandle in the DLL
  HINSTANCE hResource;    // AfxGetResourceHandle in the DLL
  BOOL bAppIsModule;      // AfxGetApp() is its COleControlModule
  TCHAR szString[64];     // IDS_FROMDLL, loaded in the DLL
  TCHAR szClass[64];      // the window class of its last control
};
typedef void(WINAPI* PFNDLLTESTINFO)(DLLTESTINFO*);
typedef HWND(WINAPI* PFNDLLTESTDIALOG)(HWND hWndParent);
