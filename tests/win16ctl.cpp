// win16ctl.cpp - tests the OLE control classes of MFC on 16-bit Windows.
// The part that runs uses the stock font and picture objects of MFC
// (OleCreateFontIndirect and OleCreatePictureIndirect) and
// OleTranslateColor. These are MFC code that uses only COMPOBJ.DLL.
// Wine's 16-bit OLE2.DLL is mostly stubs, so the test makes a control,
// hosts it with CWnd::CreateControl and shows a property frame only in a
// function that does not run (LinkCheck). The linker must find all of
// these functions.
#include <afxwin.h>
#include <afxdisp.h>
#include <afxctl.h>
#include <afxocc.h>
#include <olectl.h>
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

// =====================================================================
// A stream in memory

// An IStream on a fixed buffer, for the save and load tests.
class CMemStream : public IStream {
public:
  enum { kSize = 4096 };
  BYTE m_data[kSize];
  ULONG m_nPos;
  ULONG m_nLen;

  /// Makes an empty stream.
  CMemStream() : m_nPos(0), m_nLen(0) {}

  /// Gives the stream for IUnknown and IStream.
  STDMETHOD(QueryInterface)(REFIID iid, LPVOID FAR* ppv) {
    if (iid == IID_IUnknown || iid == IID_IStream) {
      *ppv = this;
      return S_OK;
    }
    *ppv = NULL;
    return E_NOINTERFACE;
  }
  /// Does not count references: the stream is on the stack.
  STDMETHOD_(ULONG, AddRef)() { return 1; }
  /// Does not count references: the stream is on the stack.
  STDMETHOD_(ULONG, Release)() { return 1; }

  /// Copies bytes from the position into pv.
  STDMETHOD(Read)(VOID HUGEP* pv, ULONG cb, ULONG FAR* pcbRead) {
    ULONG n = m_nLen - m_nPos;
    if (cb < n) n = cb;
    _fmemcpy(pv, m_data + m_nPos, (size_t)n);
    m_nPos += n;
    if (pcbRead != NULL) *pcbRead = n;
    return S_OK;
  }
  /// Copies bytes from pv to the position.
  STDMETHOD(Write)(VOID const HUGEP* pv, ULONG cb, ULONG FAR* pcbWritten) {
    if (m_nPos + cb > kSize) return STG_E_MEDIUMFULL;
    _fmemcpy(m_data + m_nPos, pv, (size_t)cb);
    m_nPos += cb;
    if (m_nPos > m_nLen) m_nLen = m_nPos;
    if (pcbWritten != NULL) *pcbWritten = cb;
    return S_OK;
  }
  /// Moves the position.
  STDMETHOD(Seek)(LARGE_INTEGER move, DWORD dwOrigin, ULARGE_INTEGER FAR* pNew) {
    long base = 0;
    if (dwOrigin == STREAM_SEEK_CUR) base = (long)m_nPos;
    if (dwOrigin == STREAM_SEEK_END) base = (long)m_nLen;
    long pos = base + (long)move.LowPart;
    if (pos < 0 || pos > kSize) return STG_E_INVALIDFUNCTION;
    m_nPos = (ULONG)pos;
    if (pNew != NULL) {
      pNew->LowPart = m_nPos;
      pNew->HighPart = 0;
    }
    return S_OK;
  }
  /// Sets the length.
  STDMETHOD(SetSize)(ULARGE_INTEGER size) {
    if (size.LowPart > kSize) return STG_E_MEDIUMFULL;
    m_nLen = size.LowPart;
    return S_OK;
  }
  /// Is not used.
  STDMETHOD(CopyTo)(IStream FAR*, ULARGE_INTEGER, ULARGE_INTEGER FAR*, ULARGE_INTEGER FAR*) {
    return E_NOTIMPL;
  }
  /// Does nothing: the stream has no storage.
  STDMETHOD(Commit)(DWORD) { return S_OK; }
  /// Does nothing: the stream has no storage.
  STDMETHOD(Revert)() { return S_OK; }
  /// Is not used.
  STDMETHOD(LockRegion)(ULARGE_INTEGER, ULARGE_INTEGER, DWORD) { return E_NOTIMPL; }
  /// Is not used.
  STDMETHOD(UnlockRegion)(ULARGE_INTEGER, ULARGE_INTEGER, DWORD) { return E_NOTIMPL; }
  /// Gives the length.
  STDMETHOD(Stat)(STATSTG FAR* pStat, DWORD) {
    memset(pStat, 0, sizeof(*pStat));
    pStat->type = STGTY_STREAM;
    pStat->cbSize.LowPart = m_nLen;
    return S_OK;
  }
  /// Is not used.
  STDMETHOD(Clone)(IStream FAR* FAR*) { return E_NOTIMPL; }

