// viewhtml.cpp - CHtmlView, a view that shows a web page with the
// WebBrowser control, and CHtmlControlSite, the control's site that
// answers the browser's IDocHostUIHandler questions through the view.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include "stdafx.h"
#include "afxhtml.h"

// The forms command group of an HTML document.
static const GUID kCmdSetForms3 = {
    0xde4ba900, 0x59ca, 0x11cf, {0x95, 0x92, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00}};

static CString StringFromBSTR(BSTR bstr) {
  CString str;
  if (bstr != NULL) str = CStringW(bstr, (int)::SysStringLen(bstr));
  return str;
}

// The text of a VARIANT that holds a string, or one that converts to it.
static CString StringFromVariant(const VARIANT* pVar) {
  CString str;
  if (pVar == NULL) return str;
  if (pVar->vt == (VT_BYREF | VT_VARIANT)) pVar = pVar->pvarVal;
  VARIANT var;
  ::VariantInit(&var);
  if (SUCCEEDED(::VariantChangeType(&var, const_cast<VARIANT*>(pVar), 0, VT_BSTR)))
    str = StringFromBSTR(var.bstrVal);
  ::VariantClear(&var);
  return str;
}

// A VT_BSTR VARIANT for an optional string argument; VT_EMPTY for NULL.
static void InitStringVariant(VARIANT* pVar, LPCTSTR lpsz) {
  ::VariantInit(pVar);
  if (lpsz != NULL) {
    pVar->vt = VT_BSTR;
    pVar->bstrVal = ::SysAllocString(CStringW(lpsz));
  }
}

// A VT_ARRAY | VT_UI1 VARIANT with the bytes; VT_EMPTY when there are none.
static void InitBytesVariant(VARIANT* pVar, const void* pv, DWORD cb) {
  ::VariantInit(pVar);
  if (pv == NULL || cb == 0) return;
  SAFEARRAY* psa = ::SafeArrayCreateVector(VT_UI1, 0, cb);
  if (psa == NULL) return;
  void* pData = NULL;
  if (SUCCEEDED(::SafeArrayAccessData(psa, &pData))) {
    memcpy(pData, pv, cb);
    ::SafeArrayUnaccessData(psa);
  }
  pVar->vt = VT_ARRAY | VT_UI1;
  pVar->parray = psa;
}

// ---------------------------------------------------------------------
// CHtmlControlSite

BEGIN_INTERFACE_MAP(CHtmlControlSite, COleControlSite)
  INTERFACE_PART(CHtmlControlSite, IID_IDocHostUIHandler, DocHostUIHandler)
END_INTERFACE_MAP()

CHtmlControlSite::CHtmlControlSite(COleControlContainer* pParentWnd)
    : COleControlSite(pParentWnd) {}

CHtmlView* CHtmlControlSite::GetView() const {
  CWnd* pWnd = m_pCtrlCont != NULL ? m_pCtrlCont->m_pWnd : NULL;
  return pWnd != NULL && pWnd->IsKindOf(RUNTIME_CLASS(CHtmlView)) ? (CHtmlView*)pWnd : NULL;
}

STDMETHODIMP_(ULONG) CHtmlControlSite::XDocHostUIHandler::AddRef() {
  METHOD_PROLOGUE_EX_(CHtmlControlSite, DocHostUIHandler)
  return pThis->ExternalAddRef();
}
STDMETHODIMP_(ULONG) CHtmlControlSite::XDocHostUIHandler::Release() {
  METHOD_PROLOGUE_EX_(CHtmlControlSite, DocHostUIHandler)
  return pThis->ExternalRelease();
}
STDMETHODIMP CHtmlControlSite::XDocHostUIHandler::QueryInterface(REFIID iid, LPVOID* ppv) {
  METHOD_PROLOGUE_EX_(CHtmlControlSite, DocHostUIHandler)
  return (HRESULT)pThis->ExternalQueryInterface(&iid, ppv);
}

// Each method asks the view, or gives the browser's own default when the
// site has no view.
#define HTMLSITE_VIEW(failValue)                       \
  METHOD_PROLOGUE_EX_(CHtmlControlSite, DocHostUIHandler) \
  CHtmlView* pView = pThis->GetView();                 \
  if (pView == NULL) return failValue;

