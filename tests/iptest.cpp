// iptest.cpp - in-place activation, with the server and its container in
// one program (as an in-process server would be): the program registers
// its server document's class, embeds an object of it in its container
// document and activates it in place in the container's view. It checks
// the in-place frame and its position, the shared menu, the server's
// toolbar on the container's frame and the container's toolbar out of
// its way, the keys of both sides, resizing through COleResizeBar, UI
// deactivation and activation again, and full deactivation; then
// CRectTracker on its own.
#include <afxwin.h>
#include <afxext.h>
#include <afxole.h>
#include "iptest.h"
#include "testutil.h"

// {3F6A2C81-9D4E-4B57-A1C2-7E8D5B0F4A36}
static const CLSID CLSID_IPDoc = {
    0x3f6a2c81, 0x9d4e, 0x4b57, {0xa1, 0xc2, 0x7e, 0x8d, 0x5b, 0x0f, 0x4a, 0x36}};
static const COLORREF g_colors[] = {RGB(255, 0, 0), RGB(0, 160, 0), RGB(0, 0, 255)};

static int g_nRecolors = 0;
static int g_nToolsChecks = 0;
static int g_nBarsCreated = 0;
static int g_nPosChanges = 0;

static CString MenuText(HMENU hMenu, int nPos) {
  TCHAR sz[64] = {0};
  ::GetMenuString(hMenu, nPos, sz, 64, MF_BYPOSITION);
  return sz;
}

static CString MenuNames(HMENU hMenu) {
  CString str;
  int n = hMenu != NULL ? ::GetMenuItemCount(hMenu) : 0;
  for (int i = 0; i < n; i++) {
    if (i > 0) str += _T(" ");
    str += MenuText(hMenu, i);
  }
  return str;
}

static COLORREF ScreenPixel(CPoint pt) {
  HDC hdc = ::GetDC(NULL);
  COLORREF cr = ::GetPixel(hdc, pt.x, pt.y);
  ::ReleaseDC(NULL, hdc);
  return cr;
}

static CRect WindowRectIn(HWND hWnd, HWND hWndParent) {
  CRect rect;
  ::GetWindowRect(hWnd, &rect);
  ::MapWindowPoints(NULL, hWndParent, (LPPOINT)&rect, 2);
  return rect;
}

// ---------------------------------------------------------------------
// The server: a document that is a colored rectangle.

class CIPSrvDoc;
static CIPSrvDoc* g_pSrvDoc = NULL;

class CIPSrvItem : public COleServerItem {
public:
  explicit CIPSrvItem(CIPSrvDoc* pDoc);
  virtual BOOL OnDraw(CDC* pDC, CSize& rSize);
  virtual BOOL OnGetExtent(DVASPECT nDrawAspect, CSize& rSize) {
    if (nDrawAspect != DVASPECT_CONTENT) return FALSE;
    rSize = CSize(2000, 1000);
    return TRUE;
  }
};

class CIPSrvDoc : public COleServerDoc {
  DECLARE_DYNCREATE(CIPSrvDoc)
public:
  int m_nColor;
  CIPSrvDoc() : m_nColor(0) { g_pSrvDoc = this; }
  virtual ~CIPSrvDoc() {
    if (g_pSrvDoc == this) g_pSrvDoc = NULL;
  }
  COLORREF Color() const { return g_colors[m_nColor]; }
  void SetColor(int nColor) {
    m_nColor = nColor;
    SetModifiedFlag();
    UpdateAllViews(NULL);
    UpdateAllItems(NULL);
  }
  COleIPFrameWnd* InPlaceFrame() const { return m_pInPlaceFrame; }
  virtual COleServerItem* OnGetEmbeddedItem() { return new CIPSrvItem(this); }
  virtual void Serialize(CArchive& ar) {
    if (ar.IsStoring())
      ar << (DWORD)m_nColor;
    else {
      DWORD dw;
      ar >> dw;
      m_nColor = (int)(dw % 3);
    }
  }
  afx_msg void OnRecolor() {
    g_nRecolors++;
    SetColor((m_nColor + 1) % 3);
  }
  afx_msg void OnColorRed() { SetColor(0); }
  DECLARE_MESSAGE_MAP()
};
IMPLEMENT_DYNCREATE(CIPSrvDoc, COleServerDoc)
BEGIN_MESSAGE_MAP(CIPSrvDoc, COleServerDoc)
  ON_COMMAND(ID_EDIT_RECOLOR, OnRecolor)
  ON_COMMAND(ID_COLOR_RED, OnColorRed)