  /// Moves the position to the start.
  void Rewind() { m_nPos = 0; }
};

// =====================================================================
// A property notify sink

// Counts the OnChanged calls of a font.
class CNotifySink : public IPropertyNotifySink {
public:
  int m_nChanged;
  DISPID m_dispid;

  /// Makes a sink with no calls.
  CNotifySink() : m_nChanged(0), m_dispid(0) {}

  /// Gives the sink for IUnknown and IPropertyNotifySink.
  STDMETHOD(QueryInterface)(REFIID iid, LPVOID FAR* ppv) {
    if (iid == IID_IUnknown || iid == IID_IPropertyNotifySink) {
      *ppv = this;
      return S_OK;
    }
    *ppv = NULL;
    return E_NOINTERFACE;
  }
  /// Does not count references: the sink is on the stack.
  STDMETHOD_(ULONG, AddRef)() { return 1; }
  /// Does not count references: the sink is on the stack.
  STDMETHOD_(ULONG, Release)() { return 1; }
  /// Counts a change and keeps its DISPID.
  STDMETHOD(OnChanged)(DISPID dispid) {
    m_nChanged++;
    m_dispid = dispid;
    return S_OK;
  }
  /// Lets every change occur.
  STDMETHOD(OnRequestEdit)(DISPID) { return S_OK; }
};

// =====================================================================
// The tests that run

