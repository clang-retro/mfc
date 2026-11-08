// htmledit.cpp - HTML editing: the helpers of CHtmlEditCtrlBase (MSHTML's
// editing commands, through the document's IOleCommandTarget),
// CHtmlEditCtrl, CHtmlEditView with its editing command map, and
// CHtmlEditDoc.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#include "stdafx.h"
#include "afxhtml.h"

// MSHTML's command group (CGID_MSHTML), not in every import library.
static const GUID kCGID_MSHTML = {
    0xde4ba900, 0x59ca, 0x11cf, {0x95, 0x92, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00}};

// ---------------------------------------------------------------------
// The helpers

HRESULT AFXAPI _AfxHtmlEditExec(IHTMLDocument2* pDoc, const GUID* pGuid, long cmdID,
                                long cmdExecOpt, VARIANT* pInVar, VARIANT* pOutVar) {
  if (pDoc == NULL) return E_NOINTERFACE;
  IOleCommandTarget* pTarget = NULL;
  HRESULT hr = pDoc->QueryInterface(IID_IOleCommandTarget, (void**)&pTarget);
  if (FAILED(hr)) return hr;
  hr = pTarget->Exec(pGuid != NULL ? pGuid : &kCGID_MSHTML, (DWORD)cmdID, (DWORD)cmdExecOpt,
                     pInVar, pOutVar);
  pTarget->Release();
  return hr;
}

long AFXAPI _AfxHtmlEditQueryStatus(IHTMLDocument2* pDoc, long cmdID) {
  if (pDoc == NULL) return 0;
  IOleCommandTarget* pTarget = NULL;
  if (FAILED(pDoc->QueryInterface(IID_IOleCommandTarget, (void**)&pTarget))) return 0;
  OLECMD cmd = {(ULONG)cmdID, 0};
  long nFlags = SUCCEEDED(pTarget->QueryStatus(&kCGID_MSHTML, 1, &cmd, NULL)) ? (long)cmd.cmdf : 0;
  pTarget->Release();
  return nFlags;
}

// A command with a string: without one, MSHTML asks the user.
HRESULT AFXAPI _AfxHtmlEditExecText(IHTMLDocument2* pDoc, long cmdID, LPCTSTR lpsz) {
  if (lpsz == NULL) return _AfxHtmlEditExec(pDoc, NULL, cmdID, OLECMDEXECOPT_DODEFAULT, NULL, NULL);
  VARIANT var;
  var.vt = VT_BSTR;
  var.bstrVal = ::SysAllocString(CStringW(lpsz));
  HRESULT hr = _AfxHtmlEditExec(pDoc, NULL, cmdID, OLECMDEXECOPT_DONTPROMPTUSER, &var, NULL);
  ::VariantClear(&var);
  return hr;
}

HRESULT AFXAPI _AfxHtmlEditExecBool(IHTMLDocument2* pDoc, long cmdID, bool b) {
  VARIANT var;
  var.vt = VT_BOOL;
  var.boolVal = b ? VARIANT_TRUE : VARIANT_FALSE;
  return _AfxHtmlEditExec(pDoc, NULL, cmdID, OLECMDEXECOPT_DONTPROMPTUSER, &var, NULL);
}

HRESULT AFXAPI _AfxHtmlEditExecInt(IHTMLDocument2* pDoc, long cmdID, long n) {
  VARIANT var;
  var.vt = VT_I4;
  var.lVal = n;
  return _AfxHtmlEditExec(pDoc, NULL, cmdID, OLECMDEXECOPT_DONTPROMPTUSER, &var, NULL);
}

// A command's current value, as text or a number.
static HRESULT GetValue(IHTMLDocument2* pDoc, long cmdID, VARTYPE vt, VARIANT* pVar) {
  ::VariantInit(pVar);
  HRESULT hr = _AfxHtmlEditExec(pDoc, NULL, cmdID, OLECMDEXECOPT_DONTPROMPTUSER, NULL, pVar);
  if (FAILED(hr)) return hr;
  if (pVar->vt == VT_NULL || pVar->vt == VT_EMPTY) return S_FALSE;  // a mixed selection
  return ::VariantChangeType(pVar, pVar, 0, vt);
}

