// dragtest.cpp - data transfer with afxole.h: a COleDataSource on the
// clipboard (cached and rendered formats, flushing), a COleDataObject
// reading it, a view's COleDropTarget called through IDropTarget (the
// effects, drag scrolling), and a whole drag with DoDragDrop. On Linux
// and macOS, drags from other programs too: the port's backends hand
// them in through its own functions, as the test does here, and they
// reach the view's drop target or a window that takes files.
#include <afxwin.h>
#include <afxext.h>
#include <afxole.h>
#include "dragtest.h"
#include "testutil.h"

#ifdef _WINPORT
#include <memory>
#include <string>

// As the port's user.h declares them.
namespace winport {
struct Window;
Window* GetWindow(HWND h);
struct DesktopOffer {
  virtual ~DesktopOffer() {}
  bool files = false;
  bool text = false;
  IDataObject* local = NULL;
  virtual bool Fetch(bool /*files*/, std::string* /*bytes*/) { return false; }
  virtual void Answer(DWORD /*effect*/) {}
  virtual void Finished(DWORD /*effect*/) {}
};
typedef std::shared_ptr<DesktopOffer> DesktopOfferPtr;
void DesktopDragOver(Window* top, const DesktopOfferPtr& offer, POINT screen, DWORD keys, DWORD allowed);
void DesktopDragLeave(Window* top, const DesktopOfferPtr& offer);
void DesktopDrop(Window* top, const DesktopOfferPtr& offer, POINT screen, DWORD keys, DWORD allowed);
}  // namespace winport

// Another program's drag, as a backend describes it.
struct TestOffer : winport::DesktopOffer {
  std::string uris, utf8;
  int fetches = 0, answers = 0, finishes = 0;
  DWORD answer = 0xFFFF, finished = 0xFFFF;
  bool Fetch(bool wantFiles, std::string* bytes) override {
    fetches++;
    *bytes = wantFiles ? uris : utf8;
    return true;
  }
  void Answer(DWORD effect) override {
    answers++;
    answer = effect;
  }
  void Finished(DWORD effect) override {
    finishes++;
    finished = effect;
  }
};

// Handles messages until done says so, for at most two seconds.
template <class F>
static bool PumpUntil(F done) {
  DWORD start = ::GetTickCount();
  while (!done()) {
    if (::GetTickCount() - start > 2000) return false;
    MSG msg;
    while (::PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
      ::TranslateMessage(&msg);
      ::DispatchMessage(&msg);
    }
    ::Sleep(5);
  }
  return true;
}

// A window that takes files, and the ones it was given.
static CStringW g_droppedFile;
static POINT g_dropPoint;
static LRESULT CALLBACK FilesProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  if (msg == WM_DROPFILES) {
    WCHAR path[MAX_PATH];
    if (::DragQueryFileW((HDROP)wp, 0xFFFFFFFF, NULL, 0) == 1 && ::DragQueryFileW((HDROP)wp, 0, path, MAX_PATH))
      g_droppedFile = path;
    ::DragQueryPoint((HDROP)wp, &g_dropPoint);
    ::DragFinish((HDROP)wp);
    return 0;
  }
  return ::DefWindowProc(hwnd, msg, wp, lp);
}
#endif

static CLIPFORMAT g_cfRows;
static CLIPFORMAT g_cfFile;

static HGLOBAL GlobalText(const char* psz) {
  size_t n = strlen(psz) + 1;
  HGLOBAL h = ::GlobalAlloc(GMEM_MOVEABLE, n);
  memcpy(::GlobalLock(h), psz, n);
  ::GlobalUnlock(h);
  return h;
}

static CStringA TextOf(HGLOBAL h) {
  if (h == NULL) return "<null>";
  const char* p = (const char*)::GlobalLock(h);
  CStringA s(p, (int)strnlen(p, ::GlobalSize(h)));
  ::GlobalUnlock(h);
  return s;
}

// A data source with one format rendered by OnRenderGlobalData and one by
// OnRenderFileData.
class CTestSource : public COleDataSource {
public:
  static int s_nAlive;
  int m_nRenders = 0;
  CTestSource() { s_nAlive++; }
  virtual ~CTestSource() { s_nAlive--; }
  virtual BOOL OnRenderGlobalData(LPFORMATETC lpFormatEtc, HGLOBAL* phGlobal) {
    if (lpFormatEtc->cfFormat != g_cfRows) return FALSE;
    m_nRenders++;
    *phGlobal = GlobalText("1,2,3");
    return TRUE;
  }
  virtual BOOL OnRenderFileData(LPFORMATETC lpFormatEtc, CFile* pFile) {
    if (lpFormatEtc->cfFormat != g_cfFile) return FALSE;
    m_nRenders++;
    pFile->Write("file data", 10);
    return TRUE;
  }
};
int CTestSource::s_nAlive = 0;

