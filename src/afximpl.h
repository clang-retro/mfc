// afximpl.h - private state shared by the library sources.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
#pragma once
#include "stdafx.h"

// Maps a Windows handle to the C++ object that wraps it. Each thread has
// one map per handle kind. Objects the program creates go in the
// permanent map; objects made up on the fly by FromHandle go in the
// temporary map and are deleted when the thread is idle.
class CHandleMap {
public:
  CHandleMap(CRuntimeClass* pClass, size_t nOffset, int nHandles = 1);
  ~CHandleMap() { DeleteTemp(); }

  CObject* FromHandle(HANDLE h);  // makes a temporary object if needed
  CObject* LookupPermanent(HANDLE h);
  CObject* LookupTemporary(HANDLE h);
  void SetPermanent(HANDLE h, CObject* pObject);
  void RemoveHandle(HANDLE h);
  void DeleteTemp();

  CMapPtrToPtr m_permanentMap;
  CMapPtrToPtr m_temporaryMap;
  CRuntimeClass* m_pClass;
  size_t m_nOffset;  // offset of the first handle in the object
  int m_nHandles;    // CDC holds two handles
};

struct AFX_THREAD_STATE {
  CWinThread* m_pCurrentWinThread;
  CWnd* m_pWndInit;          // window being created (see AfxHookWindowCreate)
  CWnd* m_pAlternateWndInit;
  HHOOK m_hHookOldCbtFilter;
  HHOOK m_hHookOldMsgFilter;
  MSG m_lastSentMsg;         // for CWnd::Default
  MSG m_msgCur;              // for threads without a CWinThread
  HWND m_hTrackingWindow;
  HMENU m_hTrackingMenu;
  TCHAR m_szTempClassName[96];
  BOOL m_bInMsgFilter;
  int m_nTempMapLock;
  CHandleMap* m_pmapHWND;
  CHandleMap* m_pmapHMENU;
  CHandleMap* m_pmapHDC;
  CHandleMap* m_pmapHGDIOBJ;
  CHandleMap* m_pmapHIMAGELIST;  // made by afxcmn's CImageList
  CView* m_pRoutingView;
  CFrameWnd* m_pRoutingFrame;
  CPoint m_ptCursorLast;
  UINT m_nMsgLast;
  int m_nDisablePumpCount;
  HWND m_hLockoutNotifyWindow;  // set during UpdateData
};

AFX_THREAD_STATE* AFXAPI AfxGetThreadState();
void AFXAPI AfxTermThreadState();

// Process-wide state. This library is linked statically, so there is
// exactly one module.
struct AFX_MODULE_STATE {
  CWinApp* m_pCurrentWinApp;
  HINSTANCE m_hCurrentInstanceHandle;
  HINSTANCE m_hCurrentResourceHandle;
  LPCTSTR m_lpszCurrentAppName;
  LONG m_fRegisteredClasses;
  CStringArray* m_pstrUnregisterList;
  BOOL m_bInitialized;
};
AFX_MODULE_STATE* AFXAPI AfxGetModuleState();

CHandleMap* AFXAPI afxMapHWND(BOOL bCreate = FALSE);
CHandleMap* AFXAPI afxMapHMENU(BOOL bCreate = FALSE);
CHandleMap* AFXAPI afxMapHDC(BOOL bCreate = FALSE);
CHandleMap* AFXAPI afxMapHGDIOBJ(BOOL bCreate = FALSE);

// Window class names registered by AfxDeferRegisterClass.
extern const TCHAR _afxWnd[];
extern const TCHAR _afxWndControlBar[];
extern const TCHAR _afxWndMDIFrame[];
extern const TCHAR _afxWndFrameOrView[];

// Sets the "routing" frame and view while a command is routed, so that
// GetRoutingFrame works (used by CDocument::OnCmdMsg and the like).
struct CPushRoutingFrame {
  CFrameWnd* pOldRoutingFrame;
  AFX_THREAD_STATE* pThreadState;
  explicit CPushRoutingFrame(CFrameWnd* pNewFrame) {
    pThreadState = AfxGetThreadState();
    pOldRoutingFrame = pThreadState->m_pRoutingFrame;
    pThreadState->m_pRoutingFrame = pNewFrame;
  }
  ~CPushRoutingFrame() { pThreadState->m_pRoutingFrame = pOldRoutingFrame; }
};
struct CPushRoutingView {
  CView* pOldRoutingView;
  AFX_THREAD_STATE* pThreadState;
  explicit CPushRoutingView(CView* pNewView) {
    pThreadState = AfxGetThreadState();
    pOldRoutingView = pThreadState->m_pRoutingView;
    pThreadState->m_pRoutingView = pNewView;
  }
  ~CPushRoutingView() { pThreadState->m_pRoutingView = pOldRoutingView; }
};

// A CCmdUI that only records whether the command is enabled. Used to
// check a menu or accelerator command before running it.
class CTestCmdUI : public CCmdUI {
public:
  BOOL m_bEnabled;
  CTestCmdUI() : m_bEnabled(TRUE) {}
  virtual void Enable(BOOL bOn) { m_bEnabled = bOn; m_bEnableChanged = TRUE; }
  virtual void SetCheck(int) {}
  virtual void SetRadio(BOOL) {}
  virtual void SetText(LPCTSTR) {}
};

// The current message for this thread (CWinThread::m_msgCur if there is one).
MSG* AFXAPI _AfxGetCurrentMsg();

// The window a message box or modal dialog should be owned by; disables
// the top-level window in *pWndTop if asked.
HWND AFXAPI _AfxGetSafeOwner(HWND hParent, HWND* pWndTop);

// Helpers shared between sources
BOOL AFXAPI _AfxIsComboBoxControl(HWND hWnd, UINT nStyle);
BOOL AFXAPI _AfxCheckCenterDialog(LPCTSTR lpszResource);
void AFXAPI _AfxAppendFilterSuffix(CString& filter, OPENFILENAME& ofn, CDocTemplate* pTemplate,
                                   CString* pstrDefaultExt);
BOOL AFXAPI _AfxDeleteMenuItems(HMENU hMenu);
HWND AFXAPI _AfxChildWindowFromPoint(HWND hWnd, POINT pt);
LPCTSTR AFXAPI _AfxLoadDefaultString(UINT nID);  // built-in text for AFX_IDS_ ids
BOOL AFXAPI AfxLoadString(UINT nIDS, CString& rString);  // with that fallback
BOOL AFXAPI _AfxIsSplitterWnd(CWnd* pWnd);  // defined with CSplitterWnd