HRESULT AFXAPI _AfxHtmlEditGetText(IHTMLDocument2* pDoc, long cmdID, CString& str) {
  str.Empty();
  VARIANT var;
  HRESULT hr = GetValue(pDoc, cmdID, VT_BSTR, &var);
  if (hr == S_OK && var.bstrVal != NULL) str = var.bstrVal;
  ::VariantClear(&var);
  return hr;
}

HRESULT AFXAPI _AfxHtmlEditGetInt(IHTMLDocument2* pDoc, long cmdID, long& n) {
  n = 0;
  VARIANT var;
  HRESULT hr = GetValue(pDoc, cmdID, VT_I4, &var);
  if (hr == S_OK) n = var.lVal;
  ::VariantClear(&var);
  return hr;
}

HRESULT AFXAPI _AfxHtmlEditGetBlockFormatNames(IHTMLDocument2* pDoc, CStringArray& sa) {
  sa.RemoveAll();
  VARIANT var;
  ::VariantInit(&var);
  HRESULT hr = _AfxHtmlEditExec(pDoc, NULL, IDM_GETBLOCKFMTS, OLECMDEXECOPT_DONTPROMPTUSER, NULL,
                                &var);
  if (FAILED(hr)) return hr;
  if ((var.vt & VT_ARRAY) != 0 && var.parray != NULL &&
      ::SafeArrayGetDim(var.parray) == 1) {
    LONG lo = 0, hi = -1;
    ::SafeArrayGetLBound(var.parray, 1, &lo);
    ::SafeArrayGetUBound(var.parray, 1, &hi);
    for (LONG i = lo; i <= hi; i++) {
      if ((var.vt & VT_TYPEMASK) == VT_BSTR) {
        BSTR bstr = NULL;
        if (SUCCEEDED(::SafeArrayGetElement(var.parray, &i, &bstr))) {
          sa.Add(CString(bstr != NULL ? bstr : L""));
          ::SysFreeString(bstr);
        }
      } else if ((var.vt & VT_TYPEMASK) == VT_VARIANT) {
        VARIANT v;
        ::VariantInit(&v);
        if (SUCCEEDED(::SafeArrayGetElement(var.parray, &i, &v)) &&
            SUCCEEDED(::VariantChangeType(&v, &v, 0, VT_BSTR)))
          sa.Add(CString(v.bstrVal != NULL ? v.bstrVal : L""));
        ::VariantClear(&v);
      }
    }
  } else {
    hr = E_UNEXPECTED;
  }
  ::VariantClear(&var);
  return hr;
}

BOOL AFXAPI _AfxHtmlEditSetDesignMode(IHTMLDocument2* pDoc, BOOL bMode) {
  if (pDoc == NULL) return FALSE;
  BSTR bstr = ::SysAllocString(bMode ? L"On" : L"Off");
  HRESULT hr = pDoc->put_designMode(bstr);
  ::SysFreeString(bstr);
  return SUCCEEDED(hr);
}

// The page's text, as its IPersistStreamInit saves it: UTF-16 with a
// byte order mark, UTF-8, or the ANSI code page.
HRESULT AFXAPI _AfxHtmlEditGetHTML(IUnknown* pDoc, CString& str) {
  str.Empty();
  if (pDoc == NULL) return E_NOINTERFACE;
  IPersistStreamInit* pPersist = NULL;
  HRESULT hr = pDoc->QueryInterface(IID_IPersistStreamInit, (void**)&pPersist);
  if (FAILED(hr)) return hr;
  IStream* pStream = NULL;
  hr = ::CreateStreamOnHGlobal(NULL, TRUE, &pStream);
  if (SUCCEEDED(hr)) {
    hr = pPersist->Save(pStream, FALSE);
    if (SUCCEEDED(hr)) {
      HGLOBAL hGlobal = NULL;
      ::GetHGlobalFromStream(pStream, &hGlobal);
      STATSTG stat;
      memset(&stat, 0, sizeof(stat));
      pStream->Stat(&stat, STATFLAG_NONAME);
      const BYTE* pb = (const BYTE*)::GlobalLock(hGlobal);
      int cb = (int)stat.cbSize.QuadPart;
      if (pb != NULL) {
        if (cb >= 2 && pb[0] == 0xFF && pb[1] == 0xFE) {
          str = CStringW((LPCWSTR)(pb + 2), (cb - 2) / 2);
        } else {
          if (cb >= 3 && pb[0] == 0xEF && pb[1] == 0xBB && pb[2] == 0xBF) {
            pb += 3;
            cb -= 3;
          }
          UINT cp = cb == 0 || ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, (LPCSTR)pb,
                                                     cb, NULL, 0) > 0
                        ? CP_UTF8
                        : CP_ACP;
          int cch = ::MultiByteToWideChar(cp, 0, (LPCSTR)pb, cb, NULL, 0);
          CStringW strW;
          if (cch > 0) {
            ::MultiByteToWideChar(cp, 0, (LPCSTR)pb, cb, strW.GetBuffer(cch), cch);
            strW.ReleaseBuffer(cch);
          }
          str = strW;
        }
        ::GlobalUnlock(hGlobal);
      } else {
        hr = E_OUTOFMEMORY;
      }
    }
    pStream->Release();
  }
  pPersist->Release();
  return hr;
}