/// Makes a stock font and checks its attributes, its notifications,
/// Clone, IsEqual and a save and load through a stream.
static void TestFont() {
  FONTDESC fd;
  memset(&fd, 0, sizeof(fd));
  fd.cbSizeofstruct = sizeof(fd);
  fd.lpstrName = (LPOLESTR)OLESTR("Arial");
  fd.cySize.Lo = 120000L;  // 12 points
  fd.cySize.Hi = 0;
  fd.sWeight = FW_BOLD;
  fd.fItalic = TRUE;

  LPFONT pFont = NULL;
  CHECK(OleCreateFontIndirect(&fd, IID_IFont, (LPVOID FAR*)&pFont) == S_OK);
  if (pFont == NULL) return;

  BSTR bstrName = NULL;
  CHECK(pFont->get_Name(&bstrName) == S_OK);
  CHECK(bstrName != NULL && lstrcmpi(bstrName, "Arial") == 0);
  SysFreeString(bstrName);
  CY cy;
  CHECK(pFont->get_Size(&cy) == S_OK);
  CHECK(cy.Lo == 120000L);
  BOOL b = FALSE;
  CHECK(pFont->get_Bold(&b) == S_OK && b);
  CHECK(pFont->get_Italic(&b) == S_OK && b);
  CHECK(pFont->get_Underline(&b) == S_OK && !b);

  HFONT hFont = NULL;
  CHECK(pFont->get_hFont(&hFont) == S_OK && hFont != NULL);

  // The font tells a connected sink about a change.
  CNotifySink sink;
  LPCONNECTIONPOINTCONTAINER pCPC = NULL;
  CHECK(pFont->QueryInterface(IID_IConnectionPointContainer, (LPVOID FAR*)&pCPC) == S_OK);
  LPCONNECTIONPOINT pCP = NULL;
  DWORD dwCookie = 0;
  if (pCPC != NULL) {
    CHECK(pCPC->FindConnectionPoint(IID_IPropertyNotifySink, &pCP) == S_OK);
    if (pCP != NULL) CHECK(pCP->Advise(&sink, &dwCookie) == S_OK);
  }
  CHECK(pFont->put_Underline(TRUE) == S_OK);
  CHECK(sink.m_nChanged == 1 && sink.m_dispid == DISPID_FONT_UNDER);
  CHECK(pFont->get_Underline(&b) == S_OK && b);
  if (pCP != NULL) {
    CHECK(pCP->Unadvise(dwCookie) == S_OK);
    pCP->Release();
  }
  if (pCPC != NULL) pCPC->Release();
  CHECK(pFont->put_Italic(FALSE) == S_OK);
  CHECK(sink.m_nChanged == 1);

  // A clone is equal to the font.
  LPFONT pClone = NULL;
  CHECK(pFont->Clone(&pClone) == S_OK && pClone != NULL);
  if (pClone != NULL) {
    CHECK(pFont->IsEqual(pClone) == S_OK);
    CHECK(pClone->put_Bold(FALSE) == S_OK);
    CHECK(pFont->IsEqual(pClone) == S_FALSE);
    pClone->Release();
  }

  // A save and load through a stream keeps the attributes.
  CMemStream stm;
  LPPERSISTSTREAM pPS = NULL;
  CHECK(pFont->QueryInterface(IID_IPersistStream, (LPVOID FAR*)&pPS) == S_OK);
  if (pPS != NULL) {
    CHECK(pPS->Save(&stm, TRUE) == S_OK);
    CHECK(stm.m_nLen > 0);
    pPS->Release();
  }
  FONTDESC fd2;
  memset(&fd2, 0, sizeof(fd2));
  fd2.cbSizeofstruct = sizeof(fd2);
  fd2.lpstrName = (LPOLESTR)OLESTR("Courier");
  fd2.cySize.Lo = 80000L;
  fd2.sWeight = FW_NORMAL;
  LPFONT pLoaded = NULL;
  CHECK(OleCreateFontIndirect(&fd2, IID_IFont, (LPVOID FAR*)&pLoaded) == S_OK);
  if (pLoaded != NULL) {
    CHECK(pLoaded->QueryInterface(IID_IPersistStream, (LPVOID FAR*)&pPS) == S_OK);
    if (pPS != NULL) {
      stm.Rewind();
      CHECK(pPS->Load(&stm) == S_OK);
      pPS->Release();
    }
    CHECK(pFont->IsEqual(pLoaded) == S_OK);
    CHECK(pLoaded->get_Name(&bstrName) == S_OK);
    CHECK(bstrName != NULL && lstrcmpi(bstrName, "Arial") == 0);
    SysFreeString(bstrName);
    CHECK(pLoaded->get_Underline(&b) == S_OK && b);
    pLoaded->Release();
  }

  // The font gives IFontDisp too.
  LPFONTDISP pDisp = NULL;
  CHECK(pFont->QueryInterface(IID_IFontDisp, (LPVOID FAR*)&pDisp) == S_OK);
  if (pDisp != NULL) pDisp->Release();
  pFont->Release();
}

