// wltest.cpp - windowless ActiveX controls. The program is both the
// control's server and its container:
//   - the control (CWlCtrl, a COleControl) asks for windowless activation
//     in GetControlFlags;
//   - a frame window hosts it with CWnd::CreateControl: no window, the
//     frame's painting draws it, its mouse messages over the control and
//     its keys while the control has the focus go to it (in the control's
//     coordinates), and capture, focus, InvalidateControl, GetDC, moving,
//     hiding and the stock Click event work through the container;
//   - drag and drop over it goes to the drop target the control gives
//     (GetWindowlessDropTarget), through the drop target the container
//     made for the frame: entering, leaving it for the frame, coming back,
//     the drop and its data;
//   - without windowlessActivate the same control has a window as before.
#include <afxwin.h>
#include <afxdisp.h>
#include <afxctl.h>
#include <afxocc.h>
#include <afxole.h>
#include <olectl.h>
#include "testutil.h"

#define IDS_WLCTL 100
#define IDC_WL 2001

// {7C2E4A63-1F5C-4E8B-A034-9C6F2DBE5A71}
static const IID IID_DWlCtrl = {
    0x7c2e4a63, 0x1f5c, 0x4e8b, {0xa0, 0x34, 0x9c, 0x6f, 0x2d, 0xbe, 0x5a, 0x71}};
// {7C2E4A63-1F5C-4E8B-A034-9C6F2DBE5A72}
static const IID IID_DWlCtrlEvents = {
    0x7c2e4a63, 0x1f5c, 0x4e8b, {0xa0, 0x34, 0x9c, 0x6f, 0x2d, 0xbe, 0x5a, 0x72}};

static const DWORD kMiscStatus = OLEMISC_ACTIVATEWHENVISIBLE | OLEMISC_SETCLIENTSITEFIRST |
                                 OLEMISC_INSIDEOUT | OLEMISC_CANTLINKINSIDE |
                                 OLEMISC_RECOMPOSEONRESIZE;
static const COLORREF kFill = RGB(0, 128, 255);

// Whether the next control asks to be windowless.
static BOOL g_bAskWindowless = TRUE;

// =====================================================================
// The control

// The control's drop target: it takes copies, and keeps what it saw.
class CTestDropTarget : public IDropTarget {
public:
  int m_nEnter, m_nOver, m_nLeave, m_nDrop;
  POINTL m_ptLast;
  CString m_strDropped;
  CTestDropTarget() : m_nEnter(0), m_nOver(0), m_nLeave(0), m_nDrop(0) { m_ptLast.x = m_ptLast.y = 0; }

  STDMETHOD(QueryInterface)(REFIID iid, void** ppv) {
    if (iid == IID_IUnknown || iid == IID_IDropTarget) {
      *ppv = this;
      return S_OK;
    }
    *ppv = NULL;
    return E_NOINTERFACE;
  }
  STDMETHOD_(ULONG, AddRef)() { return 2; }  // part of the control
  STDMETHOD_(ULONG, Release)() { return 1; }
  STDMETHOD(DragEnter)(IDataObject*, DWORD, POINTL pt, DWORD* pdwEffect) {
    m_nEnter++;
    m_ptLast = pt;
    *pdwEffect &= DROPEFFECT_COPY;
    return S_OK;
  }
  STDMETHOD(DragOver)(DWORD, POINTL pt, DWORD* pdwEffect) {
    m_nOver++;
    m_ptLast = pt;
    *pdwEffect &= DROPEFFECT_COPY;
    return S_OK;
  }
  STDMETHOD(DragLeave)() {
    m_nLeave++;
    return S_OK;
  }
  STDMETHOD(Drop)(IDataObject* pData, DWORD, POINTL pt, DWORD* pdwEffect) {
    m_nDrop++;
    m_ptLast = pt;
    COleDataObject dataObject;
    dataObject.Attach(pData, FALSE);
    HGLOBAL hText = dataObject.GetGlobalData(CF_TEXT);
    if (hText != NULL) {
      m_strDropped = (LPCSTR)::GlobalLock(hText);
      ::GlobalUnlock(hText);
      ::GlobalFree(hText);
    }
    *pdwEffect &= DROPEFFECT_COPY;
    return S_OK;
  }
};

class CWlCtrl : public COleControl {
  DECLARE_DYNCREATE(CWlCtrl)
public:
  static int s_nAlive;
  static CWlCtrl* s_pLast;