// A new page from text: UTF-16 with its byte order mark.
HRESULT AFXAPI _AfxHtmlEditSetHTML(IUnknown* pDoc, LPCTSTR lpszHTML) {
  if (pDoc == NULL) return E_NOINTERFACE;
  IPersistStreamInit* pPersist = NULL;
  HRESULT hr = pDoc->QueryInterface(IID_IPersistStreamInit, (void**)&pPersist);
  if (FAILED(hr)) return hr;
  CStringW strW(lpszHTML != NULL ? lpszHTML : _T(""));
  SIZE_T cb = 2 + (SIZE_T)strW.GetLength() * sizeof(WCHAR);
  HGLOBAL hGlobal = ::GlobalAlloc(GMEM_MOVEABLE, cb);
  if (hGlobal == NULL) {
    pPersist->Release();
    return E_OUTOFMEMORY;
  }
  BYTE* pb = (BYTE*)::GlobalLock(hGlobal);
  pb[0] = 0xFF;
  pb[1] = 0xFE;
  memcpy(pb + 2, (LPCWSTR)strW, cb - 2);
  ::GlobalUnlock(hGlobal);
  IStream* pStream = NULL;
  hr = ::CreateStreamOnHGlobal(hGlobal, TRUE, &pStream);
  if (SUCCEEDED(hr)) {
    pPersist->InitNew();
    hr = pPersist->Load(pStream);
    pStream->Release();
  } else {
    ::GlobalFree(hGlobal);
  }
  pPersist->Release();
  return hr;
}

HRESULT AFXAPI _AfxHtmlEditNewDocument(IHTMLDocument2* pDoc) {
  if (pDoc == NULL) return E_NOINTERFACE;
  IPersistStreamInit* pPersist = NULL;
  HRESULT hr = pDoc->QueryInterface(IID_IPersistStreamInit, (void**)&pPersist);
  if (FAILED(hr)) return hr;
  hr = pPersist->InitNew();
  pPersist->Release();
  return hr;
}

HRESULT AFXAPI _AfxHtmlEditIsDirty(IHTMLDocument2* pDoc) {
  if (pDoc == NULL) return S_FALSE;
  IPersistStreamInit* pPersist = NULL;
  if (FAILED(pDoc->QueryInterface(IID_IPersistStreamInit, (void**)&pPersist))) return S_FALSE;
  HRESULT hr = pPersist->IsDirty();
  pPersist->Release();
  return hr == S_OK ? S_OK : S_FALSE;
}

HRESULT AFXAPI _AfxHtmlEditGetEvent(IHTMLDocument2* pDoc, IHTMLEventObj** ppEventObj) {
  if (ppEventObj == NULL) return E_POINTER;
  *ppEventObj = NULL;
  if (pDoc == NULL) return E_NOINTERFACE;
  IHTMLWindow2* pWindow = NULL;
  HRESULT hr = pDoc->get_parentWindow(&pWindow);
  if (FAILED(hr) || pWindow == NULL) return FAILED(hr) ? hr : E_FAIL;
  hr = pWindow->get_event(ppEventObj);
  pWindow->Release();
  if (SUCCEEDED(hr) && *ppEventObj == NULL) hr = E_FAIL;
  return hr;
}

