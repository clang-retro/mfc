// srvtest.cpp - an OLE server and a container of its objects, in one
// program. Run plainly it is the container: it registers itself as the
// server of SrvTest documents and embeds one, which makes COM start it
// again with -Embedding as the server. The container then checks the
// object's picture, extent and data, opens it in the server's window,
// changes it there, has the server update and close it, runs a copy of
// what was saved, and checks the copy the server puts on the clipboard.
#include <afxwin.h>
#include <afxext.h>
#include <afxole.h>
#include <afxtempl.h>
#include "srvtest.h"
#include "testutil.h"

// {8E2B5C14-6F3A-4D21-9B07-5A1C3E7D9F21}
static const CLSID CLSID_SrvDoc = {
    0x8e2b5c14, 0x6f3a, 0x4d21, {0x9b, 0x07, 0x5a, 0x1c, 0x3e, 0x7d, 0x9f, 0x21}};
static const TCHAR g_szClsid[] = _T("{8E2B5C14-6F3A-4D21-9B07-5A1C3E7D9F21}");
static const COLORREF g_colors[] = {RGB(255, 0, 0), RGB(0, 160, 0), RGB(0, 0, 255)};

static CLIPFORMAT CfColor() {
  static CLIPFORMAT cf = (CLIPFORMAT)::RegisterClipboardFormat(_T("SrvTest Color"));
  return cf;
}
static CLIPFORMAT CfProcess() {
  static CLIPFORMAT cf = (CLIPFORMAT)::RegisterClipboardFormat(_T("SrvTest Process"));
  return cf;
}

// ---------------------------------------------------------------------
// The server: a document that is a colored rectangle.

class CSrvDoc;

class CSrvItem : public COleServerItem {
public:
  explicit CSrvItem(CSrvDoc* pDoc);
  CSrvDoc* GetDoc() const { return (CSrvDoc*)GetDocument(); }
  virtual BOOL OnDraw(CDC* pDC, CSize& rSize);
  virtual BOOL OnGetExtent(DVASPECT nDrawAspect, CSize& rSize);
  virtual BOOL OnRenderGlobalData(LPFORMATETC lpFormatEtc, HGLOBAL* phGlobal);
  virtual BOOL OnRenderData(LPFORMATETC lpFormatEtc, LPSTGMEDIUM lpStgMedium) {
    BOOL b = COleServerItem::OnRenderData(lpFormatEtc, lpStgMedium);
    TestLog("render %u aspect %lu tymed %lu: %d", lpFormatEtc->cfFormat, lpFormatEtc->dwAspect,
            lpFormatEtc->tymed, b);
    return b;
  }
};