/// Makes a bitmap picture and a metafile picture, draws them and does a
/// save and load of the bitmap through a stream.
static void TestPicture() {
  HDC hdcScreen = GetDC(NULL);
  HBITMAP hbm = CreateCompatibleBitmap(hdcScreen, 16, 8);
  CHECK(hbm != NULL);

  PICTDESC pd;
  memset(&pd, 0, sizeof(pd));
  pd.cbSizeofstruct = sizeof(pd);
  pd.picType = PICTYPE_BITMAP;
  pd.bmp.hbitmap = hbm;
  LPPICTURE pPict = NULL;
  CHECK(OleCreatePictureIndirect(&pd, IID_IPicture, TRUE, (LPVOID FAR*)&pPict) == S_OK);
  if (pPict != NULL) {
    short type = 0;
    CHECK(pPict->get_Type(&type) == S_OK && type == PICTYPE_BITMAP);
    OLE_XSIZE_HIMETRIC cx = 0;
    OLE_YSIZE_HIMETRIC cy = 0;
    CHECK(pPict->get_Width(&cx) == S_OK && cx > 0);
    CHECK(pPict->get_Height(&cy) == S_OK && cy > 0);
    HDC hdcMem = CreateCompatibleDC(hdcScreen);
    HBITMAP hbmDest = CreateCompatibleBitmap(hdcScreen, 16, 8);
    HGDIOBJ hOld = SelectObject(hdcMem, hbmDest);
    CHECK(pPict->Render(hdcMem, 0, 0, 16, 8, 0, cy, cx, -cy, NULL) == S_OK);
    SelectObject(hdcMem, hOld);
    DeleteObject(hbmDest);
    DeleteDC(hdcMem);

    CMemStream stm;
    LPPERSISTSTREAM pPS = NULL;
    CHECK(pPict->QueryInterface(IID_IPersistStream, (LPVOID FAR*)&pPS) == S_OK);
    if (pPS != NULL) {
      CHECK(pPS->Save(&stm, TRUE) == S_OK);
      CHECK(stm.m_nLen > 40);
      pPS->Release();
    }
    stm.Rewind();
    LPPICTURE pLoaded = NULL;
    CHECK(OleLoadPicture(&stm, (LONG)stm.m_nLen, FALSE, IID_IPicture, (LPVOID FAR*)&pLoaded) ==
          S_OK);
    if (pLoaded != NULL) {
      CHECK(pLoaded->get_Type(&type) == S_OK && type == PICTYPE_BITMAP);
      OLE_XSIZE_HIMETRIC cx2 = 0;
      CHECK(pLoaded->get_Width(&cx2) == S_OK && cx2 == cx);
      pLoaded->Release();
    }
    pPict->Release();
  }

  // A metafile picture.
  HDC hdcMeta = CreateMetaFile(NULL);
  Rectangle(hdcMeta, 0, 0, 100, 100);
  HMETAFILE hmf = CloseMetaFile(hdcMeta);
  CHECK(hmf != NULL);
  memset(&pd, 0, sizeof(pd));
  pd.cbSizeofstruct = sizeof(pd);
  pd.picType = PICTYPE_METAFILE;
  pd.wmf.hmeta = hmf;
  pd.wmf.xExt = 2540;
  pd.wmf.yExt = 2540;
  pPict = NULL;
  CHECK(OleCreatePictureIndirect(&pd, IID_IPicture, TRUE, (LPVOID FAR*)&pPict) == S_OK);
  if (pPict != NULL) {
    short type = 0;
    CHECK(pPict->get_Type(&type) == S_OK && type == PICTYPE_METAFILE);
    OLE_XSIZE_HIMETRIC cx = 0;
    CHECK(pPict->get_Width(&cx) == S_OK && cx == 2540);
    pPict->Release();
  }
  ReleaseDC(NULL, hdcScreen);
}

/// Checks OleTranslateColor with an RGB colour, a system colour and a
/// colour that is not correct.
static void TestColor() {
  COLORREF cr = 0;
  CHECK(OleTranslateColor(RGB(1, 2, 3), NULL, &cr) == S_OK);
  CHECK(cr == RGB(1, 2, 3));
  CHECK(OleTranslateColor(0x80000000L | COLOR_WINDOW, NULL, &cr) == S_OK);
  CHECK(cr == GetSysColor(COLOR_WINDOW));
  CHECK(OleTranslateColor(0x80000000L | 200, NULL, &cr) == E_INVALIDARG);
}