HRESULT AFXAPI _AfxHtmlEditGetSelectionType(IHTMLDocument2* pDoc, CString& str) {
  str.Empty();
  if (pDoc == NULL) return E_NOINTERFACE;
  IHTMLSelectionObject* pSel = NULL;
  HRESULT hr = pDoc->get_selection(&pSel);
  if (FAILED(hr) || pSel == NULL) return FAILED(hr) ? hr : E_FAIL;
  BSTR bstr = NULL;
  hr = pSel->get_type(&bstr);
  if (SUCCEEDED(hr) && bstr != NULL) str = bstr;
  ::SysFreeString(bstr);
  pSel->Release();
  return hr;
}

// The document of a browser.
static BOOL BrowserDocument(IUnknown* pBrowserUnk, IHTMLDocument2** ppDocument) {
  if (ppDocument == NULL) return FALSE;
  *ppDocument = NULL;
  if (pBrowserUnk == NULL) return FALSE;
  IWebBrowser2* pBrowser = NULL;
  if (FAILED(pBrowserUnk->QueryInterface(IID_IWebBrowser2, (void**)&pBrowser))) return FALSE;
  IDispatch* pDisp = NULL;
  if (SUCCEEDED(pBrowser->get_Document(&pDisp)) && pDisp != NULL) {
    pDisp->QueryInterface(IID_IHTMLDocument2, (void**)ppDocument);
    pDisp->Release();
  }
  pBrowser->Release();
  return *ppDocument != NULL;
}

// ---------------------------------------------------------------------
// CHtmlEditCtrl

// The browser's DocumentComplete, for a CHtmlEditCtrl: the control is
// the window, so the parent's event sink map has its events.
class CHtmlEditBrowserSink : public IDispatch {
public:
  LONG m_nRef;
  CHtmlEditCtrl* m_pCtrl;
  IUnknown* m_pBrowser;  // not held: the control outlives the connection
  CHtmlEditBrowserSink(CHtmlEditCtrl* pCtrl, IUnknown* pBrowser)
      : m_nRef(1), m_pCtrl(pCtrl), m_pBrowser(pBrowser) {}
  virtual ~CHtmlEditBrowserSink() {}

  STDMETHOD(QueryInterface)(REFIID iid, void** ppv) {
    if (iid == IID_IUnknown || iid == IID_IDispatch || iid == DIID_DWebBrowserEvents2) {
      *ppv = static_cast<IDispatch*>(this);
      AddRef();
      return S_OK;
    }
    *ppv = NULL;
    return E_NOINTERFACE;
  }
  STDMETHOD_(ULONG, AddRef)() { return (ULONG)::InterlockedIncrement(&m_nRef); }
  STDMETHOD_(ULONG, Release)() {
    LONG n = ::InterlockedDecrement(&m_nRef);
    if (n == 0) delete this;
    return (ULONG)n;
  }
  STDMETHOD(GetTypeInfoCount)(UINT* pctinfo) {
    if (pctinfo != NULL) *pctinfo = 0;
    return S_OK;
  }
  STDMETHOD(GetTypeInfo)(UINT, LCID, ITypeInfo** pp) {
    if (pp != NULL) *pp = NULL;
    return E_NOTIMPL;
  }
  STDMETHOD(GetIDsOfNames)(REFIID, LPOLESTR*, UINT, LCID, DISPID*) { return E_NOTIMPL; }
  STDMETHOD(Invoke)(DISPID dispid, REFIID, LCID, WORD, DISPPARAMS* pdp, VARIANT*, EXCEPINFO*,
                    UINT*) {
    if (dispid != DISPID_DOCUMENTCOMPLETE || m_pCtrl == NULL) return S_OK;
    // The top-level page only, not a frame.
    if (pdp != NULL && pdp->cArgs >= 1) {
      VARIANT* pArg = &pdp->rgvarg[pdp->cArgs - 1];
      if (pArg->vt == VT_DISPATCH && pArg->pdispVal != NULL) {
        IUnknown* p1 = NULL;
        IUnknown* p2 = NULL;
        pArg->pdispVal->QueryInterface(IID_IUnknown, (void**)&p1);
        m_pBrowser->QueryInterface(IID_IUnknown, (void**)&p2);
        BOOL bTop = p1 == p2;
        if (p1 != NULL) p1->Release();
        if (p2 != NULL) p2->Release();
        if (!bTop) return S_OK;
      }
    }
    m_pCtrl->OnDocumentReady();
    return S_OK;
  }
};