// What CopyToClipboard puts on the clipboard, as COleServerItem's
// GetClipboardData makes it: the object in a storage of its own, its
// descriptor, and its picture. (The server checks this itself: Wine does
// not hand a server's clipboard data to another program reliably.)
static void CheckClipboardData(LPDATAOBJECT lpData, int nColor) {
  COleDataObject data;
  data.Attach(lpData, FALSE);
  CLIPFORMAT cfEmbed = (CLIPFORMAT)::RegisterClipboardFormat(_T("Embed Source"));
  CLIPFORMAT cfDesc = (CLIPFORMAT)::RegisterClipboardFormat(_T("Object Descriptor"));
  FORMATETC fe = {cfEmbed, NULL, DVASPECT_CONTENT, -1, TYMED_ISTORAGE};
  STGMEDIUM stg = {};
  CHECK(data.GetData(cfEmbed, &stg, &fe));
  if (stg.tymed == TYMED_ISTORAGE && stg.pstg != NULL) {
    CLSID clsid = CLSID_NULL;
    CHECK(::ReadClassStg(stg.pstg, &clsid) == S_OK && ::IsEqualCLSID(clsid, CLSID_SrvDoc));
    // The document's own stream, as the server saves it.
    LPSTREAM lpStream = NULL;
    CHECK(stg.pstg->OpenStream(L"Contents", NULL, STGM_READ | STGM_SHARE_EXCLUSIVE, 0,
                               &lpStream) == S_OK);
    if (lpStream != NULL) {
      DWORD dwColor = 99;
      ULONG cb = 0;
      lpStream->Read(&dwColor, sizeof(dwColor), &cb);
      TestLog("copied color index %lu", dwColor);
      CHECK(cb == sizeof(dwColor) && dwColor == (DWORD)nColor);
      lpStream->Release();
    }
    ::ReleaseStgMedium(&stg);
  }
  HGLOBAL hDesc = data.GetGlobalData(cfDesc);
  CHECK(hDesc != NULL);
  if (hDesc != NULL) {
    OBJECTDESCRIPTOR* pod = (OBJECTDESCRIPTOR*)::GlobalLock(hDesc);
    CHECK(::IsEqualCLSID(pod->clsid, CLSID_SrvDoc));
    CHECK(pod->sizel.cx == 2000 && pod->sizel.cy == 1000);
    CString strType((LPCWSTR)((BYTE*)pod + pod->dwFullUserTypeName));
    TestLog("descriptor: %s", (const char*)CStringA(strType));
    CHECK(strType == _T("SrvTest Document"));
    ::GlobalUnlock(hDesc);
    ::GlobalFree(hDesc);
  }
  // The picture, played into a bitmap.
  FORMATETC feMF = {CF_METAFILEPICT, NULL, DVASPECT_CONTENT, -1, TYMED_MFPICT};
  memset(&stg, 0, sizeof(stg));
  CHECK(data.GetData(CF_METAFILEPICT, &stg, &feMF));
  if (stg.tymed == TYMED_MFPICT) {
    METAFILEPICT* pmfp = (METAFILEPICT*)::GlobalLock(stg.hMetaFilePict);
    CHECK(pmfp->mm == MM_ANISOTROPIC && pmfp->xExt == 2000 && pmfp->yExt == 1000);
    CClientDC dcScreen(NULL);
    CDC dc;
    dc.CreateCompatibleDC(&dcScreen);
    CBitmap bitmap;
    bitmap.CreateCompatibleBitmap(&dcScreen, 40, 20);
    CBitmap* pOld = dc.SelectObject(&bitmap);
    dc.FillSolidRect(0, 0, 40, 20, RGB(255, 255, 255));
    dc.SetMapMode(MM_ANISOTROPIC);
    dc.SetViewportExt(40, 20);
    dc.SetWindowExt(40, 20);
    ::PlayMetaFile(dc.m_hDC, pmfp->hMF);
    dc.SetMapMode(MM_TEXT);
    COLORREF cr = dc.GetPixel(20, 10);
    TestLog("copied picture %06lx", (unsigned long)cr);
    CHECK(cr == g_colors[nColor]);
    dc.SelectObject(pOld);
    ::GlobalUnlock(stg.hMetaFilePict);
    ::ReleaseStgMedium(&stg);
  }
}

class CSrvView : public CView {
  DECLARE_DYNCREATE(CSrvView)
public:
  BOOL m_bInitialUpdate;
  CSrvView() : m_bInitialUpdate(FALSE) {}
  virtual void OnInitialUpdate() {
    m_bInitialUpdate = TRUE;
    CView::OnInitialUpdate();
  }
  virtual void OnDraw(CDC* pDC);
};