// Drops after a few calls, without a mouse button held.
class CTestDropSource : public COleDropSource {
public:
  int m_nCalls = 0;
  virtual SCODE QueryContinueDrag(BOOL, DWORD) {
    // Let the drag loop find the window under the cursor first.
    return ++m_nCalls >= 6 ? DRAGDROP_S_DROP : S_OK;
  }
};

class CDragDoc : public CDocument {
  DECLARE_DYNCREATE(CDragDoc)
};
IMPLEMENT_DYNCREATE(CDragDoc, CDocument)

class CDragView : public CScrollView {
  DECLARE_DYNCREATE(CDragView)
public:
  COleDropTarget m_dropTarget;
  int m_nEnters = 0, m_nLeaves = 0, m_nDrops = 0;
  CStringA m_strDropped;
  CPoint m_ptDrop;
  DROPEFFECT m_dropEffect = DROPEFFECT_NONE;

  virtual void OnInitialUpdate() {
    CScrollView::OnInitialUpdate();
    SetScrollSizes(MM_TEXT, CSize(2000, 2000));
  }
  virtual void OnDraw(CDC* pDC) { pDC->TextOut(10, 10, _T("Drop text here")); }

  DROPEFFECT Choose(COleDataObject* pDataObject, DWORD dwKeyState) {
    if (!pDataObject->IsDataAvailable(CF_TEXT)) return DROPEFFECT_NONE;
    return (dwKeyState & MK_CONTROL) ? DROPEFFECT_COPY : DROPEFFECT_MOVE;
  }
  virtual DROPEFFECT OnDragEnter(COleDataObject* pDataObject, DWORD dwKeyState, CPoint) {
    m_nEnters++;
    return Choose(pDataObject, dwKeyState);
  }
  virtual DROPEFFECT OnDragOver(COleDataObject* pDataObject, DWORD dwKeyState, CPoint) {
    return Choose(pDataObject, dwKeyState);
  }
  virtual void OnDragLeave() { m_nLeaves++; }
  virtual BOOL OnDrop(COleDataObject* pDataObject, DROPEFFECT dropEffect, CPoint point) {
    m_nDrops++;
    HGLOBAL h = pDataObject->GetGlobalData(CF_TEXT);
    m_strDropped = TextOf(h);
    if (h != NULL) ::GlobalFree(h);
    m_ptDrop = point;
    m_dropEffect = dropEffect;
    return h != NULL;
  }

  afx_msg int OnCreate(LPCREATESTRUCT lpcs) {
    if (CScrollView::OnCreate(lpcs) == -1) return -1;
    m_dropTarget.Register(this);
    return 0;
  }
  DECLARE_MESSAGE_MAP()
};
IMPLEMENT_DYNCREATE(CDragView, CScrollView)
BEGIN_MESSAGE_MAP(CDragView, CScrollView)
  ON_WM_CREATE()
END_MESSAGE_MAP()

class CMainFrame : public CFrameWnd {
  DECLARE_DYNCREATE(CMainFrame)
public:
  int m_nStep = 0;
  BOOL m_bBusy = FALSE;

  afx_msg int OnCreate(LPCREATESTRUCT lpcs) {
    if (CFrameWnd::OnCreate(lpcs) == -1) return -1;
    SetTimer(1, 250, NULL);
    return 0;
  }

  CDragView* View() { return (CDragView*)GetActiveView(); }