// =====================================================================
// A control and a property page, for the link check

enum { DISPID_VALUE_ = 1 };

// A control with a custom property and the stock properties.
class CTestCtl : public COleControl {
  DECLARE_DYNCREATE(CTestCtl)
public:
  long m_nValue;

  /// Makes the control.
  CTestCtl() : m_nValue(7) {}

  /// Fills the bounds with the background colour and writes the caption.
  virtual void OnDraw(CDC* pDC, const CRect& rcBounds, const CRect&) {
    CBrush brush(TranslateColor(GetBackColor()));
    pDC->FillRect(rcBounds, &brush);
    CFont* pOld = SelectStockFont(pDC);
    CRect rc(rcBounds);
    pDC->DrawText(InternalGetText(), -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    if (pOld != NULL) pDC->SelectObject(pOld);
  }
  /// Saves and loads the custom property.
  virtual void DoPropExchange(CPropExchange* pPX) {
    COleControl::DoPropExchange(pPX);
    PX_Long(pPX, _T("Value"), m_nValue, 7);
  }

  DECLARE_OLECREATE_EX(CTestCtl)
  DECLARE_OLECTLTYPE(CTestCtl)
  DECLARE_PROPPAGEIDS(CTestCtl)
  DECLARE_DISPATCH_MAP()
  DECLARE_EVENT_MAP()
};

IMPLEMENT_DYNCREATE(CTestCtl, COleControl)
// {3C5E8A21-7B4D-4F19-A6E2-5D9C0B7F1A40}
IMPLEMENT_OLECREATE_EX(CTestCtl, "W16CtlTest.TestCtl.1", 0x3c5e8a21, 0x7b4d, 0x4f19, 0xa6, 0xe2,
                       0x5d, 0x9c, 0x0b, 0x7f, 0x1a, 0x40)
IMPLEMENT_OLECTLTYPE(CTestCtl, 1, OLEMISC_ACTIVATEWHENVISIBLE | OLEMISC_SETCLIENTSITEFIRST)

BEGIN_PROPPAGEIDS(CTestCtl, 2)
  PROPPAGEID(CLSID_CFontPropPage)
  PROPPAGEID(CLSID_CColorPropPage)
END_PROPPAGEIDS(CTestCtl)

BEGIN_DISPATCH_MAP(CTestCtl, COleControl)
  DISP_PROPERTY_ID(CTestCtl, "Value", DISPID_VALUE_, m_nValue, VT_I4)
  DISP_STOCKPROP_BACKCOLOR()
  DISP_STOCKPROP_FORECOLOR()
  DISP_STOCKPROP_CAPTION()
  DISP_STOCKPROP_FONT()
  DISP_STOCKPROP_ENABLED()
END_DISPATCH_MAP()

BEGIN_EVENT_MAP(CTestCtl, COleControl)
  EVENT_STOCK_CLICK()
END_EVENT_MAP()

/// Writes or removes the registry keys of the control.
BOOL CTestCtl::CTestCtlFactory::UpdateRegistry(BOOL bRegister) {
  if (bRegister)
    return AfxOleRegisterControlClass(AfxGetInstanceHandle(), m_clsid, m_lpszProgID, 1, 1, 0,
                                      OLEMISC_ACTIVATEWHENVISIBLE, CLSID_NULL, 1, 0);
  return AfxOleUnregisterClass(m_clsid, m_lpszProgID);
}

// A property page with one edit box.
class CTestPage : public COlePropertyPage {
  DECLARE_DYNCREATE(CTestPage)
public:
  long m_nValue;

  /// Makes the page from dialog template 100.
  CTestPage() : COlePropertyPage(100, 1), m_nValue(0) {}