class CSrvDoc : public COleServerDoc {
  DECLARE_DYNCREATE(CSrvDoc)
public:
  int m_nColor;
  CSrvDoc() : m_nColor(0) {}
  COLORREF Color() const { return g_colors[m_nColor]; }
  virtual COleServerItem* OnGetEmbeddedItem() { return new CSrvItem(this); }
  virtual BOOL OnNewDocument() {
    if (!COleServerDoc::OnNewDocument()) return FALSE;
    m_nColor = 0;
    return TRUE;
  }
  virtual void Serialize(CArchive& ar) {
    if (ar.IsStoring()) {
      ar << (DWORD)m_nColor;
    } else {
      DWORD n;
      ar >> n;
      m_nColor = (int)(n % 3);
    }
  }
  virtual void OnShowDocument(BOOL bShow) {
    COleServerDoc::OnShowDocument(bShow);
    POSITION pos = GetFirstViewPosition();
    CFrameWnd* pFrame = GetNextView(pos)->GetParentFrame();
    TestLog("show %d", bShow);
    if (!bShow) return;
    // The embedding menu, its commands named after the container's
    // document. (Wine keeps menus to their process, so the server checks
    // this; the container checks the server's exit code.)
    HMENU hMenu = ::GetMenu(pFrame->m_hWnd);
    CHECK(hMenu != NULL && hMenu == GetDefaultMenu());
    TCHAR szItem[64] = {0};
    ::GetMenuString(hMenu, ID_FILE_UPDATE, szItem, 64, MF_BYCOMMAND);
    TestLog("update item: %s", (const char*)CStringA(szItem));
    CHECK(lstrcmp(szItem, _T("&Update Report")) == 0);
    ::GetMenuString(hMenu, ID_APP_EXIT, szItem, 64, MF_BYCOMMAND);
    TestLog("exit item: %s", (const char*)CStringA(szItem));
    CHECK(lstrcmp(szItem, _T("E&xit && Return to Report")) == 0);
    CHECK(((CSrvView*)GetNextView(pos = GetFirstViewPosition()))->m_bInitialUpdate);
  }
  virtual void OnCloseDocument() {
    TestLog("closing");
    COleServerDoc::OnCloseDocument();
    TestLog("closed");
  }
  virtual void OnNewEmbedding(LPSTORAGE lpStorage) {
    TestLog("new embedding");
    COleServerDoc::OnNewEmbedding(lpStorage);
  }
  virtual void OnOpenEmbedding(LPSTORAGE lpStorage) {
    COleServerDoc::OnOpenEmbedding(lpStorage);
    TestLog("loaded embedding, color %d", m_nColor);
  }
  virtual void OnSaveEmbedding(LPSTORAGE lpStorage) {
    TestLog("saving embedding, color %d", m_nColor);
    COleServerDoc::OnSaveEmbedding(lpStorage);
  }
  virtual void OnSetHostNames(LPCTSTR lpszHost, LPCTSTR lpszHostObj) {
    TestLog("host names %s, %s", (const char*)CStringA(lpszHost),
            (const char*)CStringA(lpszHostObj));
    COleServerDoc::OnSetHostNames(lpszHost, lpszHostObj);
  }
  afx_msg void OnRecolor() {
    m_nColor = (m_nColor + 1) % 3;
    TestLog("recolor to %d", m_nColor);
    SetModifiedFlag();
    UpdateAllViews(NULL);
    UpdateAllItems(NULL);
  }
  afx_msg void OnEditCopy() {
    TestLog("copy");
    COleServerItem* pItem = GetEmbeddedItem();
    COleDataSource* pSource = new COleDataSource;
    pItem->GetClipboardData(pSource);
    CheckClipboardData((LPDATAOBJECT)pSource->GetInterface(&IID_IDataObject), m_nColor);
    pSource->InternalRelease();
    pItem->CopyToClipboard();
  }
  DECLARE_MESSAGE_MAP()
};
IMPLEMENT_DYNCREATE(CSrvDoc, COleServerDoc)
BEGIN_MESSAGE_MAP(CSrvDoc, COleServerDoc)
  ON_COMMAND(ID_EDIT_RECOLOR, OnRecolor)
  ON_COMMAND(ID_EDIT_COPY, OnEditCopy)
END_MESSAGE_MAP()

CSrvItem::CSrvItem(CSrvDoc* pDoc) : COleServerItem(pDoc, TRUE) {
  GetDataSource()->DelayRenderData(CfColor());
  GetDataSource()->DelayRenderData(CfProcess());
}