END_MESSAGE_MAP()

CIPSrvItem::CIPSrvItem(CIPSrvDoc* pDoc) : COleServerItem(pDoc, TRUE) {}

BOOL CIPSrvItem::OnDraw(CDC* pDC, CSize& rSize) {
  pDC->SetMapMode(MM_ANISOTROPIC);
  pDC->SetWindowOrg(0, 0);
  pDC->SetWindowExt(2000, 1000);
  CBrush brush(((CIPSrvDoc*)GetDocument())->Color());
  pDC->FillRect(CRect(0, 0, 2000, 1000), &brush);
  rSize = CSize(2000, 1000);
  return TRUE;
}

class CIPSrvView : public CView {
  DECLARE_DYNCREATE(CIPSrvView)
public:
  virtual void OnDraw(CDC* pDC) {
    CRect rect;
    GetClientRect(&rect);
    pDC->FillSolidRect(rect, ((CIPSrvDoc*)GetDocument())->Color());
  }
};
IMPLEMENT_DYNCREATE(CIPSrvView, CView)

class CIPSrvFrame : public CFrameWnd {
  DECLARE_DYNCREATE(CIPSrvFrame)
};
IMPLEMENT_DYNCREATE(CIPSrvFrame, CFrameWnd)

// The in-place frame, as AppWizard writes it: a resize bar in the frame,
// and a toolbar on the container's frame.
class CInPlaceFrame;
static CInPlaceFrame* g_pIPFrame = NULL;

class CInPlaceFrame : public COleIPFrameWnd {
  DECLARE_DYNCREATE(CInPlaceFrame)
public:
  CToolBar m_wndToolBar;
  COleResizeBar m_wndResizeBar;
  CInPlaceFrame() { g_pIPFrame = this; }
  virtual ~CInPlaceFrame() {
    if (g_pIPFrame == this) g_pIPFrame = NULL;
  }
  afx_msg int OnCreate(LPCREATESTRUCT lpcs) {
    if (COleIPFrameWnd::OnCreate(lpcs) == -1) return -1;
    if (!m_wndResizeBar.Create(this)) return -1;
    return 0;
  }
  virtual BOOL OnCreateControlBars(CFrameWnd* pWndFrame, CFrameWnd*) {
    g_nBarsCreated++;
    m_wndToolBar.SetOwner(this);
    if (!m_wndToolBar.CreateEx(pWndFrame, TBSTYLE_FLAT,
                               WS_CHILD | WS_VISIBLE | CBRS_TOP | CBRS_TOOLTIPS | CBRS_FLYBY) ||
        !m_wndToolBar.LoadToolBar(IDR_SRVR_INPLACE)) {
      TestLog("FAIL: the server's toolbar");
      g_nFailures++;
      return FALSE;
    }
    m_wndToolBar.EnableDocking(CBRS_ALIGN_ANY);
    pWndFrame->EnableDocking(CBRS_ALIGN_ANY);
    pWndFrame->DockControlBar(&m_wndToolBar);
    return TRUE;
  }
  DECLARE_MESSAGE_MAP()
};
IMPLEMENT_DYNCREATE(CInPlaceFrame, COleIPFrameWnd)
BEGIN_MESSAGE_MAP(CInPlaceFrame, COleIPFrameWnd)
  ON_WM_CREATE()