STDMETHODIMP CHtmlControlSite::XDocHostUIHandler::ShowContextMenu(DWORD dwID, LPPOINT ppt,
                                                                  LPUNKNOWN pcmdt,
                                                                  LPDISPATCH pdisp) {
  HTMLSITE_VIEW(S_FALSE)
  return pView->OnShowContextMenu(dwID, ppt, pcmdt, pdisp);
}
STDMETHODIMP CHtmlControlSite::XDocHostUIHandler::GetHostInfo(DOCHOSTUIINFO* pInfo) {
  HTMLSITE_VIEW(E_NOTIMPL)
  return pView->OnGetHostInfo(pInfo);
}
STDMETHODIMP CHtmlControlSite::XDocHostUIHandler::ShowUI(DWORD dwID,
                                                         LPOLEINPLACEACTIVEOBJECT pActiveObject,
                                                         LPOLECOMMANDTARGET pCommandTarget,
                                                         LPOLEINPLACEFRAME pFrame,
                                                         LPOLEINPLACEUIWINDOW pDoc) {
  HTMLSITE_VIEW(S_FALSE)
  return pView->OnShowUI(dwID, pActiveObject, pCommandTarget, pFrame, pDoc);
}
STDMETHODIMP CHtmlControlSite::XDocHostUIHandler::HideUI() {
  HTMLSITE_VIEW(E_NOTIMPL)
  return pView->OnHideUI();
}
STDMETHODIMP CHtmlControlSite::XDocHostUIHandler::UpdateUI() {
  HTMLSITE_VIEW(E_NOTIMPL)
  return pView->OnUpdateUI();
}
STDMETHODIMP CHtmlControlSite::XDocHostUIHandler::EnableModeless(BOOL fEnable) {
  HTMLSITE_VIEW(E_NOTIMPL)
  return pView->OnEnableModeless(fEnable);
}
STDMETHODIMP CHtmlControlSite::XDocHostUIHandler::OnDocWindowActivate(BOOL fActivate) {
  HTMLSITE_VIEW(E_NOTIMPL)
  return pView->OnDocWindowActivate(fActivate);
}
STDMETHODIMP CHtmlControlSite::XDocHostUIHandler::OnFrameWindowActivate(BOOL fActivate) {
  HTMLSITE_VIEW(E_NOTIMPL)
  return pView->OnFrameWindowActivate(fActivate);
}
STDMETHODIMP CHtmlControlSite::XDocHostUIHandler::ResizeBorder(LPCRECT prcBorder,
                                                               LPOLEINPLACEUIWINDOW pUIWindow,
                                                               BOOL fFrameWindow) {
  HTMLSITE_VIEW(E_NOTIMPL)
  return pView->OnResizeBorder(prcBorder, pUIWindow, fFrameWindow);
}
STDMETHODIMP CHtmlControlSite::XDocHostUIHandler::TranslateAccelerator(LPMSG lpMsg,
                                                                       const GUID* pguidCmdGroup,
                                                                       DWORD nCmdID) {
  HTMLSITE_VIEW(S_FALSE)
  return pView->OnTranslateAccelerator(lpMsg, pguidCmdGroup, nCmdID);
}
STDMETHODIMP CHtmlControlSite::XDocHostUIHandler::GetOptionKeyPath(LPOLESTR* pchKey,
                                                                   DWORD dwReserved) {
  HTMLSITE_VIEW(E_NOTIMPL)
  return pView->OnGetOptionKeyPath(pchKey, dwReserved);
}
STDMETHODIMP CHtmlControlSite::XDocHostUIHandler::GetDropTarget(LPDROPTARGET pDropTarget,
                                                                LPDROPTARGET* ppDropTarget) {
  HTMLSITE_VIEW(E_NOTIMPL)
  return pView->OnGetDropTarget(pDropTarget, ppDropTarget);
}
STDMETHODIMP CHtmlControlSite::XDocHostUIHandler::GetExternal(LPDISPATCH* lppDispatch) {
  HTMLSITE_VIEW(E_NOTIMPL)
  return pView->OnGetExternal(lppDispatch);
}
STDMETHODIMP CHtmlControlSite::XDocHostUIHandler::TranslateUrl(DWORD dwTranslate,
                                                               OLECHAR* pchURLIn,
                                                               OLECHAR** ppchURLOut) {
  HTMLSITE_VIEW(S_FALSE)
  return pView->OnTranslateUrl(dwTranslate, pchURLIn, ppchURLOut);
}
STDMETHODIMP CHtmlControlSite::XDocHostUIHandler::FilterDataObject(LPDATAOBJECT pDataObject,
                                                                   LPDATAOBJECT* ppDataObject) {
  HTMLSITE_VIEW(S_FALSE)
  return pView->OnFilterDataObject(pDataObject, ppDataObject);
}

// ---------------------------------------------------------------------
// CHtmlView

IMPLEMENT_DYNCREATE(CHtmlView, CFormView)

BEGIN_MESSAGE_MAP(CHtmlView, CFormView)
  ON_WM_SIZE()
  ON_WM_PAINT()
  ON_WM_ERASEBKGND()
  ON_WM_DESTROY()
  ON_COMMAND(ID_FILE_PRINT, OnFilePrint)
END_MESSAGE_MAP()