BEGIN_MESSAGE_MAP(CHtmlEditCtrl, CWnd)
END_MESSAGE_MAP()

CHtmlEditCtrl::CHtmlEditCtrl() : m_pBrowserSink(NULL), m_pSinkPoint(NULL), m_dwSinkCookie(0) {}

CHtmlEditCtrl::~CHtmlEditCtrl() { DisconnectBrowser(); }

LPCTSTR CHtmlEditCtrl::GetStartDocument() { return _T("about:blank"); }

BOOL CHtmlEditCtrl::Create(LPCTSTR lpszWindowName, DWORD dwStyle, const RECT& rect,
                           CWnd* pParentWnd, int nID, CCreateContext*) {
  if (!CreateControl(CLSID_WebBrowser, lpszWindowName, dwStyle, rect, pParentWnd, (UINT)nID))
    return FALSE;
  IUnknown* pUnk = GetControlUnknown();
  IWebBrowser2* pBrowser = NULL;
  if (pUnk == NULL || FAILED(pUnk->QueryInterface(IID_IWebBrowser2, (void**)&pBrowser))) {
    DestroyWindow();
    return FALSE;
  }
  // Design mode once the start page is there.
  IConnectionPointContainer* pCPC = NULL;
  if (SUCCEEDED(pUnk->QueryInterface(IID_IConnectionPointContainer, (void**)&pCPC))) {
    IConnectionPoint* pCP = NULL;
    if (SUCCEEDED(pCPC->FindConnectionPoint(DIID_DWebBrowserEvents2, &pCP))) {
      CHtmlEditBrowserSink* pSink = new CHtmlEditBrowserSink(this, pUnk);
      if (SUCCEEDED(pCP->Advise(pSink, &m_dwSinkCookie))) {
        m_pBrowserSink = pSink;
        m_pSinkPoint = pCP;
      } else {
        pSink->Release();
        pCP->Release();
      }
    }
    pCPC->Release();
  }
  VARIANT vURL, vEmpty;
  ::VariantInit(&vEmpty);
  vURL.vt = VT_BSTR;
  vURL.bstrVal = ::SysAllocString(CStringW(GetStartDocument()));
  pBrowser->Navigate2(&vURL, &vEmpty, &vEmpty, &vEmpty, &vEmpty);
  ::VariantClear(&vURL);
  pBrowser->Release();
  return TRUE;
}

void CHtmlEditCtrl::OnDocumentReady() { SetDesignMode(TRUE); }

void CHtmlEditCtrl::DisconnectBrowser() {
  if (m_pBrowserSink == NULL) return;
  ((CHtmlEditBrowserSink*)m_pBrowserSink)->m_pCtrl = NULL;
  m_pSinkPoint->Unadvise(m_dwSinkCookie);
  m_pSinkPoint->Release();
  m_pSinkPoint = NULL;
  m_pBrowserSink->Release();
  m_pBrowserSink = NULL;
  m_dwSinkCookie = 0;
}

BOOL CHtmlEditCtrl::DestroyWindow() {
  DisconnectBrowser();
  return CWnd::DestroyWindow();
}

BOOL CHtmlEditCtrl::GetDHtmlDocument(IHTMLDocument2** ppDocument) const {
  return BrowserDocument(m_hWnd != NULL ? const_cast<CHtmlEditCtrl*>(this)->GetControlUnknown()
                                        : NULL,
                         ppDocument);
}

// ---------------------------------------------------------------------
// CHtmlEditView

IMPLEMENT_DYNCREATE(CHtmlEditView, CHtmlView)