  void TestClipboard() {
    CTestSource* pSource = new CTestSource;
    pSource->CacheGlobalData(CF_TEXT, GlobalText("hello"));
    pSource->DelayRenderData(g_cfRows);
    pSource->DelayRenderFileData(g_cfFile);
    pSource->SetClipboard();
    CHECK(COleDataSource::GetClipboardOwner() == pSource);

    COleDataObject data;
    CHECK(data.AttachClipboard());
    CHECK(data.IsDataAvailable(CF_TEXT));
    CHECK(data.IsDataAvailable(g_cfRows));
    CHECK(data.IsDataAvailable(g_cfFile));
    CHECK(!data.IsDataAvailable(CF_BITMAP));
    HGLOBAL h = data.GetGlobalData(CF_TEXT);
    CHECK(TextOf(h) == "hello");
    if (h != NULL) ::GlobalFree(h);
    CFile* pFile = data.GetFileData(g_cfRows);
    CHECK(pFile != NULL);
    if (pFile != NULL) {
      char buf[16] = {0};
      CHECK(pFile->Read(buf, sizeof(buf)) == 6 && strcmp(buf, "1,2,3") == 0);
      delete pFile;
    }
    CHECK(pSource->m_nRenders == 1);
    h = data.GetGlobalData(g_cfFile);
    CHECK(TextOf(h) == "file data");
    if (h != NULL) ::GlobalFree(h);
    CHECK(pSource->m_nRenders == 2);
    // The formats, in the order they were given.
    int nFound = 0;
    data.BeginEnumFormats();
    FORMATETC fmt;
    while (data.GetNextFormat(&fmt)) {
      if (fmt.cfFormat == CF_TEXT || fmt.cfFormat == g_cfRows || fmt.cfFormat == g_cfFile) nFound++;
    }
    CHECK(nFound == 3);
    data.Release();

    // Flushing renders everything onto the clipboard and lets the
    // source go.
    COleDataSource::FlushClipboard();
    CHECK(COleDataSource::GetClipboardOwner() == NULL);
    CHECK(CTestSource::s_nAlive == 0);
    CHECK(::IsClipboardFormatAvailable(g_cfRows));
    if (::OpenClipboard(m_hWnd)) {
      CHECK(TextOf(::GetClipboardData(CF_TEXT)) == "hello");
      CHECK(TextOf(::GetClipboardData(g_cfRows)) == "1,2,3");
      ::CloseClipboard();
    } else {
      CHECK(!"OpenClipboard");
    }
  }

  void TestDropTarget() {
    CDragView* pView = View();
    IDropTarget* pTarget = (IDropTarget*)pView->m_dropTarget.GetInterface(&IID_IDropTarget);
    CHECK(pTarget != NULL);
    if (pTarget == NULL) return;
    CTestSource source;
    source.CacheGlobalData(CF_TEXT, GlobalText("dropped"));
    LPDATAOBJECT pData = (LPDATAOBJECT)source.GetInterface(&IID_IDataObject);
    CRect rect;
    pView->GetClientRect(&rect);
    CPoint ptClient(100, 80);
    CPoint pt = ptClient;
    pView->ClientToScreen(&pt);
    POINTL ptl = {pt.x, pt.y};

    DWORD dwEffect = DROPEFFECT_COPY | DROPEFFECT_MOVE;
    CHECK(pTarget->DragEnter(pData, MK_LBUTTON, ptl, &dwEffect) == S_OK);
    CHECK(dwEffect == DROPEFFECT_MOVE);
    CHECK(pView->m_nEnters == 1);
    dwEffect = DROPEFFECT_COPY | DROPEFFECT_MOVE;
    CHECK(pTarget->DragOver(MK_LBUTTON | MK_CONTROL, ptl, &dwEffect) == S_OK);
    CHECK(dwEffect == DROPEFFECT_COPY);
    // A move the source does not allow becomes a copy.
    dwEffect = DROPEFFECT_COPY;
    CHECK(pTarget->DragOver(MK_LBUTTON, ptl, &dwEffect) == S_OK);
    CHECK(dwEffect == DROPEFFECT_COPY);
    dwEffect = DROPEFFECT_LINK;
    CHECK(pTarget->DragOver(MK_LBUTTON, ptl, &dwEffect) == S_OK);
    CHECK(dwEffect == DROPEFFECT_NONE);

    // Near the bottom edge the view scrolls.
    int nPos = pView->GetScrollPos(SB_VERT);
    CPoint ptEdge(100, rect.bottom - 3);
    pView->ClientToScreen(&ptEdge);
    POINTL ptlEdge = {ptEdge.x, ptEdge.y};
    dwEffect = DROPEFFECT_COPY | DROPEFFECT_MOVE;
    CHECK(pTarget->DragOver(MK_LBUTTON, ptlEdge, &dwEffect) == S_OK);
    CHECK((dwEffect & DROPEFFECT_SCROLL) != 0);
    CHECK(pView->GetScrollPos(SB_VERT) > nPos);
    pView->ScrollToPosition(CPoint(0, 0));

    dwEffect = DROPEFFECT_COPY | DROPEFFECT_MOVE;
    CHECK(pTarget->Drop(pData, MK_LBUTTON, ptl, &dwEffect) == S_OK);
    CHECK(dwEffect == DROPEFFECT_MOVE);
    CHECK(pView->m_nDrops == 1 && pView->m_strDropped == "dropped");
    CHECK(pView->m_ptDrop == ptClient);
    CHECK(pView->m_dropEffect == DROPEFFECT_MOVE);

    // Data without text is refused, and leaving tells the view.
    COleDataSource other;
    other.CacheGlobalData(g_cfRows, GlobalText("x"));
    dwEffect = DROPEFFECT_COPY;
    CHECK(pTarget->DragEnter((LPDATAOBJECT)other.GetInterface(&IID_IDataObject), MK_LBUTTON, ptl,
                             &dwEffect) == S_OK);
    CHECK(dwEffect == DROPEFFECT_NONE);
    CHECK(pTarget->DragLeave() == S_OK);
    CHECK(pView->m_nLeaves == 1);
  }

