// win16oledoc.cpp - tests the OLE document classes of MFC on 16-bit
// Windows. Wine's 16-bit OLE2.DLL is mostly stubs, so most of the test
// is a link check: the test makes a container document, a server
// document and item, a drop target and an in-place frame only in a
// function that does not run (LinkCheck). The part that runs caches data
// in a COleDataSource and asks for its formats through COleDataObject,
// which uses MFC code only. The OLE dialogs (Insert Object, Paste
// Special, Links and Server Busy) run with a hook that closes them.
#include <afxwin.h>
#include <afxole.h>
#include <oledlg.h>
#include <stdio.h>
#include <string.h>

static int g_failures = 0;
#define CHECK(cond)                                          \
  do {                                                       \
    if (!(cond)) {                                           \
      printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
      g_failures++;                                          \
    }                                                        \
  } while (0)

// A container item and its document.
class CTestItem : public COleClientItem {
public:
  /// Makes an item of the document.
  CTestItem(COleDocument* pDoc = NULL) : COleClientItem(pDoc) {}
};

class CTestContainerDoc : public COleDocument {
public:
  /// Makes an empty container document.
  CTestContainerDoc() {}
};

// A server item that draws a rectangle.
class CTestServerItem : public COleServerItem {
public:
  /// Makes the item of a server document.
  CTestServerItem(COleServerDoc* pDoc) : COleServerItem(pDoc, TRUE) {}

  /// Draws the item.
  virtual BOOL OnDraw(CDC* pDC, CSize& rSize) {
    rSize = CSize(1000, 1000);
    pDC->Rectangle(0, 0, 1000, 1000);
    return TRUE;
  }
};

// A server document with one item.
class CTestServerDoc : public COleServerDoc {
public:
  /// Gives the embedded item, made on the first call.
  virtual COleServerItem* OnGetEmbeddedItem() { return new CTestServerItem(this); }
};

/// Uses the container, server, drag and drop and in-place classes, so
/// that the linker must find all of their functions. The test does not
/// call it: Wine's 16-bit OLE2.DLL cannot run them.
void LinkCheck(CWnd* pWnd) {
  CTestContainerDoc container;
  CTestItem* pItem = new CTestItem(&container);
  pItem->CreateNewItem(CLSID_NULL);
  pItem->CreateFromFile("c:\\file.txt");
  pItem->DoVerb(OLEIVERB_SHOW, NULL);
  pItem->Close();
  pItem->Delete();

  CTestServerDoc* pServer = new CTestServerDoc;
  pServer->GetEmbeddedItem();
  pServer->NotifyChanged();
  pServer->OnCloseDocument();

  COleTemplateServer templateServer;
  templateServer.UpdateRegistry(OAT_INPLACE_SERVER);

  COleDropTarget target;
  target.Register(pWnd);
  target.Revoke();

  COleDataSource* pSource = new COleDataSource;
  pSource->DoDragDrop();
  pSource->SetClipboard();
  pSource->InternalRelease();

  COleIPFrameWnd* pFrame = new COleIPFrameWnd;
  pFrame->DestroyWindow();

  COleStreamFile file;
  file.CreateMemoryStream();
  file.Close();

  COleMessageFilter* pFilter = AfxOleGetMessageFilter();
  if (pFilter != NULL) pFilter->BeginBusyState();
  AfxOleInit();
}

/// Caches text in a data source and finds its format through its
/// IDataObject with COleDataObject.
static void TestDataTransfer() {
  COleDataSource* pSource = new COleDataSource;
  const char szText[] = "Hello, OLE";
  HGLOBAL hText = ::GlobalAlloc(GMEM_MOVEABLE | GMEM_SHARE, sizeof(szText));
  CHECK(hText != NULL);
  if (hText == NULL) return;
  memcpy(::GlobalLock(hText), szText, sizeof(szText));
  ::GlobalUnlock(hText);
  pSource->CacheGlobalData(CF_TEXT, hText);

  LPDATAOBJECT lpData = (LPDATAOBJECT)pSource->GetInterface(&IID_IDataObject);
  CHECK(lpData != NULL);
  if (lpData == NULL) return;

  COleDataObject data;
  data.Attach(lpData, FALSE);
  CHECK(data.IsDataAvailable(CF_TEXT));
  CHECK(!data.IsDataAvailable(CF_BITMAP));

  // GetData copies the cached data with OleDuplicateData, which Wine's
  // OLE2.DLL does not have, so the test does not get the data.

  data.BeginEnumFormats();
  FORMATETC fe;
  int nFormats = 0;
  while (data.GetNextFormat(&fe)) {
    CHECK(fe.cfFormat == CF_TEXT && fe.tymed == TYMED_HGLOBAL);
    nFormats++;
  }
  CHECK(nFormats == 1);
  data.Release();
  pSource->InternalRelease();
}