BEGIN_DHTMLEDITING_CMDMAP(CHtmlEditView)
  DHTMLEDITING_CMD_ENTRY(ID_EDIT_COPY, IDM_COPY)
  DHTMLEDITING_CMD_ENTRY(ID_EDIT_CUT, IDM_CUT)
  DHTMLEDITING_CMD_ENTRY(ID_EDIT_PASTE, IDM_PASTE)
  DHTMLEDITING_CMD_ENTRY(ID_EDIT_CLEAR, IDM_DELETE)
  DHTMLEDITING_CMD_ENTRY(ID_EDIT_UNDO, IDM_UNDO)
  DHTMLEDITING_CMD_ENTRY(ID_EDIT_REDO, IDM_REDO)
  DHTMLEDITING_CMD_ENTRY(ID_EDIT_SELECT_ALL, IDM_SELECTALL)
END_DHTMLEDITING_CMDMAP()

CHtmlEditView::CHtmlEditView() {}

CHtmlEditView::~CHtmlEditView() {}

LPCTSTR CHtmlEditView::GetStartDocument() { return _T("about:blank"); }

BOOL CHtmlEditView::Create(LPCTSTR lpszClassName, LPCTSTR lpszWindowName, DWORD dwStyle,
                           const RECT& rect, CWnd* pParentWnd, UINT nID,
                           CCreateContext* pContext) {
  if (!CHtmlView::Create(lpszClassName, lpszWindowName, dwStyle, rect, pParentWnd, nID, pContext))
    return FALSE;
  Navigate2(GetStartDocument());
  return TRUE;
}

CHtmlEditDoc* CHtmlEditView::GetDocument() const {
  return DYNAMIC_DOWNCAST(CHtmlEditDoc, m_pDocument);
}

BOOL CHtmlEditView::GetDHtmlDocument(IHTMLDocument2** ppDocument) const {
  return BrowserDocument(m_pBrowserApp, ppDocument);
}

void CHtmlEditView::OnNavigateComplete2(LPCTSTR strURL) {
  CHtmlView::OnNavigateComplete2(strURL);
  SetDesignMode(TRUE);
}

const DHtmlEditingCmdMapEntry* CHtmlEditView::FindEditingCmd(UINT nID) const {
  const DHtmlEditingCmdMapEntry* apMaps[2] = {GetDHtmlEditingCmdMap(),
                                              CHtmlEditView::GetDHtmlEditingCmdMap()};
  for (int i = 0; i < 2; i++) {
    if (i == 1 && apMaps[1] == apMaps[0]) break;
    for (const DHtmlEditingCmdMapEntry* p = apMaps[i]; p != NULL && p->nCmdID != 0; p++)
      if (p->nCmdID == nID) return p;
  }
  return NULL;
}

BOOL CHtmlEditView::OnCmdMsg(UINT nID, int nCode, void* pExtra,
                             AFX_CMDHANDLERINFO* pHandlerInfo) {
  if (pHandlerInfo == NULL && (nCode == CN_COMMAND || nCode == CN_UPDATE_COMMAND_UI)) {
    const DHtmlEditingCmdMapEntry* p = FindEditingCmd(nID);
    if (p != NULL) {
      if (nCode == CN_UPDATE_COMMAND_UI) {
        CCmdUI* pCmdUI = (CCmdUI*)pExtra;
        long nFlags = QueryStatus(p->nDHtmlCmdID);
        pCmdUI->Enable((nFlags & OLECMDF_ENABLED) != 0);
        if (p->nElementType == AFX_UI_ELEMTYPE_CHECBOX)
          pCmdUI->SetCheck((nFlags & OLECMDF_LATCHED) != 0 ? 1 : 0);
        else if (p->nElementType == AFX_UI_ELEMTYPE_RADIO)
          pCmdUI->SetRadio((nFlags & OLECMDF_LATCHED) != 0);
      } else if (p->pfnCmd != NULL) {
        p->pfnCmd(this, nID);
      } else {
        ExecCommand(p->nDHtmlCmdID, OLECMDEXECOPT_DODEFAULT);
      }
      return TRUE;
    }
  }
  return CHtmlView::OnCmdMsg(nID, nCode, pExtra, pHandlerInfo);
}

// ---------------------------------------------------------------------
// CHtmlEditDoc