END_MESSAGE_MAP()

// ---------------------------------------------------------------------
// The container.

class CCntrItem : public COleClientItem {
public:
  CRect m_rect;
  explicit CCntrItem(COleDocument* pDoc) : COleClientItem(pDoc), m_rect(20, 20, 220, 120) {}
  virtual void OnGetItemPosition(CRect& rPosition) { rPosition = m_rect; }
  virtual BOOL OnChangeItemPosition(const CRect& rectPos) {
    g_nPosChanges++;
    TestLog("position change to %ld,%ld,%ld,%ld", rectPos.left, rectPos.top, rectPos.right,
            rectPos.bottom);
    if (!COleClientItem::OnChangeItemPosition(rectPos)) return FALSE;
    m_rect = rectPos;
    return TRUE;
  }
};

class CCntrDoc : public COleDocument {
  DECLARE_DYNCREATE(CCntrDoc)
};
IMPLEMENT_DYNCREATE(CCntrDoc, COleDocument)

class CCntrView : public CView {
  DECLARE_DYNCREATE(CCntrView)
public:
  virtual void OnDraw(CDC* pDC) {
    COleDocument* pDoc = (COleDocument*)GetDocument();
    POSITION pos = pDoc->GetStartPosition();
    while (pos != NULL) {
      CCntrItem* pItem = (CCntrItem*)pDoc->GetNextClientItem(pos);
      if (pItem != NULL && !pItem->IsInPlaceActive()) {
        pItem->Draw(pDC, pItem->m_rect);
      }
    }
  }
};
IMPLEMENT_DYNCREATE(CCntrView, CView)

class CMainFrame : public CFrameWnd {
  DECLARE_DYNCREATE(CMainFrame)
public:
  CToolBar m_wndToolBar;
  CStatusBar m_wndStatusBar;
  int m_nStep, m_nWait;
  BOOL m_bBusy;
  CCntrItem* m_pItem;
  HWND m_hWndInPlace;
  CMainFrame() : m_nStep(0), m_nWait(0), m_bBusy(FALSE), m_pItem(NULL), m_hWndInPlace(NULL) {}

  afx_msg int OnCreate(LPCREATESTRUCT lpcs) {
    if (CFrameWnd::OnCreate(lpcs) == -1) return -1;
    static const UINT indicators[] = {ID_SEPARATOR};
    if (!m_wndToolBar.CreateEx(this, TBSTYLE_FLAT, WS_CHILD | WS_VISIBLE | CBRS_TOP) ||
        !m_wndToolBar.LoadToolBar(IDR_CNTR) || !m_wndStatusBar.Create(this) ||
        !m_wndStatusBar.SetIndicators(indicators, 1))
      return -1;
    SetTimer(1, 200, NULL);
    return 0;
  }
  afx_msg void OnToolsCheck() { g_nToolsChecks++; }

  BOOL Wait(BOOL bDone, const char* pszWhat) {
    if (bDone) {
      m_nWait = 0;
      return TRUE;
    }
    if (++m_nWait < 25) return FALSE;
    TestLog("FAIL: gave up waiting for %s", pszWhat);
    g_nFailures++;
    m_nWait = 0;
    return TRUE;
  }

  CCntrView* View() { return (CCntrView*)GetActiveView(); }

  // Where the object is on the screen, and its middle.
  CPoint ItemCenter() {
    CRect rect(m_pItem->m_rect);
    View()->ClientToScreen(&rect);
    return rect.CenterPoint();
  }

