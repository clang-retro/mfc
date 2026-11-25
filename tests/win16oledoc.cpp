// win16oledoc.cpp - tests the OLE document classes of MFC on 16-bit
// Windows. Wine's 16-bit OLE2.DLL is mostly stubs, so most of the test
// is a link check: the test makes a container document, a server
// document and item, a drop target and an in-place frame only in a
// function that does not run (LinkCheck). The part that runs caches data
// in a COleDataSource and asks for its formats through COleDataObject,
// which uses MFC code only.
#include <afxwin.h>
#include <afxole.h>
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

/// Runs the test and prints ALL PASSED when every check holds.
int main(int argc, char** argv) {
  (void)argv;
  // The link check runs only when the program has 100 arguments.
  if (argc == 100) LinkCheck(NULL);
  TestDataTransfer();
  if (g_failures == 0) printf("ALL PASSED\n");
  return g_failures;
}