IMPLEMENT_DYNCREATE(CHtmlEditDoc, CDocument)

CHtmlEditDoc::CHtmlEditDoc() {}

CHtmlEditDoc::~CHtmlEditDoc() {}

CHtmlEditView* CHtmlEditDoc::GetView() const {
  POSITION pos = GetFirstViewPosition();
  while (pos != NULL) {
    CView* pView = GetNextView(pos);
    if (pView != NULL && pView->IsKindOf(RUNTIME_CLASS(CHtmlEditView)))
      return (CHtmlEditView*)pView;
  }
  return NULL;
}

BOOL CHtmlEditDoc::IsModified() {
  CHtmlEditView* pView = GetView();
  if (pView != NULL && pView->IsDirty() == S_OK) return TRUE;
  return CDocument::IsModified();
}

BOOL CHtmlEditDoc::OpenURL(LPCTSTR lpszURL) {
  CHtmlEditView* pView = GetView();
  if (pView == NULL || lpszURL == NULL) return FALSE;
  pView->Navigate2(lpszURL);
  SetModifiedFlag(FALSE);
  return TRUE;
}

BOOL CHtmlEditDoc::NewDocument() {
  CHtmlEditView* pView = GetView();
  if (pView == NULL) return FALSE;
  // A blank page; the start page when the document has none to clear.
  if (FAILED(pView->NewDocument())) pView->Navigate2(pView->GetStartDocument());
  SetModifiedFlag(FALSE);
  return TRUE;
}

BOOL CHtmlEditDoc::OnNewDocument() {
  if (!CDocument::OnNewDocument()) return FALSE;
  return GetView() == NULL || NewDocument();
}

BOOL CHtmlEditDoc::OnOpenDocument(LPCTSTR lpszPathName) {
  // The browser reads the file itself.
  if (!OpenURL(lpszPathName)) return FALSE;
  return TRUE;
}

BOOL CHtmlEditDoc::OnSaveDocument(LPCTSTR lpszPathName) {
  CHtmlEditView* pView = GetView();
  if (pView == NULL) return FALSE;
  CComPtr<IHTMLDocument2> spDoc;
  pView->GetDHtmlDocument(&spDoc);
  if (spDoc == NULL) return FALSE;
  // The page saves itself when it can, in its own character set.
  BOOL bSaved = FALSE;
  CComQIPtr<IPersistFile> spFile(static_cast<IUnknown*>(spDoc));
  if (spFile != NULL) bSaved = SUCCEEDED(spFile->Save(CStringW(lpszPathName), TRUE));
  if (!bSaved) {
    // Otherwise its text, in UTF-8 with a byte order mark.
    CString strHTML;
    if (FAILED(pView->GetDocumentHTML(strHTML))) return FALSE;
    CStringW strW(strHTML);
    int cb = ::WideCharToMultiByte(CP_UTF8, 0, strW, strW.GetLength(), NULL, 0, NULL, NULL);
    CStringA strA;
    if (cb > 0) {
      ::WideCharToMultiByte(CP_UTF8, 0, strW, strW.GetLength(), strA.GetBuffer(cb), cb, NULL,
                            NULL);
      strA.ReleaseBuffer(cb);
    }
    CFile file;
    CFileException fe;
    if (!file.Open(lpszPathName, CFile::modeCreate | CFile::modeWrite | CFile::shareExclusive,
                   &fe)) {
      ReportSaveLoadException(lpszPathName, &fe, TRUE, AFX_IDP_INVALID_FILENAME);
      return FALSE;
    }
    static const BYTE kBOM[3] = {0xEF, 0xBB, 0xBF};
    file.Write(kBOM, 3);
    file.Write((LPCSTR)strA, (UINT)strA.GetLength());
    file.Close();
  }
  // Saved: no longer changed.
  VARIANT var;
  var.vt = VT_BOOL;
  var.boolVal = VARIANT_FALSE;
  pView->ExecCommand(IDM_SETDIRTY, OLECMDEXECOPT_DONTPROMPTUSER, &var, NULL);
  SetModifiedFlag(FALSE);
  return TRUE;
}

void CHtmlEditDoc::DeleteContents() { CDocument::DeleteContents(); }