BEGIN_EVENTSINK_MAP(CHtmlView, CFormView)
  ON_EVENT(CHtmlView, AFX_IDW_PANE_FIRST, DISPID_STATUSTEXTCHANGE, OnStatusTextChange, VTS_BSTR)
  ON_EVENT(CHtmlView, AFX_IDW_PANE_FIRST, DISPID_PROGRESSCHANGE, OnProgressChange,
           VTS_I4 VTS_I4)
  ON_EVENT(CHtmlView, AFX_IDW_PANE_FIRST, DISPID_COMMANDSTATECHANGE, OnCommandStateChange,
           VTS_I4 VTS_BOOL)
  ON_EVENT(CHtmlView, AFX_IDW_PANE_FIRST, DISPID_DOWNLOADBEGIN, OnDownloadBegin, VTS_NONE)
  ON_EVENT(CHtmlView, AFX_IDW_PANE_FIRST, DISPID_DOWNLOADCOMPLETE, OnDownloadComplete, VTS_NONE)
  ON_EVENT(CHtmlView, AFX_IDW_PANE_FIRST, DISPID_TITLECHANGE, OnTitleChange, VTS_BSTR)
  ON_EVENT(CHtmlView, AFX_IDW_PANE_FIRST, DISPID_PROPERTYCHANGE, OnPropertyChange, VTS_BSTR)
  ON_EVENT(CHtmlView, AFX_IDW_PANE_FIRST, DISPID_NEWWINDOW2, OnNewWindow2,
           VTS_PDISPATCH VTS_PBOOL)
  ON_EVENT(CHtmlView, AFX_IDW_PANE_FIRST, DISPID_BEFORENAVIGATE2, BeforeNavigate2,
           VTS_DISPATCH VTS_PVARIANT VTS_PVARIANT VTS_PVARIANT VTS_PVARIANT VTS_PVARIANT VTS_PBOOL)
  ON_EVENT(CHtmlView, AFX_IDW_PANE_FIRST, DISPID_NAVIGATECOMPLETE2, NavigateComplete2,
           VTS_DISPATCH VTS_PVARIANT)
  ON_EVENT(CHtmlView, AFX_IDW_PANE_FIRST, DISPID_DOCUMENTCOMPLETE, DocumentComplete,
           VTS_DISPATCH VTS_PVARIANT)
  ON_EVENT(CHtmlView, AFX_IDW_PANE_FIRST, DISPID_NAVIGATEERROR, NavigateError,
           VTS_DISPATCH VTS_PVARIANT VTS_PVARIANT VTS_PVARIANT VTS_PBOOL)
  ON_EVENT(CHtmlView, AFX_IDW_PANE_FIRST, DISPID_ONQUIT, OnQuit, VTS_NONE)
  ON_EVENT(CHtmlView, AFX_IDW_PANE_FIRST, DISPID_ONVISIBLE, OnVisible, VTS_BOOL)
  ON_EVENT(CHtmlView, AFX_IDW_PANE_FIRST, DISPID_ONTOOLBAR, OnToolBar, VTS_BOOL)
  ON_EVENT(CHtmlView, AFX_IDW_PANE_FIRST, DISPID_ONMENUBAR, OnMenuBar, VTS_BOOL)
  ON_EVENT(CHtmlView, AFX_IDW_PANE_FIRST, DISPID_ONSTATUSBAR, OnStatusBar, VTS_BOOL)
  ON_EVENT(CHtmlView, AFX_IDW_PANE_FIRST, DISPID_ONFULLSCREEN, OnFullScreen, VTS_BOOL)
  ON_EVENT(CHtmlView, AFX_IDW_PANE_FIRST, DISPID_ONTHEATERMODE, OnTheaterMode, VTS_BOOL)
END_EVENTSINK_MAP()

CHtmlView::CHtmlView() : CFormView((LPCTSTR)NULL), m_pBrowserApp(NULL) {}

CHtmlView::~CHtmlView() {
  if (m_pBrowserApp != NULL) m_pBrowserApp->Release();
}

BOOL CHtmlView::Create(LPCTSTR lpszClassName, LPCTSTR lpszWindowName, DWORD dwStyle,
                       const RECT& rect, CWnd* pParentWnd, UINT nID, CCreateContext* pContext) {
  // An ordinary window, not a form's dialog; CFormView::OnCreate passes
  // the context on.
  m_pCreateContext = pContext;
  BOOL bMade = CView::Create(lpszClassName, lpszWindowName, dwStyle, rect, pParentWnd, nID,
                             pContext);
  m_pCreateContext = NULL;
  if (!bMade) return FALSE;

  CRect rectClient;
  GetClientRect(&rectClient);
  if (!m_wndBrowser.CreateControl(CLSID_WebBrowser, lpszWindowName, WS_VISIBLE | WS_CHILD,
                                  rectClient, this, AFX_IDW_PANE_FIRST)) {
    DestroyWindow();
    return FALSE;
  }
  LPUNKNOWN pUnk = m_wndBrowser.GetControlUnknown();
  if (pUnk == NULL ||
      FAILED(pUnk->QueryInterface(IID_IWebBrowser2, (void**)&m_pBrowserApp))) {
    m_pBrowserApp = NULL;
    m_wndBrowser.DestroyWindow();
    DestroyWindow();
    return FALSE;
  }
  return TRUE;
}