BOOL CSrvItem::OnDraw(CDC* pDC, CSize& rSize) {
  pDC->SetMapMode(MM_ANISOTROPIC);
  pDC->SetWindowOrg(0, 0);
  pDC->SetWindowExt(2000, 1000);
  CBrush brush(GetDoc()->Color());
  pDC->FillRect(CRect(0, 0, 2000, 1000), &brush);
  rSize = CSize(2000, 1000);
  return TRUE;
}

BOOL CSrvItem::OnGetExtent(DVASPECT nDrawAspect, CSize& rSize) {
  if (nDrawAspect != DVASPECT_CONTENT) return FALSE;
  rSize = CSize(2000, 1000);  // HIMETRIC: 2 by 1 cm
  return TRUE;
}

BOOL CSrvItem::OnRenderGlobalData(LPFORMATETC lpFormatEtc, HGLOBAL* phGlobal) {
  DWORD dw;
  if (lpFormatEtc->cfFormat == CfColor())
    dw = GetDoc()->Color();
  else if (lpFormatEtc->cfFormat == CfProcess())
    dw = ::GetCurrentProcessId();
  else
    return COleServerItem::OnRenderGlobalData(lpFormatEtc, phGlobal);
  *phGlobal = ::GlobalAlloc(GMEM_MOVEABLE | GMEM_SHARE, sizeof(DWORD));
  if (*phGlobal == NULL) return FALSE;
  *(DWORD*)::GlobalLock(*phGlobal) = dw;
  ::GlobalUnlock(*phGlobal);
  return TRUE;
}

void CSrvView::OnDraw(CDC* pDC) {
  CRect rect;
  GetClientRect(&rect);
  pDC->FillSolidRect(rect, ((CSrvDoc*)GetDocument())->Color());
}
IMPLEMENT_DYNCREATE(CSrvView, CView)

class CSrvFrame : public CFrameWnd {
  DECLARE_DYNCREATE(CSrvFrame)
};
IMPLEMENT_DYNCREATE(CSrvFrame, CFrameWnd)

// ---------------------------------------------------------------------
// The container

class CCntrDoc : public COleDocument {
public:
  CCntrDoc() { SetTitle(_T("Report")); }
};

class CCntrItem : public COleClientItem {
public:
  int m_nChanged, m_nSaved, m_nClosed;
  explicit CCntrItem(COleDocument* pDoc)
      : COleClientItem(pDoc), m_nChanged(0), m_nSaved(0), m_nClosed(0) {}
  virtual void OnChange(OLE_NOTIFICATION nCode, DWORD dwParam) {
    COleClientItem::OnChange(nCode, dwParam);
    if (nCode == OLE_CHANGED) m_nChanged++;
    if (nCode == OLE_SAVED) m_nSaved++;
    if (nCode == OLE_CLOSED) m_nClosed++;
  }
};

static CString RegValue(LPCTSTR lpszKey) {
  TCHAR sz[MAX_PATH * 2];
  LONG cb = sizeof(sz);
  if (::RegQueryValue(HKEY_CLASSES_ROOT, lpszKey, sz, &cb) != ERROR_SUCCESS) return _T("<none>");
  return sz;
}

static CString ClsidKey(LPCTSTR lpszSub = NULL) {
  CString str = CString(_T("CLSID\\")) + g_szClsid;
  if (lpszSub != NULL) str += CString(_T("\\")) + lpszSub;
  return str;
}

// What the item draws at the middle of a 40 by 20 pixel picture.
static COLORREF ItemColor(COleClientItem* pItem) {
  CClientDC dcScreen(NULL);
  CDC dc;
  dc.CreateCompatibleDC(&dcScreen);
  CBitmap bitmap;
  bitmap.CreateCompatibleBitmap(&dcScreen, 40, 20);
  CBitmap* pOld = dc.SelectObject(&bitmap);
  dc.FillSolidRect(0, 0, 40, 20, RGB(255, 255, 255));
  BOOL bDrawn = pItem->Draw(&dc, CRect(0, 0, 40, 20));
  if (!bDrawn) TestLog("Draw: %08lx", (unsigned long)pItem->GetLastStatus());
  COLORREF cr = dc.GetPixel(20, 10);
  dc.SelectObject(pOld);
  return bDrawn ? cr : (COLORREF)-1;
}