// What the hook of the dialog tests sends when a dialog starts, and what
// it saw.
static UINT g_nHookCommand = IDOK;
static int g_nHookListItems = -1;
static HWND g_hHookDialog = NULL;

/// Gives the count of items in the first list box of a dialog, or -1
/// when the dialog has no list box.
static int CountListItems(HWND hDlg) {
  for (HWND h = ::GetWindow(hDlg, GW_CHILD); h != NULL; h = ::GetWindow(h, GW_HWNDNEXT)) {
    char szClass[16];
    ::GetClassName(h, szClass, sizeof(szClass));
    if (lstrcmpi(szClass, "ListBox") == 0) return (int)::SendMessage(h, LB_GETCOUNT, 0, 0);
  }
  return -1;
}

/// The hook of the dialog tests: when the dialog starts, it keeps the
/// window and the count of list items, and posts the command that closes
/// the dialog.
static UINT CALLBACK CloseHook(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam) {
  (void)wParam;
  (void)lParam;
  if (msg == WM_INITDIALOG) {
    g_hHookDialog = hDlg;
    g_nHookListItems = CountListItems(hDlg);
    ::PostMessage(hDlg, WM_COMMAND, g_nHookCommand, 0);
  }
  return 0;
}

// A link container with one link, for the Links dialog.
struct CTestLinks : IOleUILinkContainerA {
  /// Gives the interface for IUnknown and the container.
  STDMETHOD(QueryInterface)(REFIID, LPVOID* ppv) {
    *ppv = this;
    return S_OK;
  }
  /// Does not count: the object is on the stack.
  STDMETHOD_(ULONG, AddRef)() { return 1; }
  /// Does not count: the object is on the stack.
  STDMETHOD_(ULONG, Release)() { return 1; }
  /// Gives the one link, 1, after 0.
  STDMETHOD_(DWORD, GetNextLink)(DWORD dwLink) { return dwLink == 0 ? 1 : 0; }
  /// Accepts the options.
  STDMETHOD(SetLinkUpdateOptions)(DWORD, DWORD) { return S_OK; }
  /// Gives automatic updates.
  STDMETHOD(GetLinkUpdateOptions)(DWORD, DWORD* lpdw) {
    *lpdw = OLEUPDATE_ALWAYS;
    return S_OK;
  }
  /// Accepts the source.
  STDMETHOD(SetLinkSource)(DWORD, LPSTR, ULONG, ULONG*, BOOL) { return S_OK; }
  /// Gives no names: the dialog shows an empty line for the link.
  STDMETHOD(GetLinkSource)(DWORD, LPSTR* lplpszName, ULONG* lpLen, LPSTR* lplpszFull,
                           LPSTR* lplpszShort, BOOL* lpfAvailable, BOOL* lpfSelected) {
    *lplpszName = *lplpszFull = *lplpszShort = NULL;
    *lpLen = 0;
    *lpfAvailable = TRUE;
    *lpfSelected = FALSE;
    return S_OK;
  }
  /// Does nothing.
  STDMETHOD(OpenLinkSource)(DWORD) { return S_OK; }
  /// Does nothing.
  STDMETHOD(UpdateLink)(DWORD, BOOL, BOOL) { return S_OK; }
  /// Does nothing.
  STDMETHOD(CancelLink)(DWORD) { return S_OK; }
};