BOOL CHtmlView::CreateControlSite(COleControlContainer* pContainer, COleControlSite** ppSite,
                                  UINT, REFCLSID) {
  if (ppSite == NULL) return FALSE;
  *ppSite = new CHtmlControlSite(pContainer);
  return TRUE;
}

void CHtmlView::OnDestroy() {
  if (m_pBrowserApp != NULL) {
    m_pBrowserApp->Release();
    m_pBrowserApp = NULL;
  }
  CFormView::OnDestroy();
}

void CHtmlView::OnSize(UINT nType, int cx, int cy) {
  CFormView::OnSize(nType, cx, cy);
  if (::IsWindow(m_wndBrowser.m_hWnd)) {
    // The browser fills the view.
    CRect rect;
    GetClientRect(&rect);
    m_wndBrowser.SetWindowPos(NULL, rect.left, rect.top, rect.Width(), rect.Height(),
                              SWP_NOACTIVATE | SWP_NOZORDER);
  }
}

// The browser draws the page; the view has nothing of its own to paint.
void CHtmlView::OnPaint() { Default(); }
BOOL CHtmlView::OnEraseBkgnd(CDC*) { return TRUE; }
void CHtmlView::OnDraw(CDC*) {}

BOOL CHtmlView::PreTranslateMessage(MSG* pMsg) {
  // The page gets its keys first: tab between its links, its shortcuts.
  if (m_pBrowserApp != NULL && pMsg->message >= WM_KEYFIRST && pMsg->message <= WM_KEYLAST &&
      (pMsg->hwnd == m_hWnd || ::IsChild(m_hWnd, pMsg->hwnd))) {
    IOleInPlaceActiveObject* pActive = NULL;
    if (SUCCEEDED(m_pBrowserApp->QueryInterface(IID_IOleInPlaceActiveObject,
                                                (void**)&pActive))) {
      HRESULT hr = pActive->TranslateAccelerator(pMsg);
      pActive->Release();
      if (hr == S_OK) return TRUE;
    }
  }
  return CFormView::PreTranslateMessage(pMsg);
}

void CHtmlView::OnFilePrint() {
  if (m_pBrowserApp == NULL) return;
  m_pBrowserApp->ExecWB(OLECMDID_PRINT, OLECMDEXECOPT_DODEFAULT, NULL, NULL);
}

// ----- Properties

#define HTMLVIEW_GET_BOOL(name)                         \
  BOOL CHtmlView::Get##name() const {                   \
    VARIANT_BOOL b = VARIANT_FALSE;                     \
    if (m_pBrowserApp != NULL) m_pBrowserApp->get_##name(&b); \
    return b != VARIANT_FALSE;                          \
  }
#define HTMLVIEW_SET_BOOL(name)                                         \
  void CHtmlView::Set##name(BOOL bNewValue) {                           \
    if (m_pBrowserApp != NULL)                                          \
      m_pBrowserApp->put_##name(bNewValue ? VARIANT_TRUE : VARIANT_FALSE); \
  }
#define HTMLVIEW_GET_LONG(name)                         \
  LONG CHtmlView::Get##name() const {                   \
    LONG n = 0;                                         \
    if (m_pBrowserApp != NULL) m_pBrowserApp->get_##name(&n); \
    return (LONG)n;                                     \
  }
#define HTMLVIEW_SET_LONG(name)                                             \
  void CHtmlView::Set##name(LONG nNewValue) {                               \
    if (m_pBrowserApp != NULL) m_pBrowserApp->put_##name(nNewValue);  \
  }
#define HTMLVIEW_GET_STRING(fn, prop)                   \
  CString CHtmlView::fn() const {                       \
    BSTR bstr = NULL;                                   \
    if (m_pBrowserApp != NULL) m_pBrowserApp->get_##prop(&bstr); \
    CString str = StringFromBSTR(bstr);                 \
    ::SysFreeString(bstr);                              \
    return str;                                         \
  }
#define HTMLVIEW_GET_DISPATCH(fn, prop)                 \
  LPDISPATCH CHtmlView::fn() const {                    \
    LPDISPATCH pDisp = NULL;                            \
    if (m_pBrowserApp != NULL) m_pBrowserApp->get_##prop(&pDisp); \
    return pDisp;                                       \
  }