  void TestDoDragDrop() {
    CDragView* pView = View();
    int nDrops = pView->m_nDrops;
    CPoint pt(120, 90);
    pView->ClientToScreen(&pt);
    ::SetCursorPos(pt.x, pt.y);
    COleDataSource source;
    source.CacheGlobalData(CF_TEXT, GlobalText("dragged"));
    CTestDropSource dropSource;
    CRect rectNone(0, 0, 0, 0);
    DROPEFFECT effect = source.DoDragDrop(DROPEFFECT_COPY, &rectNone, &dropSource);
    TestLog("DoDragDrop: effect %lu, %d calls, %d drops", (unsigned long)effect, dropSource.m_nCalls,
            pView->m_nDrops - nDrops);
    CHECK(effect == DROPEFFECT_COPY);
    CHECK(pView->m_nDrops == nDrops + 1);
    CHECK(pView->m_strDropped == "dragged");
    CHECK(pView->m_dropEffect == DROPEFFECT_COPY);
  }

#ifdef _WINPORT
  void TestDesktopDrag() {
    CDragView* pView = View();
    winport::Window* top = winport::GetWindow(m_hWnd);
    CHECK(top != NULL);
    CPoint ptClient(100, 80);
    CPoint pt = ptClient;
    pView->ClientToScreen(&pt);
    int nEnters = pView->m_nEnters, nLeaves = pView->m_nLeaves, nDrops = pView->m_nDrops;

    // Text from another program reaches the view's drop target, and the
    // effects go back to the backend.
    std::shared_ptr<TestOffer> offer = std::make_shared<TestOffer>();
    offer->files = offer->text = true;
    offer->uris = "file:///tmp/a%20b.txt\r\n";
    offer->utf8 = "from afar \xC3\xA9";
    winport::DesktopOfferPtr p = offer;
    winport::DesktopDragOver(top, p, pt, MK_LBUTTON, DROPEFFECT_COPY | DROPEFFECT_MOVE);
    CHECK(PumpUntil([&] { return offer->answers == 1; }));
    CHECK(offer->answer == DROPEFFECT_MOVE);
    CHECK(pView->m_nEnters == nEnters + 1);
    winport::DesktopDragOver(top, p, pt, MK_LBUTTON | MK_CONTROL, DROPEFFECT_COPY | DROPEFFECT_MOVE);
    CHECK(PumpUntil([&] { return offer->answers == 2; }));
    CHECK(offer->answer == DROPEFFECT_COPY);
    CHECK(pView->m_nEnters == nEnters + 1);
    // Only a copy is allowed.
    winport::DesktopDragOver(top, p, pt, MK_LBUTTON, DROPEFFECT_COPY);
    CHECK(PumpUntil([&] { return offer->answers == 3; }));
    CHECK(offer->answer == DROPEFFECT_COPY);
    winport::DesktopDrop(top, p, pt, 0, DROPEFFECT_COPY | DROPEFFECT_MOVE);
    CHECK(PumpUntil([&] { return offer->finishes == 1; }));
    CHECK(offer->finished == DROPEFFECT_MOVE);
    CHECK(pView->m_nDrops == nDrops + 1);
    CHECK(pView->m_ptDrop == ptClient);
    // CF_TEXT is in the ANSI code page, which is UTF-8 here.
    CHECK(pView->m_strDropped == "from afar \xC3\xA9");
    CHECK(offer->fetches == 1);

    // Files without text: the view refuses them, and the drag leaves.
    std::shared_ptr<TestOffer> files = std::make_shared<TestOffer>();
    files->files = true;
    files->uris = offer->uris;
    winport::DesktopOfferPtr f = files;
    winport::DesktopDragOver(top, f, pt, MK_LBUTTON, DROPEFFECT_COPY);
    CHECK(PumpUntil([&] { return files->answers == 1; }));
    CHECK(files->answer == DROPEFFECT_NONE);
    winport::DesktopDragLeave(top, f);
    CHECK(PumpUntil([&] { return pView->m_nLeaves == nLeaves + 1; }));
    CHECK(files->fetches == 0);

    // A window that takes files gets them as WM_DROPFILES.
    WNDCLASSW wc = {};
    wc.lpfnWndProc = FilesProc;
    wc.hInstance = AfxGetInstanceHandle();
    wc.lpszClassName = L"MfcTestFiles";
    ::RegisterClassW(&wc);
    HWND hFiles = ::CreateWindowExW(WS_EX_TOPMOST | WS_EX_ACCEPTFILES, L"MfcTestFiles", L"", WS_POPUP | WS_VISIBLE,
                                    pt.x - 20, pt.y - 20, 60, 60, NULL, NULL, AfxGetInstanceHandle(), NULL);
    winport::Window* filesTop = winport::GetWindow(hFiles);
    std::shared_ptr<TestOffer> more = std::make_shared<TestOffer>();
    more->files = true;
    more->uris = "file:///tmp/a%20b.txt\r\n";
    winport::DesktopOfferPtr m = more;
    winport::DesktopDragOver(filesTop, m, pt, MK_LBUTTON, DROPEFFECT_COPY | DROPEFFECT_MOVE);
    CHECK(PumpUntil([&] { return more->answers == 1; }));
    CHECK(more->answer == DROPEFFECT_COPY);
    winport::DesktopDrop(filesTop, m, pt, 0, DROPEFFECT_COPY | DROPEFFECT_MOVE);
    CHECK(PumpUntil([&] { return more->finishes == 1 && !g_droppedFile.IsEmpty(); }));
    CHECK(more->finished == DROPEFFECT_COPY);
    CHECK(g_droppedFile == L"/tmp/a b.txt");
    CHECK(g_dropPoint.x == 20 && g_dropPoint.y == 20);
    CHECK(pView->m_nDrops == nDrops + 1);
    ::DestroyWindow(hFiles);
  }
#endif