/// Runs the Insert Object, Paste Special, Links and Server Busy dialogs
/// with a hook that closes each one, and checks what they give.
static void TestDialogs() {
  OLEUIINSERTOBJECTA io;
  memset(&io, 0, sizeof(io));
  io.cbStruct = sizeof(io);
  io.dwFlags = IOF_SELECTCREATENEW;
  io.lpfnHook = CloseHook;
  char szFile[64] = "c:\\file.txt";
  io.lpszFile = szFile;
  io.cchFile = sizeof(szFile);
  g_nHookCommand = IDOK;
  CHECK(OleUIInsertObjectA(&io) == OLEUI_OK);
  CHECK(io.dwFlags & IOF_SELECTCREATENEW);
  CHECK(lstrcmp(szFile, "c:\\file.txt") == 0);
  g_nHookCommand = IDCANCEL;
  CHECK(OleUIInsertObjectA(&io) == OLEUI_CANCEL);
  io.cbStruct = 0;
  CHECK(OleUIInsertObjectA(&io) == OLEUI_ERR_CBSTRUCTINCORRECT);

  COleDataSource* pSource = new COleDataSource;
  HGLOBAL hText = ::GlobalAlloc(GMEM_MOVEABLE | GMEM_SHARE, 4);
  lstrcpy((LPSTR)::GlobalLock(hText), "abc");
  ::GlobalUnlock(hText);
  pSource->CacheGlobalData(CF_TEXT, hText);
  OLEUIPASTEENTRYA entries[2];
  memset(entries, 0, sizeof(entries));
  entries[0].fmtetc.cfFormat = CF_BITMAP;
  entries[0].fmtetc.dwAspect = DVASPECT_CONTENT;
  entries[0].fmtetc.lindex = -1;
  entries[0].fmtetc.tymed = TYMED_GDI;
  entries[0].lpstrFormatName = "Bitmap";
  entries[0].lpstrResultText = "a bitmap";
  entries[0].dwFlags = OLEUIPASTE_PASTEONLY;
  entries[1].fmtetc.cfFormat = CF_TEXT;
  entries[1].fmtetc.dwAspect = DVASPECT_CONTENT;
  entries[1].fmtetc.lindex = -1;
  entries[1].fmtetc.tymed = TYMED_HGLOBAL;
  entries[1].lpstrFormatName = "Text";
  entries[1].lpstrResultText = "%s as text";
  entries[1].dwFlags = OLEUIPASTE_PASTEONLY;
  OLEUIPASTESPECIALA ps;
  memset(&ps, 0, sizeof(ps));
  ps.cbStruct = sizeof(ps);
  ps.dwFlags = PSF_SELECTPASTE;
  ps.lpfnHook = CloseHook;
  ps.lpSrcDataObj = (LPDATAOBJECT)pSource->GetInterface(&IID_IDataObject);
  ps.arrPasteEntries = entries;
  ps.cPasteEntries = 2;
  ps.nSelectedIndex = -1;
  g_nHookCommand = IDOK;
  g_nHookListItems = -1;
  CHECK(OleUIPasteSpecialA(&ps) == OLEUI_OK);
  CHECK(g_nHookListItems == 1);  // the data source has text and no bitmap
  CHECK(ps.nSelectedIndex == 1);
  CHECK(!ps.fLink);
  CHECK(ps.dwFlags & PSF_SELECTPASTE);
  pSource->InternalRelease();

  CTestLinks links;
  OLEUIEDITLINKSA el;
  memset(&el, 0, sizeof(el));
  el.cbStruct = sizeof(el);
  el.lpfnHook = CloseHook;
  el.lpOleUILinkContainer = &links;
  g_nHookListItems = -1;
  CHECK(OleUIEditLinksA(&el) == OLEUI_OK);
  CHECK(g_nHookListItems == 1);

  OLEUIBUSYA bz;
  memset(&bz, 0, sizeof(bz));
  bz.cbStruct = sizeof(bz);
  bz.lpfnHook = CloseHook;
  bz.hTask = ::GetCurrentTask();
  HWND hBusy = NULL;
  bz.lphWndDialog = &hBusy;
  g_nHookCommand = IDCANCEL;
  g_hHookDialog = NULL;
  CHECK(OleUIBusyA(&bz) == OLEUI_CANCEL);
  CHECK(g_hHookDialog != NULL);
  CHECK(hBusy == NULL);
  bz.hTask = NULL;
  CHECK(OleUIBusyA(&bz) == OLEUI_BZERR_HTASKINVALID);
}

/// Runs the test and prints ALL PASSED when every check holds.
int main(int argc, char** argv) {
  (void)argv;
  // The link check runs only when the program has 100 arguments.
  if (argc == 100) LinkCheck(NULL);
  TestDataTransfer();
  TestDialogs();
  if (g_failures == 0) printf("ALL PASSED\n");
  return g_failures;
}