HTMLVIEW_GET_STRING(GetType, Type)
HTMLVIEW_GET_STRING(GetLocationName, LocationName)
HTMLVIEW_GET_STRING(GetLocationURL, LocationURL)
HTMLVIEW_GET_STRING(GetFullName, FullName)
HTMLVIEW_GET_LONG(Left)
HTMLVIEW_SET_LONG(Left)
HTMLVIEW_GET_LONG(Top)
HTMLVIEW_SET_LONG(Top)
HTMLVIEW_GET_LONG(Height)
HTMLVIEW_SET_LONG(Height)
HTMLVIEW_GET_LONG(Width)
HTMLVIEW_SET_LONG(Width)
HTMLVIEW_GET_BOOL(Visible)
HTMLVIEW_SET_BOOL(Visible)
HTMLVIEW_GET_BOOL(Offline)
HTMLVIEW_SET_BOOL(Offline)
HTMLVIEW_GET_BOOL(Silent)
HTMLVIEW_SET_BOOL(Silent)
HTMLVIEW_GET_BOOL(TopLevelContainer)
HTMLVIEW_GET_BOOL(Busy)
HTMLVIEW_GET_BOOL(MenuBar)
HTMLVIEW_SET_BOOL(MenuBar)
HTMLVIEW_GET_BOOL(FullScreen)
HTMLVIEW_SET_BOOL(FullScreen)
HTMLVIEW_GET_BOOL(StatusBar)
HTMLVIEW_SET_BOOL(StatusBar)
HTMLVIEW_GET_BOOL(RegisterAsBrowser)
HTMLVIEW_SET_BOOL(RegisterAsBrowser)
HTMLVIEW_GET_BOOL(RegisterAsDropTarget)
HTMLVIEW_SET_BOOL(RegisterAsDropTarget)
HTMLVIEW_GET_BOOL(TheaterMode)
HTMLVIEW_SET_BOOL(TheaterMode)
HTMLVIEW_GET_BOOL(AddressBar)
HTMLVIEW_SET_BOOL(AddressBar)
HTMLVIEW_GET_DISPATCH(GetApplication, Application)
HTMLVIEW_GET_DISPATCH(GetParentBrowser, Parent)
HTMLVIEW_GET_DISPATCH(GetContainer, Container)
HTMLVIEW_GET_DISPATCH(GetHtmlDocument, Document)

READYSTATE CHtmlView::GetReadyState() const {
  READYSTATE state = READYSTATE_UNINITIALIZED;
  if (m_pBrowserApp != NULL) m_pBrowserApp->get_ReadyState(&state);
  return state;
}

int CHtmlView::GetToolBar() const {
  int n = 0;
  if (m_pBrowserApp != NULL) m_pBrowserApp->get_ToolBar(&n);
  return n;
}

void CHtmlView::SetToolBar(int nNewValue) {
  if (m_pBrowserApp != NULL) m_pBrowserApp->put_ToolBar(nNewValue);
}

OLECMDF CHtmlView::QueryStatusWB(OLECMDID cmdID) const {
  OLECMDF flags = (OLECMDF)0;
  if (m_pBrowserApp != NULL) m_pBrowserApp->QueryStatusWB(cmdID, &flags);
  return flags;
}

BOOL CHtmlView::GetProperty(LPCTSTR lpszProperty, CString& strValue) {
  strValue.Empty();
  if (m_pBrowserApp == NULL) return FALSE;
  BSTR bstrName = ::SysAllocString(CStringW(lpszProperty));
  VARIANT var;
  ::VariantInit(&var);
  HRESULT hr = m_pBrowserApp->GetProperty(bstrName, &var);
  ::SysFreeString(bstrName);
  if (FAILED(hr)) return FALSE;
  BOOL bOK = var.vt != VT_EMPTY;
  strValue = StringFromVariant(&var);
  ::VariantClear(&var);
  return bOK;
}

COleVariant CHtmlView::GetProperty(LPCTSTR lpszProperty) {
  COleVariant result;
  if (m_pBrowserApp == NULL) return result;
  BSTR bstrName = ::SysAllocString(CStringW(lpszProperty));
  m_pBrowserApp->GetProperty(bstrName, &result);
  ::SysFreeString(bstrName);
  return result;
}

void CHtmlView::PutProperty(LPCTSTR lpszProperty, const VARIANT& vtValue) {
  if (m_pBrowserApp == NULL) return;
  BSTR bstrName = ::SysAllocString(CStringW(lpszProperty));
  m_pBrowserApp->PutProperty(bstrName, vtValue);
  ::SysFreeString(bstrName);
}

void CHtmlView::PutProperty(LPCTSTR lpszPropertyName, double dValue) {
  PutProperty(lpszPropertyName, (const VARIANT&)COleVariant(dValue));
}

void CHtmlView::PutProperty(LPCTSTR lpszPropertyName, LPCTSTR lpszValue) {
  PutProperty(lpszPropertyName, (const VARIANT&)COleVariant(lpszValue, VT_BSTR));
}

void CHtmlView::PutProperty(LPCTSTR lpszPropertyName, long lValue) {
  COleVariant var;
  var.vt = VT_I4;
  var.lVal = lValue;
  PutProperty(lpszPropertyName, (const VARIANT&)var);
}

void CHtmlView::PutProperty(LPCTSTR lpszPropertyName, short nValue) {
  COleVariant var;
  var.vt = VT_I2;
  var.iVal = nValue;
  PutProperty(lpszPropertyName, (const VARIANT&)var);
}

BOOL CHtmlView::GetSource(CString& refString) {
  refString.Empty();
  LPDISPATCH pDisp = GetHtmlDocument();
  if (pDisp == NULL) return FALSE;
  BOOL bOK = SUCCEEDED(_AfxHtmlEditGetHTML(pDisp, refString));
  pDisp->Release();
  return bOK;
}