  void Redraw() {
    RedrawWindow(NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
  }

  // The object is active in place, with its menus and bars up.
  void CheckUIActive(const char* pszWhen) {
    TestLog("check UI active: %s", pszWhen);
    CHECK(m_pItem->GetItemState() == COleClientItem::activeUIState);
    CHECK(m_pItem->IsInPlaceActive());
    CHECK(g_pSrvDoc != NULL && g_pSrvDoc->IsInPlaceActive());
    CWnd* pWnd = m_pItem->GetInPlaceWindow();
    CHECK(pWnd != NULL && g_pIPFrame != NULL && pWnd->m_hWnd == g_pIPFrame->m_hWnd);
    if (pWnd == NULL || g_pIPFrame == NULL) return;
    m_hWndInPlace = pWnd->m_hWnd;
    CHECK(::GetParent(pWnd->m_hWnd) == View()->m_hWnd);
    // The frame covers the item and the resize border around it.
    int nBorder = g_pIPFrame->m_wndResizeBar.GetBorderWidth();
    CRect rectFrame = WindowRectIn(pWnd->m_hWnd, View()->m_hWnd);
    CRect rectExpect(m_pItem->m_rect);
    rectExpect.InflateRect(nBorder, nBorder);
    TestLog("in-place frame %ld,%ld,%ld,%ld", rectFrame.left, rectFrame.top, rectFrame.right,
            rectFrame.bottom);
    CHECK(rectFrame == rectExpect);
    // The server's view is in it, over the item.
    POSITION pos = g_pSrvDoc->GetFirstViewPosition();
    CView* pSrvView = pos != NULL ? g_pSrvDoc->GetNextView(pos) : NULL;
    CHECK(pSrvView != NULL && pSrvView->GetParentFrame() == g_pIPFrame);
    if (pSrvView != NULL) {
      CRect rectView = WindowRectIn(pSrvView->m_hWnd, View()->m_hWnd);
      TestLog("server view %ld,%ld,%ld,%ld", rectView.left, rectView.top, rectView.right,
              rectView.bottom);
      CHECK(rectView == m_pItem->m_rect);
      CHECK(g_pIPFrame->GetActiveView() == pSrvView);
    }
    // The shared menu: the container's File and Tools, the server's
    // Edit, Color and Help, in group order.
    CString strMenu = MenuNames(::GetMenu(m_hWnd));
    TestLog("menu: %s", (const char*)CStringA(strMenu));
    CHECK(strMenu == _T("&File &Edit &Tools &Color &Help"));
    // The server's toolbar on this frame, the container's out of the way.
    CHECK(!m_wndToolBar.IsWindowVisible());
    CHECK(m_wndStatusBar.IsWindowVisible());
    CHECK(g_pIPFrame->m_wndToolBar.m_hWnd != NULL && g_pIPFrame->m_wndToolBar.IsWindowVisible());
    if (g_pIPFrame->m_wndToolBar.m_hWnd == NULL) return;
    CHECK(::GetAncestor(g_pIPFrame->m_wndToolBar.m_hWnd, GA_ROOT) == m_hWnd);
    CRect rectBar = WindowRectIn(g_pIPFrame->m_wndToolBar.m_hWnd, m_hWnd);
    CRect rectCntrView = WindowRectIn(View()->m_hWnd, m_hWnd);
    TestLog("server toolbar %ld,%ld,%ld,%ld, view top %ld", rectBar.left, rectBar.top,
            rectBar.right, rectBar.bottom, rectCntrView.top);
    CHECK(rectBar.top >= 0 && rectBar.top < 4);
    CHECK(rectCntrView.top >= rectBar.bottom && rectCntrView.top <= rectBar.bottom + 4);
  }

  // Back to the container's own menu and bars.
  void CheckContainerUI(const char* pszWhen) {
    TestLog("check container UI: %s", pszWhen);
    CHECK(::GetMenu(m_hWnd) == m_hMenuDefault);
    CHECK(m_wndToolBar.IsWindowVisible());
    CHECK(g_pIPFrame == NULL || g_pIPFrame->m_wndToolBar.m_hWnd == NULL ||
          !g_pIPFrame->m_wndToolBar.IsWindowVisible());
    CRect rectBar = WindowRectIn(m_wndToolBar.m_hWnd, m_hWnd);
    CRect rectCntrView = WindowRectIn(View()->m_hWnd, m_hWnd);
    CHECK(rectCntrView.top >= rectBar.bottom && rectCntrView.top <= rectBar.bottom + 4);
  }

  void PostKey(HWND hWnd, UINT vk) {
    ::PostMessage(hWnd, WM_KEYDOWN, vk, 1);
    ::PostMessage(hWnd, WM_KEYUP, vk, 0xC0000001);
  }

  void CheckTracker();

  afx_msg void OnTimer(UINT_PTR) {
    if (m_bBusy) return;
    m_bBusy = TRUE;
    Step();
    m_bBusy = FALSE;
  }

  void Step() {
    CCntrDoc* pDoc = (CCntrDoc*)GetActiveDocument();
    switch (m_nStep) {
    case 0: {
      // An object of the program's own class, embedded and shown: it
      // becomes active in place in the view.
      m_pItem = new CCntrItem(pDoc);
      CHECK(m_pItem->CreateNewItem(CLSID_IPDoc, OLERENDER_FORMAT, CF_METAFILEPICT));
      TestLog("created: status %08lx, server doc %p", m_pItem->GetLastStatus(), g_pSrvDoc);
      CHECK(g_pSrvDoc != NULL);
      if (g_pSrvDoc == NULL) {
        Finish();
        return;
      }
      CHECK(!g_pSrvDoc->IsInPlaceActive());
      {
        LPUNKNOWN pDirect = (LPUNKNOWN)g_pSrvDoc->GetInterface(&IID_IOleObject);
        IViewObject2* pVO = NULL;
        HRESULT hr = m_pItem->m_lpObject->QueryInterface(IID_IViewObject2, (void**)&pVO);
        // In one process Wine hands back the server's own object, with no
        // view object; the item then draws from the object's data.
        TestLog("own object %d, view object %d", m_pItem->m_lpObject == pDirect, SUCCEEDED(hr));
        if (pVO) pVO->Release();
      }
      CHECK(m_pItem->DoVerb(OLEIVERB_SHOW, View()));
      CheckUIActive("after OLEIVERB_SHOW");
      CHECK(g_nBarsCreated == 1);
      m_nStep++;
      break;
    }
    case 1: {
      Redraw();
      TestSaveWindowBitmap(m_hWnd, _T("iptest-active.bmp"));
      COLORREF cr = ScreenPixel(ItemCenter());
      TestLog("object drawn %06lx", (unsigned long)cr);
      CHECK(cr == g_colors[0]);
      // The keys: the server's F5 and the container's F6, pressed in the
      // server's view and in the container's frame.
      POSITION pos = g_pSrvDoc->GetFirstViewPosition();
      CView* pSrvView = g_pSrvDoc->GetNextView(pos);
      PostKey(pSrvView->m_hWnd, VK_F5);
      PostKey(pSrvView->m_hWnd, VK_F6);
      PostKey(m_hWnd, VK_F5);
      PostKey(m_hWnd, VK_F6);
      m_nStep++;
      break;
    }
    case 2: {
      if (!Wait(g_nRecolors == 2 && g_nToolsChecks == 2, "the keys")) return;
      TestLog("recolors %d, tools checks %d, color %d", g_nRecolors, g_nToolsChecks,
              g_pSrvDoc->m_nColor);
      CHECK(g_nRecolors == 2);
      CHECK(g_nToolsChecks == 2);
      CHECK(g_pSrvDoc->m_nColor == 2);
      // A command from the server's toolbar goes to the server.
      // The toolbar lives in the container's frame, but a click on its Red
      // button reaches the server: the in-place frame is its owner.
      CToolBar* pBar = &g_pIPFrame->m_wndToolBar;
      CRect rectButton;
      pBar->GetItemRect(pBar->CommandToIndex(ID_COLOR_RED), &rectButton);
      CPoint pt = rectButton.CenterPoint();
      pBar->PostMessage(WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(pt.x, pt.y));
      pBar->PostMessage(WM_LBUTTONUP, 0, MAKELPARAM(pt.x, pt.y));
      m_nStep++;
      break;
    }
    case 3: {
      if (!Wait(g_pSrvDoc->m_nColor == 0, "the toolbar command")) return;
      TestLog("color after the toolbar command %d", g_pSrvDoc->m_nColor);
      CHECK(g_pSrvDoc->m_nColor == 0);
      // Resizing: drag the resize bar's bottom right handle 40 by 20.
      COleResizeBar* pBar = &g_pIPFrame->m_wndResizeBar;
      CRect rect;
      pBar->GetClientRect(&rect);
      CPoint ptDown(rect.right - 2, rect.bottom - 2);
      CPoint ptUp(ptDown.x + 40, ptDown.y + 20);
      CPoint ptScreen(ptUp);
      pBar->ClientToScreen(&ptScreen);
      ::SetCursorPos(ptScreen.x, ptScreen.y);
      pBar->PostMessage(WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(ptDown.x, ptDown.y));
      pBar->PostMessage(WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(ptUp.x, ptUp.y));
      pBar->PostMessage(WM_LBUTTONUP, 0, MAKELPARAM(ptUp.x, ptUp.y));
      m_nStep++;
      break;
    }
    case 4: {
      if (!Wait(g_nPosChanges > 0, "the new position")) return;
      CHECK(g_nPosChanges == 1);
      TestLog("item now %ld,%ld,%ld,%ld", m_pItem->m_rect.left, m_pItem->m_rect.top,
              m_pItem->m_rect.right, m_pItem->m_rect.bottom);
      CHECK(m_pItem->m_rect == CRect(20, 20, 260, 140));
      CheckUIActive("after resizing");
      // UI deactivation: still active in place, the container's UI back.
      m_pItem->DeactivateUI();
      CHECK(m_pItem->GetItemState() == COleClientItem::activeState);
      CHECK(g_pSrvDoc->IsInPlaceActive());
      CHECK(::IsWindow(m_hWndInPlace));
      CheckContainerUI("after DeactivateUI");
      m_nStep++;
      break;
    }
    case 5: {
      // Shown again, it takes its UI back, with the same bars.
      CHECK(m_pItem->DoVerb(OLEIVERB_SHOW, View()));
      CheckUIActive("shown again");
      CHECK(g_nBarsCreated == 1);
      m_nStep++;
      break;
    }
    case 6: {
      // Full deactivation: the frame goes, the server's view goes back to
      // its own frame, and the container draws the object again.
      HWND hWndInPlace = m_hWndInPlace;
      m_pItem->Deactivate();
      CHECK(m_pItem->GetItemState() == COleClientItem::loadedState);
      CHECK(!m_pItem->IsInPlaceActive());
      CHECK(!g_pSrvDoc->IsInPlaceActive());
      CHECK(!::IsWindow(hWndInPlace));
      CHECK(g_pIPFrame == NULL);
      POSITION pos = g_pSrvDoc->GetFirstViewPosition();
      CView* pSrvView = g_pSrvDoc->GetNextView(pos);
      CFrameWnd* pSrvFrame = pSrvView->GetParentFrame();
      CHECK(pSrvFrame != NULL && pSrvFrame->IsKindOf(RUNTIME_CLASS(CIPSrvFrame)));
      CHECK(pSrvFrame != NULL && pSrvFrame->GetActiveView() == pSrvView);
      CheckContainerUI("after Deactivate");
      CHECK(m_pFrameHookGone());
      m_nStep++;
      break;
    }
    case 7: {
      Redraw();
      TestSaveWindowBitmap(m_hWnd, _T("iptest-inactive.bmp"));
      COLORREF cr = ScreenPixel(ItemCenter());
      TestLog("picture after deactivation %06lx", (unsigned long)cr);
      CHECK(cr == g_colors[0]);
      CheckTracker();
      // Done with the object: it closes, and its document goes.
      m_pItem->Delete();
      m_pItem = NULL;
      m_nStep++;
      break;
    }
    case 8:
      if (!Wait(g_pSrvDoc == NULL, "the server document to go")) return;
      CHECK(g_pSrvDoc == NULL);
      Finish();
      break;
    }
  }

  BOOL m_pFrameHookGone() { return m_pNotifyHook == NULL; }

  void Finish() {
    KillTimer(1);
    m_nStep = 99;
    // No question about saving the container's document.
    if (GetActiveDocument() != NULL) GetActiveDocument()->SetModifiedFlag(FALSE);
    PostMessage(WM_CLOSE);
  }

  DECLARE_MESSAGE_MAP()
};
IMPLEMENT_DYNCREATE(CMainFrame, CFrameWnd)
BEGIN_MESSAGE_MAP(CMainFrame, CFrameWnd)
  ON_WM_CREATE()
  ON_WM_TIMER()
  ON_COMMAND(ID_TOOLS_CHECK, OnToolsCheck)
END_MESSAGE_MAP()

// CRectTracker: hit testing, its true rectangle, drawing, and tracking
// driven by posted mouse messages.
void CMainFrame::CheckTracker() {
  CRectTracker tracker(CRect(50, 50, 150, 100),
                       CRectTracker::solidLine | CRectTracker::resizeOutside);
  int n = tracker.m_nHandleSize;
  CHECK(tracker.HitTest(CPoint(100, 75)) == CRectTracker::hitMiddle);
  CHECK(tracker.HitTest(CPoint(150 + n / 2, 100 + n / 2)) == CRectTracker::hitBottomRight);
  CHECK(tracker.HitTest(CPoint(50 - n / 2, 50 - n / 2)) == CRectTracker::hitTopLeft);
  CHECK(tracker.HitTest(CPoint(100, 50 - n / 2)) == CRectTracker::hitTop);
  CHECK(tracker.HitTest(CPoint(50 - n / 2, 75)) == CRectTracker::hitLeft);
  CHECK(tracker.HitTest(CPoint(10, 10)) == CRectTracker::hitNothing);
  CRect rectTrue;
  tracker.GetTrueRect(&rectTrue);
  CHECK(rectTrue == CRect(50 - n, 50 - n, 150 + n, 100 + n));

  // Upside down and back to front, the handles keep to m_rect's corners.
  CRectTracker inverted(CRect(150, 100, 50, 50), CRectTracker::resizeOutside);
  CHECK(inverted.HitTest(CPoint(150 + n / 2, 100 + n / 2)) == CRectTracker::hitTopLeft);
  CHECK(inverted.NormalizeHit(CRectTracker::hitTopLeft) == CRectTracker::hitBottomRight);

  // Drawing.
  CClientDC dcScreen(NULL);
  CDC dc;
  dc.CreateCompatibleDC(&dcScreen);
  CBitmap bitmap;
  bitmap.CreateCompatibleBitmap(&dcScreen, 200, 150);
  CBitmap* pOld = dc.SelectObject(&bitmap);
  dc.FillSolidRect(0, 0, 200, 150, RGB(255, 255, 255));
  tracker.Draw(&dc);
  CHECK(dc.GetPixel(100, 50) == RGB(0, 0, 0));                // the line
  CHECK(dc.GetPixel(100, 75) == RGB(255, 255, 255));          // inside
  CHECK(dc.GetPixel(150 + n / 2, 100 + n / 2) == RGB(0, 0, 0));  // a handle
  dc.SelectObject(pOld);

  // Moving: from the middle by 20, 10.
  HWND hWnd = View()->m_hWnd;
  ::PostMessage(hWnd, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(110, 80));
  ::PostMessage(hWnd, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(120, 85));
  ::PostMessage(hWnd, WM_LBUTTONUP, 0, MAKELPARAM(120, 85));
  CPoint ptScreen(120, 85);
  View()->ClientToScreen(&ptScreen);
  ::SetCursorPos(ptScreen.x, ptScreen.y);
  BOOL bMoved = tracker.Track(View(), CPoint(100, 75));
  TestLog("tracked to %ld,%ld,%ld,%ld", tracker.m_rect.left, tracker.m_rect.top,
          tracker.m_rect.right, tracker.m_rect.bottom);
  CHECK(bMoved);
  CHECK(tracker.m_rect == CRect(70, 60, 170, 110));

  // Escape gives the old rectangle back.
  ::PostMessage(hWnd, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(140, 95));
  ::PostMessage(hWnd, WM_KEYDOWN, VK_ESCAPE, 1);
  CHECK(!tracker.Track(View(), CPoint(120, 85)));
  CHECK(tracker.m_rect == CRect(70, 60, 170, 110));

  // A rubber band, from nothing.
  ::PostMessage(hWnd, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(60, 40));
  ::PostMessage(hWnd, WM_LBUTTONUP, 0, MAKELPARAM(60, 40));
  CRectTracker band;
  CHECK(band.TrackRubberBand(View(), CPoint(10, 10)));
  TestLog("rubber band %ld,%ld,%ld,%ld", band.m_rect.left, band.m_rect.top, band.m_rect.right,
          band.m_rect.bottom);
  CHECK(band.m_rect == CRect(10, 10, 60, 40));
}

// ---------------------------------------------------------------------
// The program.

class CIPApp : public CWinApp {
public:
  COleTemplateServer m_server;