  afx_msg void OnTimer(UINT_PTR nIDEvent) {
    if (nIDEvent != 1) {
      CFrameWnd::OnTimer(nIDEvent);
      return;
    }
    // The drag runs its own message loop, so a step can be re-entered.
    if (m_bBusy) return;
    m_bBusy = TRUE;
    switch (m_nStep++) {
    case 0:
      TestClipboard();
      TestDropTarget();
      TestSaveWindowBitmap(m_hWnd, _T("dragtest.bmp"));
      break;
    case 1:
      TestDoDragDrop();
      break;
#ifdef _WINPORT
    case 2:
      TestDesktopDrag();
      break;
#endif
    default:
      KillTimer(1);
      PostMessage(WM_CLOSE);
      break;
    }
    m_bBusy = FALSE;
  }
  DECLARE_MESSAGE_MAP()
};
IMPLEMENT_DYNCREATE(CMainFrame, CFrameWnd)
BEGIN_MESSAGE_MAP(CMainFrame, CFrameWnd)
  ON_WM_CREATE()
  ON_WM_TIMER()
END_MESSAGE_MAP()

class CDragApp : public CWinApp {
public:
  virtual BOOL InitInstance() {
    TestOpenLog(_T("dragtest.log"));
    if (!AfxOleInit()) {
      TestLog("AfxOleInit failed");
      g_nFailures++;
      return FALSE;
    }
    g_cfRows = (CLIPFORMAT)::RegisterClipboardFormat(_T("MfcTest Rows"));
    g_cfFile = (CLIPFORMAT)::RegisterClipboardFormat(_T("MfcTest File"));
    CSingleDocTemplate* pTemplate = new CSingleDocTemplate(
        IDR_MAINFRAME, RUNTIME_CLASS(CDragDoc), RUNTIME_CLASS(CMainFrame), RUNTIME_CLASS(CDragView));
    AddDocTemplate(pTemplate);
    CCommandLineInfo cmdInfo;
    ParseCommandLine(cmdInfo);
    if (!ProcessShellCommand(cmdInfo)) {
      TestLog("ProcessShellCommand failed");
      g_nFailures++;
      return FALSE;
    }
    m_pMainWnd->ShowWindow(SW_SHOW);
    m_pMainWnd->UpdateWindow();
    return TRUE;
  }
  virtual int ExitInstance() {
    CWinApp::ExitInstance();
    return TestFinish();
  }
};

CDragApp theApp;