  /// Moves the value between the edit box and the control.
  virtual void DoDataExchange(CDataExchange* pDX) {
    DDP_Text(pDX, 101, m_nValue, _T("Value"));
    DDX_Text(pDX, 101, m_nValue);
    DDP_PostProcessing(pDX);
  }
  DECLARE_OLECREATE_EX(CTestPage)
};

IMPLEMENT_DYNCREATE(CTestPage, COlePropertyPage)
// {3C5E8A21-7B4D-4F19-A6E2-5D9C0B7F1A41}
IMPLEMENT_OLECREATE_EX(CTestPage, "W16CtlTest.TestPage.1", 0x3c5e8a21, 0x7b4d, 0x4f19, 0xa6, 0xe2,
                       0x5d, 0x9c, 0x0b, 0x7f, 0x1a, 0x41)

/// Writes or removes the registry keys of the property page.
BOOL CTestPage::CTestPageFactory::UpdateRegistry(BOOL bRegister) {
  if (bRegister) return AfxOleRegisterPropertyPageClass(AfxGetInstanceHandle(), m_clsid, 1);
  return AfxOleUnregisterClass(m_clsid, NULL);
}

/// Hosts the control, saves and loads its state, shows its property
/// frame and checks a licence file, so that the linker must find all of
/// these functions. The test does not call it: Wine's 16-bit OLE2.DLL
/// cannot run them.
void LinkCheck(CWnd* pWnd) {
  AfxEnableControlContainer();
  COleObjectFactory::RegisterAll();
  COleObjectFactory::UpdateRegistryAll(TRUE);

  CWnd wndCtl;
  wndCtl.CreateControl(CTestCtl::guid, "Test", WS_CHILD | WS_VISIBLE, CRect(0, 0, 100, 50), pWnd,
                       1000);
  wndCtl.CreateControl("W16CtlTest.TestCtl.1", "Test", WS_CHILD, CRect(0, 0, 100, 50), pWnd, 1001);
  COleVariant var(7L);
  wndCtl.SetProperty(DISPID_VALUE_, VT_I4, 8L);
  long n = 0;
  wndCtl.GetProperty(DISPID_VALUE_, VT_I4, &n);
  wndCtl.InvokeHelper(DISPID_REFRESH, DISPATCH_METHOD, VT_EMPTY, NULL, NULL);
  LPUNKNOWN pUnk = wndCtl.GetControlUnknown();
  if (pUnk != NULL) {
    LPPERSISTSTREAMINIT pInit = NULL;
    pUnk->QueryInterface(IID_IPersistStreamInit, (LPVOID FAR*)&pInit);
    if (pInit != NULL) {
      CMemStream stm;
      pInit->Save(&stm, TRUE);
      stm.Rewind();
      pInit->Load(&stm);
      pInit->Release();
    }
    LPSPECIFYPROPERTYPAGES pSpec = NULL;
    pUnk->QueryInterface(IID_ISpecifyPropertyPages, (LPVOID FAR*)&pSpec);
    if (pSpec != NULL) {
      CAUUID pages;
      pSpec->GetPages(&pages);
      OleCreatePropertyFrame(pWnd->GetSafeHwnd(), 0, 0, "Test", 1, &pUnk, pages.cElems,
                             pages.pElems, 0, 0, NULL);
      CoTaskMemFree(pages.pElems);
      pSpec->Release();
    }
  }
  wndCtl.DestroyWindow();

  AfxVerifyLicFile(AfxGetInstanceHandle(), "test.lic", OLESTR("Licence"));
  COleObjectFactory::RevokeAll();
}

/// Runs the test and prints ALL PASSED when every check holds.
int main(int argc, char** argv) {
  (void)argv;
  // The link check runs only when the program has 100 arguments.
  if (argc == 100) LinkCheck(NULL);
  TestFont();
  TestPicture();
  TestColor();
  if (g_failures == 0) printf("ALL PASSED\n");
  return g_failures;
}