// ----- Navigation

void CHtmlView::GoBack() {
  if (m_pBrowserApp != NULL) m_pBrowserApp->GoBack();
}
void CHtmlView::GoForward() {
  if (m_pBrowserApp != NULL) m_pBrowserApp->GoForward();
}
void CHtmlView::GoHome() {
  if (m_pBrowserApp != NULL) m_pBrowserApp->GoHome();
}
void CHtmlView::GoSearch() {
  if (m_pBrowserApp != NULL) m_pBrowserApp->GoSearch();
}
void CHtmlView::Refresh() {
  if (m_pBrowserApp != NULL) m_pBrowserApp->Refresh();
}
void CHtmlView::Refresh2(int nLevel) {
  if (m_pBrowserApp == NULL) return;
  VARIANT var;
  ::VariantInit(&var);
  var.vt = VT_I4;
  var.lVal = nLevel;
  m_pBrowserApp->Refresh2(&var);
}
void CHtmlView::Stop() {
  if (m_pBrowserApp != NULL) m_pBrowserApp->Stop();
}
void CHtmlView::ExecWB(OLECMDID cmdID, OLECMDEXECOPT cmdexecopt, VARIANT* pvaIn,
                       VARIANT* pvaOut) {
  if (m_pBrowserApp != NULL) m_pBrowserApp->ExecWB(cmdID, cmdexecopt, pvaIn, pvaOut);
}

void CHtmlView::Navigate(LPCTSTR URL, DWORD dwFlags, LPCTSTR lpszTargetFrameName,
                         LPCTSTR lpszHeaders, LPVOID lpvPostData, DWORD dwPostDataLen) {
  if (m_pBrowserApp == NULL) return;
  BSTR bstrURL = ::SysAllocString(CStringW(URL));
  VARIANT varFlags, varTarget, varPost, varHeaders;
  ::VariantInit(&varFlags);
  varFlags.vt = VT_I4;
  varFlags.lVal = (LONG)dwFlags;
  InitStringVariant(&varTarget, lpszTargetFrameName);
  InitBytesVariant(&varPost, lpvPostData, dwPostDataLen);
  InitStringVariant(&varHeaders, lpszHeaders);
  m_pBrowserApp->Navigate(bstrURL, &varFlags, &varTarget, &varPost, &varHeaders);
  ::SysFreeString(bstrURL);
  ::VariantClear(&varTarget);
  ::VariantClear(&varPost);
  ::VariantClear(&varHeaders);
}

void CHtmlView::Navigate2(LPCTSTR lpszURL, DWORD dwFlags, LPCTSTR lpszTargetFrameName,
                          LPCTSTR lpszHeaders, LPVOID lpvPostData, DWORD dwPostDataLen) {
  if (m_pBrowserApp == NULL) return;
  VARIANT varURL, varFlags, varTarget, varPost, varHeaders;
  InitStringVariant(&varURL, lpszURL != NULL ? lpszURL : _T(""));
  ::VariantInit(&varFlags);
  varFlags.vt = VT_I4;
  varFlags.lVal = (LONG)dwFlags;
  InitStringVariant(&varTarget, lpszTargetFrameName);
  InitBytesVariant(&varPost, lpvPostData, dwPostDataLen);
  InitStringVariant(&varHeaders, lpszHeaders);
  m_pBrowserApp->Navigate2(&varURL, &varFlags, &varTarget, &varPost, &varHeaders);
  ::VariantClear(&varURL);
  ::VariantClear(&varTarget);
  ::VariantClear(&varPost);
  ::VariantClear(&varHeaders);
}

void CHtmlView::Navigate2(LPCTSTR lpszURL, DWORD dwFlags, CByteArray& baPostedData,
                          LPCTSTR lpszTargetFrameName, LPCTSTR lpszHeader) {
  Navigate2(lpszURL, dwFlags, lpszTargetFrameName, lpszHeader,
            baPostedData.GetSize() > 0 ? baPostedData.GetData() : NULL,
            (DWORD)baPostedData.GetSize());
}

void CHtmlView::Navigate2(LPITEMIDLIST pIDL, DWORD dwFlags, LPCTSTR lpszTargetFrameName) {
  if (m_pBrowserApp == NULL || pIDL == NULL) return;
  // The item list goes as its bytes, with the zero terminator.
  UINT cb = sizeof(USHORT);
  for (const BYTE* p = (const BYTE*)pIDL; ((const SHITEMID*)p)->cb != 0;
       p += ((const SHITEMID*)p)->cb)
    cb += ((const SHITEMID*)p)->cb;
  VARIANT varURL, varFlags, varTarget, varEmpty;
  InitBytesVariant(&varURL, pIDL, cb);
  ::VariantInit(&varFlags);
  varFlags.vt = VT_I4;
  varFlags.lVal = (LONG)dwFlags;
  InitStringVariant(&varTarget, lpszTargetFrameName);
  ::VariantInit(&varEmpty);
  m_pBrowserApp->Navigate2(&varURL, &varFlags, &varTarget, &varEmpty, &varEmpty);
  ::VariantClear(&varURL);
  ::VariantClear(&varTarget);
}