  virtual BOOL InitInstance() {
    TestOpenLog(_T("iptest.log"));
    if (!AfxOleInit()) {
      TestLog("AfxOleInit failed");
      g_nFailures++;
      return FALSE;
    }
    // The server's template: its in-place frame and in-place resources.
    CSingleDocTemplate* pSrvTemplate =
        new CSingleDocTemplate(IDR_SRVR, RUNTIME_CLASS(CIPSrvDoc), RUNTIME_CLASS(CIPSrvFrame),
                               RUNTIME_CLASS(CIPSrvView));
    pSrvTemplate->SetServerInfo(IDR_SRVR_EMBEDDED, IDR_SRVR_INPLACE,
                                RUNTIME_CLASS(CInPlaceFrame));
    // Any number of objects from this program, as an in-process server.
    m_server.ConnectTemplate(CLSID_IPDoc, pSrvTemplate, FALSE);
    CHECK(COleTemplateServer::RegisterAll());
    m_server.UpdateRegistry(OAT_INPLACE_SERVER);

    CSingleDocTemplate* pCntrTemplate =
        new CSingleDocTemplate(IDR_CNTR, RUNTIME_CLASS(CCntrDoc), RUNTIME_CLASS(CMainFrame),
                               RUNTIME_CLASS(CCntrView));
    pCntrTemplate->SetContainerInfo(IDR_CNTR_INPLACE);
    AddDocTemplate(pCntrTemplate);
    AddDocTemplate(pSrvTemplate);
    if (pCntrTemplate->OpenDocumentFile(NULL) == NULL) {
      TestLog("FAIL: no container document");
      g_nFailures++;
      return FALSE;
    }
    m_pMainWnd->SetWindowPos(NULL, 0, 0, 400, 300, SWP_NOMOVE | SWP_NOZORDER);
    m_pMainWnd->UpdateWindow();
    return TRUE;
  }

  virtual int ExitInstance() {
    m_server.Unregister();
    CWinApp::ExitInstance();
    return TestFinish();
  }
};

CIPApp theApp;