  BOOL m_bAskWindowless;
  int m_nDraws;
  CRect m_rcLastBounds;
  int m_nDowns, m_nUps, m_nMoves, m_nChars, m_nSetFocus, m_nKillFocus;
  CPoint m_ptDown, m_ptMove;
  UINT m_nLastKey;
  CTestDropTarget m_dropTarget;

  CWlCtrl()
      : m_bAskWindowless(g_bAskWindowless),
        m_nDraws(0),
        m_nDowns(0),
        m_nUps(0),
        m_nMoves(0),
        m_nChars(0),
        m_nSetFocus(0),
        m_nKillFocus(0),
        m_nLastKey(0) {
    InitializeIIDs(&IID_DWlCtrl, &IID_DWlCtrlEvents);
    s_nAlive++;
    s_pLast = this;
  }
  virtual ~CWlCtrl() {
    if (s_pLast == this) s_pLast = NULL;
    s_nAlive--;
  }

  virtual DWORD GetControlFlags() {
    DWORD dwFlags = COleControl::GetControlFlags();
    if (m_bAskWindowless) dwFlags |= windowlessActivate;
    return dwFlags;
  }
  virtual IDropTarget* GetWindowlessDropTarget() {
    m_dropTarget.AddRef();
    return &m_dropTarget;
  }
  virtual void OnDraw(CDC* pDC, const CRect& rcBounds, const CRect&) {
    m_nDraws++;
    m_rcLastBounds = rcBounds;
    pDC->FillSolidRect(rcBounds, kFill);
  }

  afx_msg void OnLButtonDown(UINT nFlags, CPoint point) {
    m_nDowns++;
    m_ptDown = point;
    COleControl::OnLButtonDown(nFlags, point);  // takes the mouse
    SetFocus();
  }
  afx_msg void OnLButtonUp(UINT nFlags, CPoint point) {
    m_nUps++;
    COleControl::OnLButtonUp(nFlags, point);
  }
  afx_msg void OnMouseMove(UINT nFlags, CPoint point) {
    m_nMoves++;
    m_ptMove = point;
    COleControl::OnMouseMove(nFlags, point);
  }
  afx_msg void OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags) {
    m_nLastKey = nChar;
    COleControl::OnKeyDown(nChar, nRepCnt, nFlags);
  }
  afx_msg void OnChar(UINT nChar, UINT nRepCnt, UINT nFlags) {
    m_nChars++;
    COleControl::OnChar(nChar, nRepCnt, nFlags);
  }
  afx_msg void OnSetFocus(CWnd* pOldWnd) {
    m_nSetFocus++;
    COleControl::OnSetFocus(pOldWnd);
  }
  afx_msg void OnKillFocus(CWnd* pNewWnd) {
    m_nKillFocus++;
    COleControl::OnKillFocus(pNewWnd);
  }

  DECLARE_MESSAGE_MAP()
  DECLARE_OLECREATE_EX(CWlCtrl)
  DECLARE_OLECTLTYPE(CWlCtrl)
  DECLARE_DISPATCH_MAP()
  DECLARE_EVENT_MAP()
};

int CWlCtrl::s_nAlive = 0;
CWlCtrl* CWlCtrl::s_pLast = NULL;

IMPLEMENT_DYNCREATE(CWlCtrl, COleControl)

BEGIN_MESSAGE_MAP(CWlCtrl, COleControl)
  ON_WM_LBUTTONDOWN()
  ON_WM_LBUTTONUP()
  ON_WM_MOUSEMOVE()
  ON_WM_KEYDOWN()
  ON_WM_CHAR()
  ON_WM_SETFOCUS()
  ON_WM_KILLFOCUS()
END_MESSAGE_MAP()

// {7C2E4A63-1F5C-4E8B-A034-9C6F2DBE5A70}
IMPLEMENT_OLECREATE_EX(CWlCtrl, "WlTest.WlCtrl.1", 0x7c2e4a63, 0x1f5c, 0x4e8b, 0xa0, 0x34, 0x9c,
                       0x6f, 0x2d, 0xbe, 0x5a, 0x70)
IMPLEMENT_OLECTLTYPE(CWlCtrl, IDS_WLCTL, kMiscStatus)

BEGIN_DISPATCH_MAP(CWlCtrl, COleControl)
  DISP_STOCKPROP_BACKCOLOR()
END_DISPATCH_MAP()

BEGIN_EVENT_MAP(CWlCtrl, COleControl)
  EVENT_STOCK_CLICK()
END_EVENT_MAP()

BOOL CWlCtrl::CWlCtrlFactory::UpdateRegistry(BOOL) { return TRUE; }

// =====================================================================
// The container