BOOL CHtmlView::LoadFromResource(LPCTSTR lpszResource) {
  // res://<module>/<resource>, the browser's own way into a module's
  // resources.
  HINSTANCE hInst = AfxGetResourceHandle();
  TCHAR szModule[MAX_PATH];
  if (::GetModuleFileName(hInst, szModule, MAX_PATH) == 0) return FALSE;
  CString strURL;
  if (IS_INTRESOURCE(lpszResource))
    strURL.Format(_T("res://%s/%u"), szModule, (UINT)(UINT_PTR)lpszResource);
  else
    strURL.Format(_T("res://%s/%s"), szModule, lpszResource);
  Navigate(strURL, 0, NULL, NULL, NULL, 0);
  return m_pBrowserApp != NULL;
}

BOOL CHtmlView::LoadFromResource(UINT nRes) {
  return LoadFromResource(MAKEINTRESOURCE(nRes));
}

// ----- Forms commands

HRESULT CHtmlView::QueryFormsCommand(DWORD dwCommandID, BOOL* pbSupported, BOOL* pbEnabled,
                                     BOOL* pbChecked) {
  if (pbSupported != NULL) *pbSupported = FALSE;
  if (pbEnabled != NULL) *pbEnabled = FALSE;
  if (pbChecked != NULL) *pbChecked = FALSE;
  LPDISPATCH pDisp = GetHtmlDocument();
  if (pDisp == NULL) return E_FAIL;
  IOleCommandTarget* pTarget = NULL;
  HRESULT hr = pDisp->QueryInterface(IID_IOleCommandTarget, (void**)&pTarget);
  if (SUCCEEDED(hr)) {
    OLECMD cmd = {dwCommandID, 0};
    hr = pTarget->QueryStatus(&kCmdSetForms3, 1, &cmd, NULL);
    if (SUCCEEDED(hr)) {
      if (pbSupported != NULL) *pbSupported = (cmd.cmdf & OLECMDF_SUPPORTED) != 0;
      if (pbEnabled != NULL) *pbEnabled = (cmd.cmdf & OLECMDF_ENABLED) != 0;
      if (pbChecked != NULL) *pbChecked = (cmd.cmdf & OLECMDF_LATCHED) != 0;
    }
    pTarget->Release();
  }
  pDisp->Release();
  return hr;
}

HRESULT CHtmlView::ExecFormsCommand(DWORD dwCommandID, VARIANT* pVarIn, VARIANT* pVarOut) {
  LPDISPATCH pDisp = GetHtmlDocument();
  if (pDisp == NULL) return E_FAIL;
  IOleCommandTarget* pTarget = NULL;
  HRESULT hr = pDisp->QueryInterface(IID_IOleCommandTarget, (void**)&pTarget);
  if (SUCCEEDED(hr)) {
    hr = pTarget->Exec(&kCmdSetForms3, dwCommandID, OLECMDEXECOPT_DONTPROMPTUSER, pVarIn,
                       pVarOut);
    pTarget->Release();
  }
  pDisp->Release();
  return hr;
}

// ----- Events

void CHtmlView::BeforeNavigate2(LPDISPATCH, VARIANT* URL, VARIANT* Flags,
                                VARIANT* TargetFrameName, VARIANT* PostData, VARIANT* Headers,
                                BOOL* Cancel) {
  CString strURL = StringFromVariant(URL);
  CString strTarget = StringFromVariant(TargetFrameName);
  CString strHeaders = StringFromVariant(Headers);
  DWORD nFlags = 0;
  if (Flags != NULL) {
    VARIANT var;
    ::VariantInit(&var);
    if (SUCCEEDED(::VariantChangeType(&var, Flags, 0, VT_I4))) nFlags = (DWORD)var.lVal;
  }
  // The posted data is a byte array, maybe inside a by-reference VARIANT.
  CByteArray baPostedData;
  VARIANT* pPost = PostData;
  while (pPost != NULL && pPost->vt == (VT_BYREF | VT_VARIANT)) pPost = pPost->pvarVal;
  if (pPost != NULL && (pPost->vt & VT_ARRAY) && (pPost->vt & VT_TYPEMASK) == VT_UI1) {
    SAFEARRAY* psa = (pPost->vt & VT_BYREF) ? *pPost->pparray : pPost->parray;
    LONG nLow = 0, nHigh = -1;
    if (psa != NULL && SUCCEEDED(::SafeArrayGetLBound(psa, 1, &nLow)) &&
        SUCCEEDED(::SafeArrayGetUBound(psa, 1, &nHigh)) && nHigh >= nLow) {
      void* pv = NULL;
      if (SUCCEEDED(::SafeArrayAccessData(psa, &pv))) {
        baPostedData.SetSize(nHigh - nLow + 1);
        memcpy(baPostedData.GetData(), pv, (size_t)(nHigh - nLow + 1));
        ::SafeArrayUnaccessData(psa);
      }
    }
  }
  OnBeforeNavigate2(strURL, nFlags, strTarget, baPostedData, strHeaders, Cancel);
}

