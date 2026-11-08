// afximpl.h - private state shared by the library sources.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "stdafx.h"

// Bars made on a container's window while an object is active in place
// there belong to the frame given (bars.cpp).
void AFXAPI _AfxRegisterInPlaceDockSite(CFrameWnd* pFrame, BOOL bAdd);

// Shared in-place menus (oleipcli.cpp): copies the groups of hMenuSource,
// split by separators, into hMenuShared (nFirstGroup 0 for a container's
// groups 0, 2 and 4; 1 for an object's 1, 3 and 5), and takes them out.
// nLastGroup (nFirstGroup + 4 when -1) is the last group filled: past it,
// with bStop the merge stops, and otherwise the rest goes into it. The
// result is the position in hMenuSource where the merge stopped.
int AFXAPI _AfxMergeMenus(HMENU hMenuShared, HMENU hMenuSource, LONG* lpMenuWidths,
                          int nFirstGroup, int nLastGroup = -1, BOOL bStop = FALSE);
void AFXAPI _AfxUnmergeMenus(HMENU hMenuShared, HMENU hMenuSource);

// Idle updates for in-place frames, which are not under the main window
// (thread.cpp calls it when set).
extern void(AFXAPI* _afxpfnInPlaceIdle)();
// What CWnd calls once ActiveX control containment is on (occmgr.cpp).
struct _AFX_OCC_HOOKS {
  // A dialog template's OLE controls come out before it is created...
  const DLGTEMPLATE* (AFXAPI* pfnPreCreateDialog)(CWnd* pWnd, const DLGTEMPLATE* pTemplate,
                                                  HINSTANCE hInst);
  void(AFXAPI* pfnPostCreateDialog)(CWnd* pWnd);
  // ...and are made on WM_INITDIALOG.
  void(AFXAPI* pfnInitDialog)(CWnd* pWnd);
  void(AFXAPI* pfnDestroy)(CWnd* pWnd);    // WM_DESTROY: the hosted controls close
  void(AFXAPI* pfnNcDestroy)(CWnd* pWnd);  // the container goes
  BOOL(AFXAPI* pfnDestroyControl)(CWnd* pWnd);  // DestroyWindow on a control's window
  BOOL(AFXAPI* pfnDDXControl)(CWnd* pDlgWnd, int nIDC, CWnd& rControl);
  // Windowless controls in the window: whether there are any, a message
  // for one of them, and drawing them over what the window painted.
  BOOL(AFXAPI* pfnHasWindowless)(CWnd* pWnd);
  BOOL(AFXAPI* pfnWindowlessMessage)(CWnd* pWnd, UINT message, WPARAM wParam, LPARAM lParam,
                                     LRESULT* plResult);
  void(AFXAPI* pfnPaintWindowless)(CWnd* pWnd, HRGN hRgnUpdate);
  // The drop target of the windowless control at a point (AddRef'd), or
  // NULL.
  LPDROPTARGET(AFXAPI* pfnWindowlessDropTarget)(CWnd* pWnd, CPoint point);
};
extern const _AFX_OCC_HOOKS* _afxOccHooks;
// What a control site calls once a control is data bound (occdb.cpp):
// a control's bound property changed, a site goes, and a data source
// control's cursor changed.
class COleControlSite;
struct _AFX_OCC_BIND_HOOKS {
  void(AFXAPI* pfnChanged)(COleControlSite* pSite, DISPID dispid);
  void(AFXAPI* pfnRelease)(COleControlSite* pSite);
  // A data source control's site was told of a change to its cursor.
  HRESULT(AFXAPI* pfnDBEvent)(COleControlSite* pSite, int nState, DWORD dwEventWhat,
                              ULONG cReasons, struct tagDBNOTIFYREASON* rgReasons);
};
// Fires a data source control's notification (ON_DSCNOTIFY) on its
// container: S_FALSE when a handler refuses it.
HRESULT AFXAPI _AfxFireDSCNotify(COleControlSite* pSite, DSCSTATE nState, DSCREASON nReason);
extern const _AFX_OCC_BIND_HOOKS* _afxOccBindHooks;

// Empties the type library caches (oleconn.cpp), before OLE goes.
void AFXAPI _AfxReleaseTypeLibCaches();

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
  void* m_pSockState;            // made by afxsock's sockets
  void (*m_pfnSockTerm)(void*);  // frees m_pSockState
};

AFX_THREAD_STATE* AFXAPI AfxGetThreadState();
void AFXAPI AfxTermThreadState();

// Process-wide state. There is exactly one: the library is linked
// statically into one module, or it is the shared mfc.dll that the
// program and its extension DLLs use together.
struct AFX_MODULE_STATE {
  CWinApp* m_pCurrentWinApp;
  HINSTANCE m_hCurrentInstanceHandle;
  HINSTANCE m_hCurrentResourceHandle;
  LPCTSTR m_lpszCurrentAppName;
  LONG m_fRegisteredClasses;
  CStringArray* m_pstrUnregisterList;  // the window classes a DLL takes away when it unloads
  BOOL m_bInitialized;
  BOOL m_bDLL;  // the library is in a DLL (DllMain ran)
  CDynLinkLibrary* m_pFirstDLL;  // the extension DLLs, searched for resources
};

// Takes the classes in [pStart, pEnd), the image of a module being
// unloaded, out of the list CRuntimeClass::FromName searches.
void AFXAPI _AfxRemoveClasses(const BYTE* pStart, const BYTE* pEnd);
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

class CDialogBar;

// Builds a dialog template in memory, for the framework's own dialogs
// when the program's resources do not have them (programs that do not
// include afxres.rc or afxprint.rc). Units are dialog units.
class CAfxDlgTemplate {
public:
  CAfxDlgTemplate(DWORD dwStyle, int cx, int cy, LPCWSTR lpszCaption = L"",
                  LPCWSTR lpszFont = L"MS Shell Dlg", WORD wPointSize = 8);
  // nClass is a predefined class atom: 0x80 button, 0x81 edit, 0x82 static.
  void AddItem(WORD nClass, DWORD dwStyle, int x, int y, int cx, int cy, WORD nID,
               LPCWSTR lpszText);
  LPCDLGTEMPLATE GetTemplate() const { return (LPCDLGTEMPLATE)m_data.GetData(); }

private:
  void Align(int n);
  void AddWord(WORD w);
  void AddString(LPCWSTR lpsz);
  CByteArray m_data;
};

// Creates a dialog bar from an in-memory template.
BOOL AFXAPI _AfxCreateDialogBarIndirect(CDialogBar* pBar, CWnd* pParentWnd,
                                        LPCDLGTEMPLATE lpTemplate, UINT nStyle, UINT nID);

// strftime with the codes MFC programs use (time.cpp).
struct tm;
CString AFXAPI _AfxFormatTm(LPCTSTR pszFormat, const struct tm& t);