class CMainFrame : public CFrameWnd {
public:
  CWnd m_ctl;
  int m_nClicks;
  CMainFrame() : m_nClicks(0) {}

  void OnClick() { m_nClicks++; }
  void RunTests();
  void TestWindowless();
  void TestDragDrop(CWlCtrl* pCtl, const CRect& rect);
  void TestWindowed();

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
  ON_EVENT(CMainFrame, IDC_WL, DISPID_CLICK, OnClick, VTS_NONE)
END_EVENTSINK_MAP()

static COLORREF PixelAt(CWnd* pWnd, int x, int y) {
  CClientDC dc(pWnd);
  return dc.GetPixel(x, y);
}

void CMainFrame::TestWindowless() {
  TestLog("windowless control");
  g_bAskWindowless = TRUE;
  CRect rect(20, 30, 120, 80);
  BOOL bMade = m_ctl.CreateControl(CWlCtrl::guid, NULL, WS_VISIBLE, rect, this, IDC_WL);
  CHECK(bMade);
  CWlCtrl* pCtl = CWlCtrl::s_pLast;
  CHECK(pCtl != NULL && CWlCtrl::s_nAlive == 1);
  if (!bMade || pCtl == NULL) return;
  COleControlSite* pSite = m_ctl.m_pCtrlSite;

  // Active in place without a window of its own.
  CHECK(pSite != NULL && pSite->IsWindowlessControl());
  CHECK(pCtl->IsWindowless() && pCtl->IsInPlaceActive());
  CHECK(pCtl->m_hWnd == NULL && m_ctl.m_hWnd == NULL);
  CHECK(m_pCtrlCont != NULL && m_pCtrlCont->m_nWindowlessControls == 1);
  CHECK(m_ctl.GetDlgCtrlID() == IDC_WL);
  CRect rcClient;
  pCtl->GetClientRect(&rcClient);
  CHECK(rcClient == CRect(0, 0, rect.Width(), rect.Height()));

  // The frame's painting draws it, in its place.
  pCtl->m_nDraws = 0;
  RedrawWindow(NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW);
  CHECK(pCtl->m_nDraws >= 1 && pCtl->m_rcLastBounds == rect);
  CHECK(PixelAt(this, rect.left + 5, rect.top + 5) == kFill);
  CHECK(PixelAt(this, rect.left - 5, rect.top + 5) != kFill);
  TestSaveWindowBitmap(m_hWnd, _T("wltest.bmp"));

  TestDragDrop(pCtl, rect);

  // InvalidateControl: the control's part of the frame, or a piece of it.
  ValidateRect(NULL);
  pCtl->InvalidateControl();
  CRect rcUpdate;
  CHECK(GetUpdateRect(&rcUpdate) && rcUpdate == rect);
  ValidateRect(NULL);
  CRect rcPiece(10, 10, 20, 20);
  pCtl->InvalidateControl(&rcPiece);
  CHECK(GetUpdateRect(&rcUpdate) && rcUpdate == CRect(30, 40, 40, 50));
  UpdateWindow();

  // The mouse over it is its, in its own coordinates.
  SendMessage(WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(30, 45));
  CHECK(pCtl->m_nDowns == 1 && pCtl->m_ptDown == CPoint(10, 15));
  // It took the mouse and the keyboard, through the frame.
  CHECK(m_pCtrlCont->m_pSiteCapture == pSite && ::GetCapture() == m_hWnd);
  CHECK(pCtl->GetCapture() == pCtl);
  CHECK(m_pCtrlCont->m_pSiteFocus == pSite && pCtl->GetFocus() == pCtl);
  CHECK(pCtl->m_nSetFocus >= 1 && ::GetFocus() == m_hWnd);
  // While it has the mouse, a move outside is its too.
  SendMessage(WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(5, 5));
  CHECK(pCtl->m_nMoves == 1 && pCtl->m_ptMove == CPoint(-15, -25));
  // Up over it: a click, and the mouse goes back.
  SendMessage(WM_LBUTTONUP, 0, MAKELPARAM(40, 50));
  CHECK(pCtl->m_nUps == 1 && m_nClicks == 1);
  CHECK(m_pCtrlCont->m_pSiteCapture == NULL && ::GetCapture() != m_hWnd);
  // Outside it, without the mouse, the frame's own.
  int nMoves = pCtl->m_nMoves;
  SendMessage(WM_MOUSEMOVE, 0, MAKELPARAM(5, 5));
  CHECK(pCtl->m_nMoves == nMoves);

  // Keys go to it while it has the focus.
  SendMessage(WM_KEYDOWN, 'A', 0);
  CHECK(pCtl->m_nLastKey == 'A');
  SendMessage(WM_CHAR, 'a', 0);
  CHECK(pCtl->m_nChars == 1);
  int nKill = pCtl->m_nKillFocus;
  SendMessage(WM_KILLFOCUS, 0, 0);
  CHECK(pCtl->m_nKillFocus == nKill + 1);

  // Drawing on its own through GetDC: the frame's DC, its corner at 0,0.
  UpdateWindow();
  CDC* pDC = pCtl->GetDC(NULL, 0);
  CHECK(pDC != NULL);
  if (pDC != NULL) {
    pDC->FillSolidRect(0, 0, 4, 4, RGB(255, 0, 0));
    pDC->FillSolidRect(-10, -10, 8, 8, RGB(255, 0, 0));  // outside: clipped
    CHECK(pCtl->ReleaseDC(pDC) == 1);
  }
  CHECK(PixelAt(this, rect.left + 1, rect.top + 1) == RGB(255, 0, 0));
  CHECK(PixelAt(this, rect.left - 5, rect.top - 5) != RGB(255, 0, 0));

  // Moved: the control has its new place.
  m_ctl.MoveWindow(50, 60, 80, 40);
  CRect rcPos;
  CHECK(pCtl->GetRectInContainer(&rcPos) && rcPos == CRect(50, 60, 130, 100));
  SendMessage(WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(55, 65));
  CHECK(pCtl->m_nDowns == 2 && pCtl->m_ptDown == CPoint(5, 5));
  SendMessage(WM_LBUTTONUP, 0, MAKELPARAM(55, 65));
  CHECK(m_nClicks == 2);

  // Hidden: not drawn, and the mouse passes it by.
  m_ctl.ShowWindow(SW_HIDE);
  pCtl->m_nDraws = 0;
  RedrawWindow(NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW);
  CHECK(pCtl->m_nDraws == 0);
  SendMessage(WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(55, 65));
  SendMessage(WM_LBUTTONUP, 0, MAKELPARAM(55, 65));
  CHECK(pCtl->m_nDowns == 2);
  m_ctl.ShowWindow(SW_SHOW);
  RedrawWindow(NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW);
  CHECK(pCtl->m_nDraws >= 1);

  // Destroyed: the frame has no windowless controls left.
  m_ctl.DestroyWindow();
  CHECK(CWlCtrl::s_nAlive == 0);
  CHECK(m_pCtrlCont->m_nWindowlessControls == 0 && m_pCtrlCont->m_pSiteFocus == NULL);
  CHECK(m_ctl.m_pCtrlSite == NULL);
}

void CMainFrame::TestDragDrop(CWlCtrl* pCtl, const CRect& rect) {
  // The frame took no drops of its own, so the container made it a drop
  // target for the control.
  CHECK(m_pDropTarget != NULL && m_pDropTarget == m_pCtrlCont->m_pWindowlessDropTarget);
  if (m_pDropTarget == NULL) return;
  LPDROPTARGET pTarget = (LPDROPTARGET)m_pDropTarget->GetInterface(&IID_IDropTarget);
  COleDataSource* pSource = new COleDataSource;
  HGLOBAL hText = ::GlobalAlloc(GMEM_MOVEABLE, 5);
  memcpy(::GlobalLock(hText), "drop", 5);
  ::GlobalUnlock(hText);
  pSource->CacheGlobalData(CF_TEXT, hText);
  LPDATAOBJECT pData = (LPDATAOBJECT)pSource->GetInterface(&IID_IDataObject);

  CTestDropTarget& dt = pCtl->m_dropTarget;
  CPoint ptIn(rect.left + 10, rect.top + 10), ptOut(5, 5);
  ClientToScreen(&ptIn);
  ClientToScreen(&ptOut);
  POINTL plIn = {ptIn.x, ptIn.y}, plOut = {ptOut.x, ptOut.y};
  const DWORD dwBoth = DROPEFFECT_COPY | DROPEFFECT_MOVE;

  // Entering over the control: the control's, in screen coordinates.
  DWORD dwEffect = dwBoth;
  CHECK(pTarget->DragEnter(pData, MK_LBUTTON, plIn, &dwEffect) == S_OK);
  CHECK(dt.m_nEnter == 1 && dwEffect == DROPEFFECT_COPY);
  CHECK(dt.m_ptLast.x == ptIn.x && dt.m_ptLast.y == ptIn.y);
  dwEffect = dwBoth;
  pTarget->DragOver(MK_LBUTTON, plIn, &dwEffect);
  CHECK(dt.m_nOver == 1 && dwEffect == DROPEFFECT_COPY);
  // Off it: the control is left, and the frame takes nothing.
  dwEffect = dwBoth;
  pTarget->DragOver(MK_LBUTTON, plOut, &dwEffect);
  CHECK(dt.m_nLeave == 1 && dwEffect == DROPEFFECT_NONE);
  dwEffect = dwBoth;
  pTarget->DragOver(MK_LBUTTON, plOut, &dwEffect);
  CHECK(dt.m_nOver == 1 && dwEffect == DROPEFFECT_NONE);
  // Back over it, and the drop.
  dwEffect = dwBoth;
  pTarget->DragOver(MK_LBUTTON, plIn, &dwEffect);
  CHECK(dt.m_nEnter == 2 && dwEffect == DROPEFFECT_COPY);
  dwEffect = dwBoth;
  CHECK(pTarget->Drop(pData, 0, plIn, &dwEffect) == S_OK);
  CHECK(dt.m_nDrop == 1 && dwEffect == DROPEFFECT_COPY && dt.m_strDropped == _T("drop"));
  CHECK(dt.m_nLeave == 1 && m_pDropTarget->m_pWindowlessTarget == NULL);

  // A drag that ends over it without a drop.
  dwEffect = dwBoth;
  pTarget->DragEnter(pData, MK_LBUTTON, plIn, &dwEffect);
  pTarget->DragLeave();
  CHECK(dt.m_nEnter == 3 && dt.m_nLeave == 2 && m_pDropTarget->m_lpDataObject == NULL);
  // Dropped outside it: the frame's, which takes nothing.
  dwEffect = dwBoth;
  pTarget->DragEnter(pData, MK_LBUTTON, plOut, &dwEffect);
  CHECK(dt.m_nEnter == 3 && dwEffect == DROPEFFECT_NONE);
  dwEffect = dwBoth;
  pTarget->Drop(pData, 0, plOut, &dwEffect);
  CHECK(dt.m_nDrop == 1 && dwEffect == DROPEFFECT_NONE);
  pSource->InternalRelease();
}

void CMainFrame::TestWindowed() {
  TestLog("windowed control");
  g_bAskWindowless = FALSE;
  CRect rect(20, 30, 120, 80);
  BOOL bMade = m_ctl.CreateControl(CWlCtrl::guid, NULL, WS_VISIBLE, rect, this, IDC_WL);
  CHECK(bMade);
  CWlCtrl* pCtl = CWlCtrl::s_pLast;
  if (!bMade || pCtl == NULL) return;
  CHECK(!pCtl->IsWindowless() && pCtl->m_hWnd != NULL && m_ctl.m_hWnd == pCtl->m_hWnd);
  CHECK(!m_ctl.m_pCtrlSite->IsWindowlessControl() && m_pCtrlCont->m_nWindowlessControls == 0);
  // Its own window gets the mouse.
  pCtl->SendMessage(WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(3, 4));
  CHECK(pCtl->m_nDowns == 1 && pCtl->m_ptDown == CPoint(3, 4) && ::GetCapture() == pCtl->m_hWnd);
  pCtl->SendMessage(WM_LBUTTONUP, 0, MAKELPARAM(3, 4));
  CHECK(::GetCapture() != pCtl->m_hWnd);
  m_ctl.DestroyWindow();
  CHECK(CWlCtrl::s_nAlive == 0);
}

void CMainFrame::RunTests() {
  TestWindowless();
  TestWindowed();
}

// =====================================================================

class CTestApp : public COleControlModule {
public:
  virtual BOOL InitInstance() {
    TestOpenLog(_T("wltest.log"));
    if (!COleControlModule::InitInstance()) return FALSE;
    if (!AfxOleInit()) {
      TestLog("AfxOleInit failed");
      return FALSE;
    }
    AfxEnableControlContainer();
    CHECK(COleObjectFactory::RegisterAll());
    CMainFrame* pFrame = new CMainFrame;
    if (!pFrame->Create(NULL, _T("wltest"), WS_OVERLAPPEDWINDOW, CRect(0, 0, 300, 200)))
      return FALSE;
    m_pMainWnd = pFrame;
    pFrame->ShowWindow(SW_SHOW);
    pFrame->UpdateWindow();
    return TRUE;
  }
  virtual int ExitInstance() {
    COleObjectFactory::RevokeAll();
    CHECK(CWlCtrl::s_nAlive == 0);
    COleControlModule::ExitInstance();
    return TestFinish();
  }
};

CTestApp theApp;