// A DWORD format the object hands out.
static DWORD ItemData(COleClientItem* pItem, CLIPFORMAT cf) {
  if (pItem->m_lpObject == NULL) return 0;
  LPDATAOBJECT lpData = NULL;
  if (FAILED(pItem->m_lpObject->QueryInterface(IID_IDataObject, (void**)&lpData))) return 0;
  FORMATETC fe = {cf, NULL, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
  STGMEDIUM stg;
  DWORD dw = 0;
  HRESULT hr = lpData->GetData(&fe, &stg);
  if (SUCCEEDED(hr)) {
    dw = *(DWORD*)::GlobalLock(stg.hGlobal);
    ::GlobalUnlock(stg.hGlobal);
    ::ReleaseStgMedium(&stg);
  } else {
    TestLog("GetData %u: %08lx", cf, (unsigned long)hr);
  }
  lpData->Release();
  return dw;
}

struct FindWnd {
  DWORD dwPid;
  BOOL bVisible;
  HWND hWnd;
};

static BOOL CALLBACK FindProcessWindow(HWND hWnd, LPARAM lParam) {
  FindWnd* p = (FindWnd*)lParam;
  DWORD dwPid = 0;
  ::GetWindowThreadProcessId(hWnd, &dwPid);
  if (dwPid != p->dwPid || ::GetWindow(hWnd, GW_OWNER) != NULL) return TRUE;
  if (!(::GetWindowLong(hWnd, GWL_STYLE) & WS_CAPTION)) return TRUE;
  if (p->bVisible && !::IsWindowVisible(hWnd)) return TRUE;
  p->hWnd = hWnd;
  return FALSE;
}

// The server's frame window.
static HWND ServerWindow(DWORD dwPid, BOOL bVisible) {
  FindWnd find = {dwPid, bVisible, NULL};
  ::EnumWindows(FindProcessWindow, (LPARAM)&find);
  return find.hWnd;
}

// The server processes, kept open to read their exit codes: each checks
// its own side and exits with 1 if anything failed.
static CMap<DWORD, DWORD, HANDLE, HANDLE> g_processes;

static void TrackProcess(DWORD dwPid) {
  HANDLE h = ::OpenProcess(SYNCHRONIZE | PROCESS_QUERY_INFORMATION, FALSE, dwPid);
  if (h != NULL) g_processes[dwPid] = h;
}

static BOOL ProcessEnded(DWORD dwPid) {
  HANDLE h;
  if (!g_processes.Lookup(dwPid, h)) return TRUE;
  return ::WaitForSingleObject(h, 0) == WAIT_OBJECT_0;
}

static DWORD ProcessExitCode(DWORD dwPid) {
  HANDLE h;
  DWORD dwCode = (DWORD)-1;
  if (g_processes.Lookup(dwPid, h)) ::GetExitCodeProcess(h, &dwCode);
  return dwCode;
}

class CCntrFrame : public CFrameWnd {
public:
  int m_nStep, m_nWait;
  BOOL m_bBusy;
  CCntrDoc* m_pDoc;
  CCntrItem* m_pItem;
  CCntrItem* m_pClone;
  DWORD m_dwPid, m_dwPid2;
  HWND m_hServer;

  CCntrFrame()
      : m_nStep(0), m_nWait(0), m_bBusy(FALSE), m_pDoc(NULL), m_pItem(NULL), m_pClone(NULL),
        m_dwPid(0), m_dwPid2(0), m_hServer(NULL) {}

  afx_msg int OnCreate(LPCREATESTRUCT lpcs) {
    if (CFrameWnd::OnCreate(lpcs) == -1) return -1;
    SetTimer(1, 250, NULL);
    return 0;
  }

  // Stays on this step until bDone or about 10 seconds have gone.
  BOOL Wait(BOOL bDone, const char* pszWhat) {
    if (bDone) {
      m_nWait = 0;
      return TRUE;
    }
    if (++m_nWait < 40) return FALSE;
    TestLog("FAIL: gave up waiting for %s", pszWhat);
    g_nFailures++;
    m_nWait = 0;
    return TRUE;
  }

  void Finish() {
    KillTimer(1);
    PostMessage(WM_CLOSE);
  }

  afx_msg void OnTimer(UINT_PTR nIDEvent) {
    if (nIDEvent != 1 || m_bBusy) return;
    m_bBusy = TRUE;
    Step();
    m_bBusy = FALSE;
  }

  void Step() {
    switch (m_nStep) {
      case 0: {
        // The registration UpdateRegistry wrote.
        TestLog("ProgID class: %s", (const char*)CStringA(RegValue(_T("SrvTest.Document\\CLSID"))));
        CHECK(RegValue(_T("SrvTest.Document\\CLSID")) == g_szClsid);
        CHECK(RegValue(_T("SrvTest.Document")) == _T("SrvTest Document"));
        CHECK(RegValue(_T("SrvTest.Document\\Insertable")) != _T("<none>"));
        CHECK(RegValue(ClsidKey()) == _T("SrvTest Document"));
        CString strServer = RegValue(ClsidKey(_T("LocalServer32")));
        TestLog("LocalServer32: %s", (const char*)CStringA(strServer));
        CHECK(strServer.Find(_T("srvtest")) >= 0 && strServer.Find(_T("/Automation")) < 0);
        CHECK(RegValue(ClsidKey(_T("ProgID"))) == _T("SrvTest.Document"));
        CHECK(RegValue(ClsidKey(_T("InprocHandler32"))).CompareNoCase(_T("ole32.dll")) == 0);
        CHECK(RegValue(ClsidKey(_T("AuxUserType\\2"))) == _T("SrvTest"));
        CHECK(RegValue(ClsidKey(_T("verb\\0"))) == _T("&Edit,0,2"));
        CHECK(RegValue(ClsidKey(_T("verb\\1"))) == _T("&Open,0,2"));
        CHECK(RegValue(ClsidKey(_T("Insertable"))) != _T("<none>"));

        // A new object: COM starts the server.
        m_pDoc = new CCntrDoc;
        m_pItem = new CCntrItem(m_pDoc);
        // Cached as a metafile: Wine's object handler draws nothing for the
        // default (OLERENDER_DRAW), whose cache takes no data from a server
        // program.
        BOOL bCreated = m_pItem->CreateNewItem(CLSID_SrvDoc, OLERENDER_FORMAT, CF_METAFILEPICT);
        TestLog("CreateNewItem: %d (%08lx)", bCreated, (unsigned long)m_pItem->GetLastStatus());
        CHECK(bCreated);
        if (!bCreated) {
          Finish();
          return;
        }
        CHECK(m_pItem->GetType() == OT_EMBEDDED);
        CHECK(m_pItem->IsRunning());
        CString strType;
        m_pItem->GetUserType(USERCLASSTYPE_FULL, strType);
        TestLog("user type: %s", (const char*)CStringA(strType));
        CHECK(strType == _T("SrvTest Document"));
        CSize size(0, 0);
        CHECK(m_pItem->GetExtent(&size));
        TestLog("extent %d x %d", size.cx, size.cy);
        CHECK(size == CSize(2000, 1000));
        m_dwPid = ItemData(m_pItem, CfProcess());
        TestLog("server process %lu (this one %lu)", m_dwPid, ::GetCurrentProcessId());
        CHECK(m_dwPid != 0 && m_dwPid != ::GetCurrentProcessId());
        TrackProcess(m_dwPid);
        CHECK(ItemData(m_pItem, CfColor()) == g_colors[0]);
        COLORREF cr = ItemColor(m_pItem);
        TestLog("drawn %06lx", (unsigned long)cr);
        CHECK(cr == g_colors[0]);
        // The server keeps its window hidden until a verb shows it.
        CHECK(ServerWindow(m_dwPid, TRUE) == NULL);
        CHECK(ServerWindow(m_dwPid, FALSE) != NULL);
        m_nStep++;
        break;
      }
      case 1:
        // Open: the server shows the object in its own window.
        CHECK(m_pItem->DoVerb(OLEIVERB_OPEN, NULL));
        m_nStep++;
        break;
      case 2: {
        m_hServer = ServerWindow(m_dwPid, TRUE);
        if (!Wait(m_hServer != NULL && m_pItem->GetItemState() == COleClientItem::openState,
                  "the server window"))
          return;
        CHECK(m_pItem->IsOpen());
        if (m_hServer == NULL) {
          Finish();
          return;
        }
        TCHAR szTitle[256];
        ::GetWindowText(m_hServer, szTitle, 256);
        TestLog("server window: %s", (const char*)CStringA(szTitle));
        CHECK(_tcsstr(szTitle, _T("Report")) != NULL);
        TestSaveWindowBitmap(m_hServer, _T("srvopen.bmp"));
        // Change it in the server.
        ::PostMessage(m_hServer, WM_COMMAND, ID_EDIT_RECOLOR, 0);
        m_nStep++;
        break;
      }
      case 3: {
        // The container hears of the change and draws the new picture.
        if (!Wait(m_pItem->m_nChanged > 0 && ItemColor(m_pItem) == g_colors[1], "the change"))
          return;
        TestLog("changes %d, drawn %06lx", m_pItem->m_nChanged,
                (unsigned long)ItemColor(m_pItem));
        CHECK(ItemColor(m_pItem) == g_colors[1]);
        CHECK(ItemData(m_pItem, CfColor()) == g_colors[1]);
        // File Update saves the object into the container.
        ::PostMessage(m_hServer, WM_COMMAND, ID_FILE_UPDATE, 0);
        m_nStep++;
        break;
      }
      case 4: {
        if (!Wait(m_pItem->m_nSaved > 0, "the update")) return;
        TestLog("saved %d", m_pItem->m_nSaved);
        CHECK(!m_pItem->IsModified());
        // Close: the server saves if it must, and goes.
        m_pItem->Close(OLECLOSE_SAVEIFDIRTY);
        TestLog("closed %d, state %u", m_pItem->m_nClosed, m_pItem->GetItemState());
        CHECK(m_pItem->m_nClosed > 0);
        CHECK(m_pItem->GetItemState() == COleClientItem::loadedState);
        CHECK(!m_pItem->IsRunning());
        m_nStep++;
        break;
      }
      case 5: {
        if (!Wait(ProcessEnded(m_dwPid), "the server to end")) return;
        CHECK(ProcessEnded(m_dwPid));
        TestLog("server exit code %lu", ProcessExitCode(m_dwPid));
        CHECK(ProcessExitCode(m_dwPid) == 0);
        // The cached picture stays.
        CHECK(ItemColor(m_pItem) == g_colors[1]);
        // A copy of the saved object, run: a new server loads what was
        // saved. (Running the same item again would not show this under
        // Wine, whose object handler starts a server that has only been
        // saved since it was created with a new, empty document.)
        m_pClone = new CCntrItem(m_pDoc);
        CHECK(m_pClone->CreateCloneFrom(m_pItem));
        m_pClone->Run();
        CHECK(m_pClone->IsRunning());
        m_dwPid2 = ItemData(m_pClone, CfProcess());
        TestLog("second server %lu", m_dwPid2);
        CHECK(m_dwPid2 != 0 && m_dwPid2 != m_dwPid);
        TrackProcess(m_dwPid2);
        DWORD dwColor = ItemData(m_pClone, CfColor());
        TestLog("loaded color %06lx", (unsigned long)dwColor);
        CHECK(dwColor == g_colors[1]);
        // The server puts a copy on the clipboard.
        HWND hWnd = ServerWindow(m_dwPid2, FALSE);
        CHECK(hWnd != NULL);
        if (hWnd != NULL) ::PostMessage(hWnd, WM_COMMAND, ID_EDIT_COPY, 0);
        m_nStep++;
        break;
      }
      case 6: {
        if (!Wait(COleClientItem::CanPaste(), "the copy on the clipboard")) return;
        CHECK(COleClientItem::CanPaste());
        ::OleSetClipboard(NULL);
        m_pClone->Close(OLECLOSE_NOSAVE);
        m_nStep++;
        break;
      }
      case 7: {
        if (!Wait(ProcessEnded(m_dwPid2), "the servers to end"))
          return;
        CHECK(ProcessEnded(m_dwPid2) && ProcessExitCode(m_dwPid2) == 0);
        POSITION pos = g_processes.GetStartPosition();
        while (pos != NULL) {
          DWORD dwPid;
          HANDLE h;
          g_processes.GetNextAssoc(pos, dwPid, h);
          ::CloseHandle(h);
        }
        g_processes.RemoveAll();
        delete m_pDoc;  // and its items
        m_pDoc = NULL;
        Finish();
        break;
      }
    }
  }
  DECLARE_MESSAGE_MAP()
};
BEGIN_MESSAGE_MAP(CCntrFrame, CFrameWnd)
  ON_WM_CREATE()
  ON_WM_TIMER()
END_MESSAGE_MAP()

// ---------------------------------------------------------------------

class CSrvApp : public CWinApp {
public:
  COleTemplateServer m_server;
  BOOL m_bServer;
  CSrvApp() : m_bServer(FALSE) {}

  virtual BOOL InitInstance() {
    CCommandLineInfo cmdInfo;
    ParseCommandLine(cmdInfo);
    m_bServer = cmdInfo.m_bRunEmbedded;
    CString strLog;
    if (m_bServer)
      strLog.Format(_T("srvtest-server-%lu.log"), ::GetCurrentProcessId());
    else
      strLog = _T("srvtest.log");
    TestOpenLog(strLog);
    if (!AfxOleInit()) {
      TestLog("AfxOleInit failed");
      g_nFailures++;
      return FALSE;
    }
    CSingleDocTemplate* pTemplate = new CSingleDocTemplate(
        IDR_MAINFRAME, RUNTIME_CLASS(CSrvDoc), RUNTIME_CLASS(CSrvFrame), RUNTIME_CLASS(CSrvView));
    pTemplate->SetServerInfo(IDR_SRVR_EMBEDDED);
    AddDocTemplate(pTemplate);
    // One process for each object, as AppWizard's SDI servers do.
    m_server.ConnectTemplate(CLSID_SrvDoc, pTemplate, TRUE);

    if (m_bServer) {
      TestLog("server: user control %d", AfxOleGetUserCtrl());
      CHECK(!AfxOleGetUserCtrl());
      CHECK(COleTemplateServer::RegisterAll());
      return TRUE;  // COM makes the document
    }

    m_server.UpdateRegistry(OAT_INPLACE_SERVER);
    CCntrFrame* pFrame = new CCntrFrame;
    pFrame->Create(NULL, _T("SrvTest Container"));
    m_pMainWnd = pFrame;
    pFrame->SetWindowPos(NULL, 0, 0, 320, 200, SWP_NOMOVE | SWP_NOZORDER);
    pFrame->ShowWindow(SW_SHOW);
    pFrame->UpdateWindow();
    return TRUE;
  }

  virtual int ExitInstance() {
    if (!m_bServer) {
      m_server.Unregister();
      CHECK(RegValue(ClsidKey()) == _T("<none>"));
      CHECK(RegValue(_T("SrvTest.Document")) == _T("<none>"));
    }
    CWinApp::ExitInstance();
    if (!m_bServer) CHECK(AfxOleCanExitApp());
    return TestFinish();
  }
};

CSrvApp theApp;