void CHtmlView::NavigateComplete2(LPDISPATCH, VARIANT* URL) {
  OnNavigateComplete2(StringFromVariant(URL));
}

void CHtmlView::DocumentComplete(LPDISPATCH, VARIANT* URL) {
  OnDocumentComplete(StringFromVariant(URL));
}

void CHtmlView::NavigateError(LPDISPATCH, VARIANT* pvURL, VARIANT* pvFrame,
                              VARIANT* pvStatusCode, BOOL* pvbCancel) {
  DWORD dwError = 0;
  if (pvStatusCode != NULL) {
    VARIANT var;
    ::VariantInit(&var);
    if (SUCCEEDED(::VariantChangeType(&var, pvStatusCode, 0, VT_I4))) dwError = (DWORD)var.lVal;
  }
  OnNavigateError(StringFromVariant(pvURL), StringFromVariant(pvFrame), dwError, pvbCancel);
}

void CHtmlView::OnNavigateComplete2(LPCTSTR) {}
void CHtmlView::OnBeforeNavigate2(LPCTSTR, DWORD, LPCTSTR, CByteArray&, LPCTSTR, BOOL*) {}
void CHtmlView::OnStatusTextChange(LPCTSTR lpszText) {
  // The browser's status text goes on the frame's status bar.
  CFrameWnd* pFrame = GetParentFrame();
  if (pFrame != NULL) pFrame->SetMessageText(lpszText);
}
void CHtmlView::OnProgressChange(long, long) {}
void CHtmlView::OnCommandStateChange(long, BOOL) {}
void CHtmlView::OnDownloadBegin() {}
void CHtmlView::OnDownloadComplete() {}
void CHtmlView::OnTitleChange(LPCTSTR) {}
void CHtmlView::OnPropertyChange(LPCTSTR) {}
void CHtmlView::OnNewWindow2(LPDISPATCH*, BOOL*) {}
void CHtmlView::OnDocumentComplete(LPCTSTR) {}
void CHtmlView::OnQuit() {}
void CHtmlView::OnVisible(BOOL) {}
void CHtmlView::OnToolBar(BOOL) {}
void CHtmlView::OnMenuBar(BOOL) {}
void CHtmlView::OnStatusBar(BOOL) {}
void CHtmlView::OnFullScreen(BOOL) {}
void CHtmlView::OnTheaterMode(BOOL) {}
void CHtmlView::OnNavigateError(LPCTSTR, LPCTSTR, DWORD, BOOL*) {}

// ----- The browser's questions; the defaults let the browser do its own
// thing.

HRESULT CHtmlView::OnShowContextMenu(DWORD, LPPOINT, LPUNKNOWN, LPDISPATCH) { return S_FALSE; }
HRESULT CHtmlView::OnGetHostInfo(DOCHOSTUIINFO*) { return E_NOTIMPL; }
HRESULT CHtmlView::OnShowUI(DWORD, LPOLEINPLACEACTIVEOBJECT, LPOLECOMMANDTARGET,
                            LPOLEINPLACEFRAME, LPOLEINPLACEUIWINDOW) {
  return S_FALSE;
}
HRESULT CHtmlView::OnHideUI() { return E_NOTIMPL; }
HRESULT CHtmlView::OnUpdateUI() { return E_NOTIMPL; }
HRESULT CHtmlView::OnEnableModeless(BOOL) { return E_NOTIMPL; }
HRESULT CHtmlView::OnDocWindowActivate(BOOL) { return E_NOTIMPL; }
HRESULT CHtmlView::OnFrameWindowActivate(BOOL) { return E_NOTIMPL; }
HRESULT CHtmlView::OnResizeBorder(LPCRECT, LPOLEINPLACEUIWINDOW, BOOL) { return E_NOTIMPL; }
HRESULT CHtmlView::OnTranslateAccelerator(LPMSG, const GUID*, DWORD) { return S_FALSE; }
HRESULT CHtmlView::OnGetOptionKeyPath(LPOLESTR*, DWORD) { return E_NOTIMPL; }
HRESULT CHtmlView::OnGetDropTarget(LPDROPTARGET, LPDROPTARGET*) { return E_NOTIMPL; }
HRESULT CHtmlView::OnGetExternal(LPDISPATCH* lppDispatch) {
  if (lppDispatch != NULL) *lppDispatch = NULL;
  return E_NOTIMPL;
}
HRESULT CHtmlView::OnTranslateUrl(DWORD, OLECHAR*, OLECHAR** ppchURLOut) {
  if (ppchURLOut != NULL) *ppchURLOut = NULL;
  return S_FALSE;
}
HRESULT CHtmlView::OnFilterDataObject(LPDATAOBJECT, LPDATAOBJECT* ppDataObject) {
  if (ppDataObject != NULL) *ppDataObject = NULL;
  return S_FALSE;
}
