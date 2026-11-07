// htmltest.cpp - CHtmlView. Wine's WebBrowser control needs Gecko to
// show a page, so the test registers a WebBrowser of its own for
// CLSID_WebBrowser in this process (CoRegisterClassObject). It keeps what
// the view asks of it, answers with fixed values, and fires the events a
// browser fires while it navigates. The test checks:
//   - the view makes the control and its IWebBrowser2 (m_pBrowserApp),
//     the browser fills the view and follows its size;
//   - Navigate, the Navigate2 forms (URL, posted data, item list) and
//     LoadFromResource, with their arguments;
//   - the events reach the handlers with their arguments turned into
//     strings, and a handler's cancel goes back to the browser;
//   - the properties, PutProperty/GetProperty, ExecWB and QueryStatusWB,
//     ID_FILE_PRINT, the forms commands and GetSource on the document;
//   - the site's IDocHostUIHandler asks the view's On* functions;
//   - PreTranslateMessage gives the browser its keys first;
//   - the browser is let go when the frame closes.
// And CDHtmlDialog, over a page of fake elements (a document whose
// document.all finds them, each with its events' connection point):
//   - the browser covers the dialog, which shows the page in its
//     resources, takes the page's title, and fills the page with its data
//     when the document is complete;
//   - the DDX_DHtml functions both ways: values, numbers, a check box,
//     radio buttons, a select's index, text and value, inner text and
//     HTML, a property of an ActiveX control in the page;
//   - the element functions and the page's control's properties;
//   - the DHTML event map by id, class and tag, and the document's own
//     events, with GetEvent while a handler runs;
//   - the host flags, and window.external only for a page on this
//     computer;
//   - a new page moves the handlers to it; closing lets everything go.
#include <afxwin.h>
#include <afxhtml.h>
#include "htmltest.h"
#include "htmlstubs.h"
#include "testutil.h"

static const TCHAR kBrowserClass[] = _T("HtmlTestBrowser");

// =====================================================================
// The document the browser shows: its source, and the forms commands.

class CFakeDoc final : public IDispatch, public IPersistStreamInit, public IOleCommandTarget {
public:
  LONG m_nRef;
  DWORD m_nLastExec;
  CFakeDoc() : m_nRef(1), m_nLastExec(0) {}

  STDMETHOD(QueryInterface)(REFIID iid, void** ppv) {
    if (iid == IID_IUnknown || iid == IID_IDispatch)
      *ppv = static_cast<IDispatch*>(this);
    else if (iid == IID_IPersistStreamInit || iid == IID_IPersist)
      *ppv = static_cast<IPersistStreamInit*>(this);
    else if (iid == IID_IOleCommandTarget)
      *ppv = static_cast<IOleCommandTarget*>(this);
    else {
      *ppv = NULL;
      return E_NOINTERFACE;
    }
    AddRef();
    return S_OK;
  }
  STDMETHOD_(ULONG, AddRef)() { return (ULONG)::InterlockedIncrement(&m_nRef); }
  STDMETHOD_(ULONG, Release)() {
    LONG n = ::InterlockedDecrement(&m_nRef);
    if (n == 0) delete this;
    return (ULONG)n;
  }
  // IDispatch
  STDMETHOD(GetTypeInfoCount)(UINT* pc) {
    *pc = 0;
    return S_OK;
  }
  STDMETHOD(GetTypeInfo)(UINT, LCID, ITypeInfo**) { return E_NOTIMPL; }
  STDMETHOD(GetIDsOfNames)(REFIID, LPOLESTR*, UINT, LCID, DISPID*) { return E_NOTIMPL; }
  STDMETHOD(Invoke)(DISPID, REFIID, LCID, WORD, DISPPARAMS*, VARIANT*, EXCEPINFO*, UINT*) {
    return DISP_E_MEMBERNOTFOUND;
  }
  // IPersistStreamInit: the page is UTF-8.
  STDMETHOD(GetClassID)(CLSID* p) {
    *p = CLSID_NULL;
    return S_OK;
  }
  STDMETHOD(IsDirty)() { return S_FALSE; }
  STDMETHOD(Load)(LPSTREAM) { return E_NOTIMPL; }
  STDMETHOD(Save)(LPSTREAM pStm, BOOL) {
    static const char kPage[] = "<html><body>Caf\xc3\xa9</body></html>";
    return pStm->Write(kPage, sizeof(kPage) - 1, NULL);
  }
  STDMETHOD(GetSizeMax)(ULARGE_INTEGER*) { return E_NOTIMPL; }
  STDMETHOD(InitNew)() { return S_OK; }
  // IOleCommandTarget: forms command 7 is there and checked; others are not.
  STDMETHOD(QueryStatus)(const GUID* pguid, ULONG cCmds, OLECMD* prgCmds, OLECMDTEXT*) {
    static const GUID kForms3 = {
        0xde4ba900, 0x59ca, 0x11cf, {0x95, 0x92, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00}};
    if (pguid == NULL || *pguid != kForms3) return OLECMDERR_E_UNKNOWNGROUP;
    for (ULONG i = 0; i < cCmds; i++)
      prgCmds[i].cmdf =
          prgCmds[i].cmdID == 7 ? OLECMDF_SUPPORTED | OLECMDF_ENABLED | OLECMDF_LATCHED : 0;
    return S_OK;
  }
  STDMETHOD(Exec)(const GUID*, DWORD nCmdID, DWORD, VARIANT* pIn, VARIANT* pOut) {
    m_nLastExec = nCmdID;
    if (pOut != NULL) {
      ::VariantInit(pOut);
      pOut->vt = VT_I4;
      pOut->lVal = (LONG)nCmdID * 2 + (pIn != NULL && pIn->vt == VT_I4 ? pIn->lVal : 0);
    }
    return S_OK;
  }
};

// =====================================================================
// A page's elements, for CDHtmlDialog: a document, its elements and
// collections, and the events' object. Each answers what the dialog asks
// (htmlstubs.h answers the rest with E_NOTIMPL).

// One event connection point, with room for one sink.
class CFakeEvents : public IConnectionPoint {
public:
  IUnknown* m_pOwner;
  IID m_iid;
  LPDISPATCH m_pSink;
  CFakeEvents(IUnknown* pOwner, REFIID iid) : m_pOwner(pOwner), m_iid(iid), m_pSink(NULL) {}
  ~CFakeEvents() {
    if (m_pSink != NULL) m_pSink->Release();
  }
  STDMETHOD(QueryInterface)(REFIID iid, void** ppv) {
    if (iid == IID_IUnknown || iid == IID_IConnectionPoint) {
      *ppv = this;
      AddRef();
      return S_OK;
    }
    *ppv = NULL;
    return E_NOINTERFACE;
  }
  STDMETHOD_(ULONG, AddRef)() { return m_pOwner->AddRef(); }
  STDMETHOD_(ULONG, Release)() { return m_pOwner->Release(); }
  STDMETHOD(GetConnectionInterface)(IID* p) {
    *p = m_iid;
    return S_OK;
  }
  STDMETHOD(GetConnectionPointContainer)(IConnectionPointContainer** pp) {
    return m_pOwner->QueryInterface(IID_IConnectionPointContainer, (void**)pp);
  }
  STDMETHOD(Advise)(LPUNKNOWN pUnk, DWORD* pdwCookie) {
    *pdwCookie = 0;
    if (m_pSink != NULL) return CONNECT_E_ADVISELIMIT;
    // The sink answers the events' interface.
    if (FAILED(pUnk->QueryInterface(m_iid, (void**)&m_pSink))) return CONNECT_E_CANNOTCONNECT;
    *pdwCookie = 7;
    return S_OK;
  }
  STDMETHOD(Unadvise)(DWORD dwCookie) {
    if (dwCookie != 7 || m_pSink == NULL) return CONNECT_E_NOCONNECTION;
    m_pSink->Release();
    m_pSink = NULL;
    return S_OK;
  }
  STDMETHOD(EnumConnections)(IEnumConnections** pp) {
    *pp = NULL;
    return E_NOTIMPL;
  }
};

static const IID kDIID_HTMLElementEvents2 = {
    0x3050f60f, 0x98b5, 0x11cf, {0xbb, 0x82, 0x00, 0xaa, 0x00, 0xbd, 0xce, 0x0b}};
static const IID kDIID_HTMLDocumentEvents2 = {
    0x3050f613, 0x98b5, 0x11cf, {0xbb, 0x82, 0x00, 0xaa, 0x00, 0xbd, 0xce, 0x0b}};

#define FAKE_REFCOUNT                                                     \
  LONG m_nRef;                                                            \
  STDMETHOD_(ULONG, AddRef)() { return (ULONG)::InterlockedIncrement(&m_nRef); } \
  STDMETHOD_(ULONG, Release)() {                                          \
    LONG n = ::InterlockedDecrement(&m_nRef);                             \
    if (n == 0) delete this;                                              \
    return (ULONG)n;                                                      \
  }
#define FAKE_DISPATCH_INFO                                                        \
  STDMETHOD(GetTypeInfoCount)(UINT* pc) {                                         \
    *pc = 0;                                                                      \
    return S_OK;                                                                  \
  }                                                                               \
  STDMETHOD(GetTypeInfo)(UINT, LCID, ITypeInfo**) { return E_NOTIMPL; }          \
  STDMETHOD(GetIDsOfNames)(REFIID, LPOLESTR*, UINT, LCID, DISPID*) { return E_NOTIMPL; }

static int g_nDomAlive = 0;

class CFakeElement;

// The object an event handler can ask about the event.
class CFakeEventObj final : public CStubHTMLEventObj {
public:
  FAKE_REFCOUNT
  IHTMLElement* m_pSrc;
  explicit CFakeEventObj(IHTMLElement* pSrc) : m_nRef(1), m_pSrc(pSrc) { m_pSrc->AddRef(); }
  ~CFakeEventObj() { m_pSrc->Release(); }
  STDMETHOD(QueryInterface)(REFIID iid, void** ppv) {
    if (iid == IID_IUnknown || iid == IID_IDispatch || iid == IID_IHTMLEventObj) {
      *ppv = static_cast<IHTMLEventObj*>(this);
      AddRef();
      return S_OK;
    }
    *ppv = NULL;
    return E_NOINTERFACE;
  }
  FAKE_DISPATCH_INFO
  STDMETHOD(Invoke)(DISPID, REFIID, LCID, WORD, DISPPARAMS*, VARIANT*, EXCEPINFO*, UINT*) {
    return DISP_E_MEMBERNOTFOUND;
  }
  STDMETHOD(get_srcElement)(IHTMLElement** p) {
    *p = m_pSrc;
    m_pSrc->AddRef();
    return S_OK;
  }
  STDMETHOD(get_type)(BSTR* p) {
    *p = ::SysAllocString(L"click");
    return S_OK;
  }
};

class CFakeElement final : public CStubHTMLElement, public IConnectionPointContainer {
public:
  FAKE_REFCOUNT
  CString m_strId, m_strClass, m_strTag, m_strText, m_strHtml;
  CMap<DISPID, DISPID, COleVariant, const VARIANT&> m_props;
  CArray<CFakeElement*, CFakeElement*> m_options;  // a <select>'s
  CFakeEvents m_events;

  CFakeElement(LPCTSTR lpszTag, LPCTSTR lpszId, LPCTSTR lpszClass = _T(""))
      : m_nRef(1), m_strId(lpszId), m_strClass(lpszClass), m_strTag(lpszTag),
        m_events(static_cast<IHTMLElement*>(this), kDIID_HTMLElementEvents2) {
    g_nDomAlive++;
  }
  ~CFakeElement() {
    for (int i = 0; i < m_options.GetSize(); i++) m_options[i]->Release();
    g_nDomAlive--;
  }
  void SetProp(DISPID dispid, const VARIANT& v) { m_props[dispid] = v; }
  COleVariant GetProp(DISPID dispid) {
    COleVariant v;
    m_props.Lookup(dispid, v);
    return v;
  }
  BOOL Connected() const { return m_events.m_pSink != NULL; }
  // Fires an event as the page would: the event object is the argument.
  void Fire(DISPID dispid, IHTMLElement* pSrc = NULL) {
    if (m_events.m_pSink == NULL) return;
    CFakeEventObj* pEvent = new CFakeEventObj(pSrc != NULL ? pSrc : this);
    VARIANT arg;
    arg.vt = VT_DISPATCH;
    arg.pdispVal = static_cast<IHTMLEventObj*>(pEvent);
    DISPPARAMS dp = {&arg, NULL, 1, 0};
    m_events.m_pSink->Invoke(dispid, IID_NULL, 0, DISPATCH_METHOD, &dp, NULL, NULL, NULL);
    pEvent->Release();
  }

  STDMETHOD(QueryInterface)(REFIID iid, void** ppv) {
    if (iid == IID_IUnknown || iid == IID_IDispatch || iid == IID_IHTMLElement)
      *ppv = static_cast<IHTMLElement*>(this);
    else if (iid == IID_IConnectionPointContainer)
      *ppv = static_cast<IConnectionPointContainer*>(this);
    else {
      *ppv = NULL;
      return E_NOINTERFACE;
    }
    AddRef();
    return S_OK;
  }
  FAKE_DISPATCH_INFO
  STDMETHOD(Invoke)(DISPID dispid, REFIID, LCID, WORD wFlags, DISPPARAMS* pdp, VARIANT* pResult,
                    EXCEPINFO*, UINT*) {
    BOOL bPut = (wFlags & (DISPATCH_PROPERTYPUT | DISPATCH_PROPERTYPUTREF)) != 0;
    BOOL bSelect = m_strTag == _T("SELECT");
    if (bPut && (pdp == NULL || pdp->cArgs != 1)) return DISP_E_BADPARAMCOUNT;
    VARIANT* pArg = bPut ? &pdp->rgvarg[0] : NULL;
    if (dispid == (DISPID)DISPID_IHTMLELEMENT_INNERTEXT ||
        dispid == (DISPID)DISPID_IHTMLELEMENT_INNERHTML) {
      CString& str = dispid == (DISPID)DISPID_IHTMLELEMENT_INNERTEXT ? m_strText : m_strHtml;
      if (bPut) {
        COleVariant v(*pArg);
        v.ChangeType(VT_BSTR);
        str = v.bstrVal;
      } else if (pResult != NULL) {
        pResult->vt = VT_BSTR;
        pResult->bstrVal = ::SysAllocString(CStringW(str));
      }
      return S_OK;
    }
    if (bSelect) {
      if (dispid == DISPID_IHTMLSELECTELEMENT_LENGTH && !bPut) {
        pResult->vt = VT_I4;
        pResult->lVal = (LONG)m_options.GetSize();
        return S_OK;
      }
      if (dispid == DISPID_IHTMLSELECTELEMENT_ITEM && (wFlags & DISPATCH_METHOD)) {
        // item(name, index): the option at name, a number here.
        COleVariant v(pdp->rgvarg[pdp->cArgs - 1]);
        v.ChangeType(VT_I4);
        if (v.lVal < 0 || v.lVal >= m_options.GetSize()) return E_INVALIDARG;
        pResult->vt = VT_DISPATCH;
        pResult->pdispVal = static_cast<IHTMLElement*>(m_options[v.lVal]);
        pResult->pdispVal->AddRef();
        return S_OK;
      }
      if (dispid == DISPID_IHTMLSELECTELEMENT_VALUE) {
        // The selected option's value, or the option with this value.
        LONG nSel = GetProp(DISPID_IHTMLSELECTELEMENT_SELECTEDINDEX).lVal;
        if (bPut) {
          COleVariant v(*pArg);
          v.ChangeType(VT_BSTR);
          for (int i = 0; i < m_options.GetSize(); i++) {
            COleVariant ov = m_options[i]->GetProp(DISPID_IHTMLOPTIONELEMENT_VALUE);
            if (ov.vt == VT_BSTR && wcscmp(ov.bstrVal, v.bstrVal) == 0)
              SetProp(DISPID_IHTMLSELECTELEMENT_SELECTEDINDEX, COleVariant((long)i, VT_I4));
          }
        } else if (nSel >= 0 && nSel < m_options.GetSize()) {
          COleVariant v = m_options[nSel]->GetProp(DISPID_IHTMLOPTIONELEMENT_VALUE);
          ::VariantCopy(pResult, &v);
        }
        return S_OK;
      }
    }
    // Anything else is kept as it is put.
    if (bPut) {
      SetProp(dispid, *pArg);
      return S_OK;
    }
    COleVariant v;
    if (!m_props.Lookup(dispid, v)) return DISP_E_MEMBERNOTFOUND;
    return pResult != NULL ? ::VariantCopy(pResult, &v) : S_OK;
  }
  STDMETHOD(get_id)(BSTR* p) {
    *p = ::SysAllocString(CStringW(m_strId));
    return S_OK;
  }
  STDMETHOD(get_className)(BSTR* p) {
    *p = ::SysAllocString(CStringW(m_strClass));
    return S_OK;
  }
  STDMETHOD(get_tagName)(BSTR* p) {
    *p = ::SysAllocString(CStringW(m_strTag));
    return S_OK;
  }
  STDMETHOD(get_innerText)(BSTR* p) {
    *p = ::SysAllocString(CStringW(m_strText));
    return S_OK;
  }
  STDMETHOD(put_innerText)(BSTR v) {
    m_strText = v;
    return S_OK;
  }
  STDMETHOD(get_innerHTML)(BSTR* p) {
    *p = ::SysAllocString(CStringW(m_strHtml));
    return S_OK;
  }
  STDMETHOD(put_innerHTML)(BSTR v) {
    m_strHtml = v;
    return S_OK;
  }
  // IConnectionPointContainer
  STDMETHOD(EnumConnectionPoints)(IEnumConnectionPoints** pp) {
    *pp = NULL;
    return E_NOTIMPL;
  }
  STDMETHOD(FindConnectionPoint)(REFIID iid, IConnectionPoint** pp) {
    *pp = NULL;
    if (iid != kDIID_HTMLElementEvents2) return CONNECT_E_NOCONNECTION;
    *pp = &m_events;
    AddRef();
    return S_OK;
  }
};

// document.all, and the collections it gives.
class CFakeCollection final : public CStubHTMLElementCollection {
public:
  FAKE_REFCOUNT
  CArray<CFakeElement*, CFakeElement*> m_items;
  CFakeCollection() : m_nRef(1) { g_nDomAlive++; }
  ~CFakeCollection() {
    for (int i = 0; i < m_items.GetSize(); i++) m_items[i]->Release();
    g_nDomAlive--;
  }
  void Add(CFakeElement* p) {
    p->AddRef();
    m_items.Add(p);
  }
  STDMETHOD(QueryInterface)(REFIID iid, void** ppv) {
    if (iid == IID_IUnknown || iid == IID_IDispatch || iid == IID_IHTMLElementCollection) {
      *ppv = static_cast<IHTMLElementCollection*>(this);
      AddRef();
      return S_OK;
    }
    *ppv = NULL;
    return E_NOINTERFACE;
  }
  FAKE_DISPATCH_INFO
  STDMETHOD(Invoke)(DISPID, REFIID, LCID, WORD, DISPPARAMS*, VARIANT*, EXCEPINFO*, UINT*) {
    return DISP_E_MEMBERNOTFOUND;
  }
  STDMETHOD(get_length)(LONG* p) {
    *p = (LONG)m_items.GetSize();
    return S_OK;
  }
  // item(id): the element, or a collection when several share the id;
  // item(number): the element at the index.
  STDMETHOD(item)(VARIANT name, VARIANT index, IDispatch** pdisp) {
    *pdisp = NULL;
    if (name.vt == VT_I4) {
      if (name.lVal >= 0 && name.lVal < m_items.GetSize()) {
        *pdisp = static_cast<IHTMLElement*>(m_items[name.lVal]);
        (*pdisp)->AddRef();
      }
      return S_OK;
    }
    if (name.vt != VT_BSTR) return E_INVALIDARG;
    CFakeCollection* pMatches = new CFakeCollection;
    for (int i = 0; i < m_items.GetSize(); i++)
      if (m_items[i]->m_strId == CString(name.bstrVal)) pMatches->Add(m_items[i]);
    if (pMatches->m_items.GetSize() == 1 || (index.vt == VT_I4 && pMatches->m_items.GetSize() > 0)) {
      int n = index.vt == VT_I4 ? index.lVal : 0;
      if (n < pMatches->m_items.GetSize()) {
        *pdisp = static_cast<IHTMLElement*>(pMatches->m_items[n]);
        (*pdisp)->AddRef();
      }
    } else if (pMatches->m_items.GetSize() > 1) {
      *pdisp = pMatches;
      pMatches->AddRef();
    }
    pMatches->Release();
    return S_OK;
  }
  STDMETHOD(tags)(VARIANT tagName, IDispatch** pdisp) {
    CFakeCollection* pMatches = new CFakeCollection;
    for (int i = 0; i < m_items.GetSize(); i++)
      if (tagName.vt == VT_BSTR && m_items[i]->m_strTag.CompareNoCase(CString(tagName.bstrVal)) == 0)
        pMatches->Add(m_items[i]);
    *pdisp = pMatches;
    return S_OK;
  }
};

class CFakeHtmlDoc final : public CStubHTMLDocument2,
                          public IConnectionPointContainer,
                          public IOleCommandTarget,
                          public IPersistStreamInit {
public:
  FAKE_REFCOUNT
  CFakeCollection* m_pAll;
  CString m_strTitle;
  CFakeEvents m_events;
  // For HTML editing: design mode, the editing commands, the text.
  CString m_strDesignMode;
  DWORD m_nLastCmd, m_nLastOpt;
  COleVariant m_varLastIn;
  int m_nCmds;
  BOOL m_bBold, m_bDirty;
  CStringW m_strSource;
  int m_nInitNew;
  CFakeHtmlDoc()
      : m_nRef(1), m_pAll(new CFakeCollection),
        m_events(static_cast<IHTMLDocument2*>(this), kDIID_HTMLDocumentEvents2),
        m_nLastCmd(0), m_nLastOpt(0), m_nCmds(0), m_bBold(FALSE), m_bDirty(FALSE),
        m_nInitNew(0) {
    g_nDomAlive++;
  }
  ~CFakeHtmlDoc() {
    m_pAll->Release();
    g_nDomAlive--;
  }
  CFakeElement* Add(LPCTSTR lpszTag, LPCTSTR lpszId, LPCTSTR lpszClass = _T("")) {
    CFakeElement* p = new CFakeElement(lpszTag, lpszId, lpszClass);
    m_pAll->Add(p);
    p->Release();
    return p;
  }
  CFakeElement* Find(LPCTSTR lpszId, int n = 0) {
    for (int i = 0; i < m_pAll->m_items.GetSize(); i++)
      if (m_pAll->m_items[i]->m_strId == lpszId && n-- == 0) return m_pAll->m_items[i];
    return NULL;
  }
  void Fire(DISPID dispid, IHTMLElement* pSrc) {
    if (m_events.m_pSink == NULL) return;
    CFakeEventObj* pEvent = new CFakeEventObj(pSrc);
    VARIANT arg;
    arg.vt = VT_DISPATCH;
    arg.pdispVal = static_cast<IHTMLEventObj*>(pEvent);
    DISPPARAMS dp = {&arg, NULL, 1, 0};
    m_events.m_pSink->Invoke(dispid, IID_NULL, 0, DISPATCH_METHOD, &dp, NULL, NULL, NULL);
    pEvent->Release();
  }
  STDMETHOD(QueryInterface)(REFIID iid, void** ppv) {
    if (iid == IID_IUnknown || iid == IID_IDispatch || iid == IID_IHTMLDocument ||
        iid == IID_IHTMLDocument2)
      *ppv = static_cast<IHTMLDocument2*>(this);
    else if (iid == IID_IConnectionPointContainer)
      *ppv = static_cast<IConnectionPointContainer*>(this);
    else if (iid == IID_IOleCommandTarget)
      *ppv = static_cast<IOleCommandTarget*>(this);
    else if (iid == IID_IPersistStreamInit)
      *ppv = static_cast<IPersistStreamInit*>(this);
    else {
      *ppv = NULL;
      return E_NOINTERFACE;
    }
    AddRef();
    return S_OK;
  }
  FAKE_DISPATCH_INFO
  STDMETHOD(Invoke)(DISPID, REFIID, LCID, WORD, DISPPARAMS*, VARIANT*, EXCEPINFO*, UINT*) {
    return DISP_E_MEMBERNOTFOUND;
  }
  STDMETHOD(put_designMode)(BSTR v) {
    m_strDesignMode = v;
    return S_OK;
  }
  STDMETHOD(get_designMode)(BSTR* p) {
    *p = ::SysAllocString(CStringW(m_strDesignMode));
    return S_OK;
  }

  // ----- The editing commands (CGID_MSHTML)
  static BOOL IsMshtml(const GUID* pGuid) {
    static const GUID kMshtml = {
        0xde4ba900, 0x59ca, 0x11cf, {0x95, 0x92, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00}};
    return pGuid != NULL && *pGuid == kMshtml;
  }
  STDMETHOD(QueryStatus)(const GUID* pGuid, ULONG cCmds, OLECMD* prgCmds, OLECMDTEXT*) {
    if (!IsMshtml(pGuid)) return OLECMDERR_E_UNKNOWNGROUP;
    for (ULONG i = 0; i < cCmds; i++) {
      DWORD f = OLECMDF_SUPPORTED;
      if (prgCmds[i].cmdID != IDM_PASTE) f |= OLECMDF_ENABLED;  // nothing to paste
      if (prgCmds[i].cmdID == IDM_BOLD && m_bBold) f |= OLECMDF_LATCHED;
      prgCmds[i].cmdf = f;
    }
    return S_OK;
  }
  STDMETHOD(Exec)(const GUID* pGuid, DWORD nCmdID, DWORD nOpt, VARIANT* pIn, VARIANT* pOut) {
    if (!IsMshtml(pGuid)) return OLECMDERR_E_UNKNOWNGROUP;
    m_nCmds++;
    m_nLastCmd = nCmdID;
    m_nLastOpt = nOpt;
    m_varLastIn.Clear();
    if (pIn != NULL) m_varLastIn = *pIn;
    if (nCmdID == IDM_BOLD) m_bBold = !m_bBold;
    if (nCmdID == IDM_SETDIRTY && pIn != NULL && pIn->vt == VT_BOOL)
      m_bDirty = pIn->boolVal != VARIANT_FALSE;
    if (pOut == NULL) return S_OK;
    ::VariantInit(pOut);
    switch (nCmdID) {
      case IDM_FONTNAME:
        pOut->vt = VT_BSTR;
        pOut->bstrVal = ::SysAllocString(L"Arial");
        break;
      case IDM_BLOCKFMT:
        pOut->vt = VT_BSTR;
        pOut->bstrVal = ::SysAllocString(L"Normal");
        break;
      case IDM_FONTSIZE:
        pOut->vt = VT_I4;
        pOut->lVal = 3;
        break;
      case IDM_FORECOLOR:
        pOut->vt = VT_I4;
        pOut->lVal = 0xff;
        break;
      case IDM_BACKCOLOR:
        pOut->vt = VT_NULL;  // the selection has more than one
        break;
      case IDM_GETBLOCKFMTS: {
        SAFEARRAY* psa = ::SafeArrayCreateVector(VT_BSTR, 0, 2);
        LONG i = 0;
        BSTR b = ::SysAllocString(L"Normal");
        ::SafeArrayPutElement(psa, &i, b);
        ::SysFreeString(b);
        i = 1;
        b = ::SysAllocString(L"Heading 1");
        ::SafeArrayPutElement(psa, &i, b);
        ::SysFreeString(b);
        pOut->vt = VT_ARRAY | VT_BSTR;
        pOut->parray = psa;
        break;
      }
      default:
        break;
    }
    return S_OK;
  }

  // ----- The page's text
  STDMETHOD(GetClassID)(CLSID* p) {
    *p = CLSID_NULL;
    return E_NOTIMPL;
  }
  STDMETHOD(IsDirty)() { return m_bDirty ? S_OK : S_FALSE; }
  STDMETHOD(Load)(LPSTREAM pStm) {
    CByteArray ba;
    BYTE buf[256];
    ULONG cb = 0;
    while (SUCCEEDED(pStm->Read(buf, sizeof(buf), &cb)) && cb != 0) {
      INT_PTR n = ba.GetSize();
      ba.SetSize(n + cb);
      memcpy(ba.GetData() + n, buf, cb);
    }
    // UTF-16 with its mark, as CHtmlEditCtrlBase writes it.
    if (ba.GetSize() < 2 || ba[0] != 0xFF || ba[1] != 0xFE) return E_FAIL;
    m_strSource = CStringW((LPCWSTR)(ba.GetData() + 2), (int)(ba.GetSize() - 2) / 2);
    m_bDirty = TRUE;
    return S_OK;
  }
  STDMETHOD(Save)(LPSTREAM pStm, BOOL) {
    // UTF-8, as a page usually is.
    CStringA strA;
    int cb = ::WideCharToMultiByte(CP_UTF8, 0, m_strSource, m_strSource.GetLength(), NULL, 0,
                                   NULL, NULL);
    if (cb > 0) {
      ::WideCharToMultiByte(CP_UTF8, 0, m_strSource, m_strSource.GetLength(), strA.GetBuffer(cb),
                            cb, NULL, NULL);
      strA.ReleaseBuffer(cb);
    }
    return pStm->Write((LPCSTR)strA, (ULONG)strA.GetLength(), NULL);
  }
  STDMETHOD(GetSizeMax)(ULARGE_INTEGER* p) {
    p->QuadPart = 0;
    return E_NOTIMPL;
  }
  STDMETHOD(InitNew)() {
    m_strSource.Empty();
    m_nInitNew++;
    return S_OK;
  }
  STDMETHOD(get_all)(IHTMLElementCollection** p) {
    *p = m_pAll;
    m_pAll->AddRef();
    return S_OK;
  }
  STDMETHOD(get_title)(BSTR* p) {
    *p = ::SysAllocString(CStringW(m_strTitle));
    return S_OK;
  }
  STDMETHOD(EnumConnectionPoints)(IEnumConnectionPoints** pp) {
    *pp = NULL;
    return E_NOTIMPL;
  }
  STDMETHOD(FindConnectionPoint)(REFIID iid, IConnectionPoint** pp) {
    *pp = NULL;
    if (iid != kDIID_HTMLDocumentEvents2) return CONNECT_E_NOCONNECTION;
    *pp = &m_events;
    AddRef();
    return S_OK;
  }
};

// The document the next browser shows, when it is not its own.
static IDispatch* g_pNextDoc = NULL;

// =====================================================================
// The browser

class CFakeBrowser : public IWebBrowser2,
                     public IOleObject,
                     public IOleInPlaceObject,
                     public IOleInPlaceActiveObject,
                     public IOleControl,
                     public IProvideClassInfo2,
                     public IConnectionPointContainer {
public:
  // The one connection point, for DWebBrowserEvents2. (IOleObject has an
  // Unadvise of the same type, so it is an object of its own.)
  class CPoint : public IConnectionPoint {
  public:
    CFakeBrowser* m_pOwner;
    STDMETHOD(QueryInterface)(REFIID iid, void** ppv) {
      if (iid == IID_IUnknown || iid == IID_IConnectionPoint) {
        *ppv = this;
        AddRef();
        return S_OK;
      }
      *ppv = NULL;
      return E_NOINTERFACE;
    }
    STDMETHOD_(ULONG, AddRef)() { return m_pOwner->AddRef(); }
    STDMETHOD_(ULONG, Release)() { return m_pOwner->Release(); }
    STDMETHOD(GetConnectionInterface)(IID* p) {
      *p = DIID_DWebBrowserEvents2;
      return S_OK;
    }
    STDMETHOD(GetConnectionPointContainer)(IConnectionPointContainer** pp) {
      *pp = m_pOwner;
      AddRef();
      return S_OK;
    }
    // Room for two sinks: the container's, and one of the control's
    // own (CHtmlEditCtrl's).
    STDMETHOD(Advise)(LPUNKNOWN pUnk, DWORD* pdwCookie) {
      *pdwCookie = 0;
      LPDISPATCH* ppSlot = m_pOwner->m_pSink == NULL    ? &m_pOwner->m_pSink
                           : m_pOwner->m_pSink2 == NULL ? &m_pOwner->m_pSink2
                                                        : NULL;
      if (ppSlot == NULL) return CONNECT_E_ADVISELIMIT;
      if (FAILED(pUnk->QueryInterface(IID_IDispatch, (void**)ppSlot))) return CONNECT_E_CANNOTCONNECT;
      *pdwCookie = ppSlot == &m_pOwner->m_pSink ? 1 : 2;
      return S_OK;
    }
    STDMETHOD(Unadvise)(DWORD dwCookie) {
      LPDISPATCH* ppSlot = dwCookie == 1   ? &m_pOwner->m_pSink
                           : dwCookie == 2 ? &m_pOwner->m_pSink2
                                           : NULL;
      if (ppSlot == NULL || *ppSlot == NULL) return CONNECT_E_NOCONNECTION;
      (*ppSlot)->Release();
      *ppSlot = NULL;
      return S_OK;
    }
    STDMETHOD(EnumConnections)(IEnumConnections** pp) {
      *pp = NULL;
      return E_NOTIMPL;
    }
  } m_point;

  static int s_nAlive;
  static CFakeBrowser* s_pLast;

  LONG m_nRef;
  LPOLECLIENTSITE m_pClientSite;
  LPOLEINPLACESITE m_pIPSite;
  IDocHostUIHandler* m_pHostUI;
  LPDISPATCH m_pSink;
  LPDISPATCH m_pSink2;
  HWND m_hWnd;
  CFakeDoc* m_pDoc;
  IDispatch* m_pDocOverride;  // the document it shows instead

  // What the view asked.
  CString m_strLastCall;
  CString m_strURL;  // the last navigation's
  LONG m_nNavFlags;
  CString m_strTarget, m_strHeaders;
  CByteArray m_baPost;
  VARTYPE m_vtURL;  // the URL VARIANT's type: VT_BSTR, or an item list's byte array
  ULONG m_cbURL;
  BOOL m_bCancelled;  // the view cancelled the last navigation
  OLECMDID m_nLastExecWB;
  OLECMDEXECOPT m_nLastExecOpt;
  int m_nAccelerators;

  // Its properties.
  CString m_strLocation, m_strTitle;
  LONG m_nLeft, m_nTop, m_nWidth, m_nHeight;
  int m_nToolBar;
  BOOL m_bVisible, m_bOffline, m_bSilent, m_bMenuBar, m_bFullScreen, m_bStatusBar;
  BOOL m_bRegBrowser, m_bRegDrop, m_bTheater, m_bAddressBar;
  CStringArray m_aPropNames;
  CArray<COleVariant, const VARIANT&> m_aPropValues;

  CFakeBrowser()
      : m_nRef(1),
        m_pClientSite(NULL),
        m_pIPSite(NULL),
        m_pHostUI(NULL),
        m_pSink(NULL),
        m_pSink2(NULL),
        m_hWnd(NULL),
        m_pDoc(new CFakeDoc),
        m_pDocOverride(g_pNextDoc),
        m_nNavFlags(0),
        m_vtURL(VT_EMPTY),
        m_cbURL(0),
        m_bCancelled(FALSE),
        m_nLastExecWB((OLECMDID)0),
        m_nLastExecOpt((OLECMDEXECOPT)0),
        m_nAccelerators(0),
        m_nLeft(0),
        m_nTop(0),
        m_nWidth(0),
        m_nHeight(0),
        m_nToolBar(1),
        m_bVisible(TRUE),
        m_bOffline(FALSE),
        m_bSilent(FALSE),
        m_bMenuBar(TRUE),
        m_bFullScreen(FALSE),
        m_bStatusBar(TRUE),
        m_bRegBrowser(FALSE),
        m_bRegDrop(TRUE),
        m_bTheater(FALSE),
        m_bAddressBar(TRUE) {
    m_point.m_pOwner = this;
    if (m_pDocOverride != NULL) m_pDocOverride->AddRef();
    s_nAlive++;
    s_pLast = this;
  }
  virtual ~CFakeBrowser() {
    m_pDoc->Release();
    if (m_pDocOverride != NULL) m_pDocOverride->Release();
    if (s_pLast == this) s_pLast = NULL;
    s_nAlive--;
  }

  // ----- Events

  // Fires an event with the arguments in their declared order.
  void Fire(DISPID dispid, VARIANT* pArgs = NULL, UINT nArgs = 0) {
    VARIANT rev[8];
    for (UINT i = 0; i < nArgs; i++) rev[i] = pArgs[nArgs - 1 - i];
    DISPPARAMS dp = {nArgs ? rev : NULL, NULL, nArgs, 0};
    if (m_pSink != NULL) m_pSink->Invoke(dispid, IID_NULL, 0, DISPATCH_METHOD, &dp, NULL, NULL, NULL);
    if (m_pSink2 != NULL)
      m_pSink2->Invoke(dispid, IID_NULL, 0, DISPATCH_METHOD, &dp, NULL, NULL, NULL);
  }
  static VARIANT Disp(IDispatch* p) {
    VARIANT v;
    v.vt = VT_DISPATCH;
    v.pdispVal = p;
    return v;
  }
  static VARIANT Ref(VARIANT* p) {
    VARIANT v;
    v.vt = VT_BYREF | VT_VARIANT;
    v.pvarVal = p;
    return v;
  }
  static VARIANT RefBool(VARIANT_BOOL* p) {
    VARIANT v;
    v.vt = VT_BYREF | VT_BOOL;
    v.pboolVal = p;
    return v;
  }
  static VARIANT Str(LPCTSTR lpsz) {
    VARIANT v;
    v.vt = VT_BSTR;
    v.bstrVal = ::SysAllocString(CStringW(lpsz));
    return v;
  }
  static VARIANT Int(LONG n) {
    VARIANT v;
    v.vt = VT_I4;
    v.lVal = n;
    return v;
  }
  static VARIANT Bool(BOOL b) {
    VARIANT v;
    v.vt = VT_BOOL;
    v.boolVal = b ? VARIANT_TRUE : VARIANT_FALSE;
    return v;
  }
  void FireString(DISPID dispid, LPCTSTR lpsz) {
    VARIANT v = Str(lpsz);
    Fire(dispid, &v, 1);
    ::VariantClear(&v);
  }
  void FireBool(DISPID dispid, BOOL b) {
    VARIANT v = Bool(b);
    Fire(dispid, &v, 1);
  }
  // NewWindow2: the handler may cancel it.
  BOOL FireNewWindow() {
    IDispatch* pDisp = NULL;
    VARIANT_BOOL bCancel = VARIANT_FALSE;
    VARIANT args[2];
    args[0].vt = VT_BYREF | VT_DISPATCH;
    args[0].ppdispVal = &pDisp;
    args[1] = RefBool(&bCancel);
    Fire(DISPID_NEWWINDOW2, args, 2);
    if (pDisp != NULL) pDisp->Release();
    return bCancel != VARIANT_FALSE;
  }

  // What a browser fires on its way to a page.
  void DoNavigate(VARIANT* pURL, VARIANT* pFlags, VARIANT* pTarget, VARIANT* pPost,
                  VARIANT* pHeaders) {
    m_vtURL = pURL != NULL ? pURL->vt : VT_EMPTY;
    m_cbURL = 0;
    m_strURL.Empty();
    if (pURL != NULL && pURL->vt == VT_BSTR) m_strURL = pURL->bstrVal;
    if (pURL != NULL && pURL->vt == (VT_ARRAY | VT_UI1)) {
      LONG lo = 0, hi = -1;
      ::SafeArrayGetLBound(pURL->parray, 1, &lo);
      ::SafeArrayGetUBound(pURL->parray, 1, &hi);
      m_cbURL = (ULONG)(hi - lo + 1);
      m_strURL = _T("pidl:");
    }
    m_nNavFlags = pFlags != NULL && pFlags->vt == VT_I4 ? pFlags->lVal : -1;
    m_strTarget = pTarget != NULL && pTarget->vt == VT_BSTR ? CString(pTarget->bstrVal) : CString();
    m_strHeaders =
        pHeaders != NULL && pHeaders->vt == VT_BSTR ? CString(pHeaders->bstrVal) : CString();
    m_baPost.RemoveAll();
    if (pPost != NULL && pPost->vt == (VT_ARRAY | VT_UI1)) {
      LONG lo = 0, hi = -1;
      ::SafeArrayGetLBound(pPost->parray, 1, &lo);
      ::SafeArrayGetUBound(pPost->parray, 1, &hi);
      void* pv = NULL;
      if (hi >= lo && SUCCEEDED(::SafeArrayAccessData(pPost->parray, &pv))) {
        m_baPost.SetSize(hi - lo + 1);
        memcpy(m_baPost.GetData(), pv, (size_t)(hi - lo + 1));
        ::SafeArrayUnaccessData(pPost->parray);
      }
    }

    // BeforeNavigate2, with the page's arguments as the browser passes
    // them: VARIANTs by reference.
    VARIANT vURL = Str(m_strURL), vFlags = Int(m_nNavFlags), vTarget = Str(m_strTarget),
            vHeaders = Str(m_strHeaders), vPost;
    ::VariantInit(&vPost);
    if (pPost != NULL) ::VariantCopy(&vPost, pPost);
    VARIANT vPostRef = Ref(&vPost);  // PostData is a VARIANT holding a VARIANT
    VARIANT_BOOL bCancel = VARIANT_FALSE;
    VARIANT args[7] = {Disp(this),        Ref(&vURL),          Ref(&vFlags),         Ref(&vTarget),
                       Ref(&vPostRef),    Ref(&vHeaders),      RefBool(&bCancel)};
    Fire(DISPID_BEFORENAVIGATE2, args, 7);
    m_bCancelled = bCancel != VARIANT_FALSE;
    ::VariantClear(&vFlags);
    ::VariantClear(&vTarget);
    ::VariantClear(&vHeaders);
    ::VariantClear(&vPost);
    if (m_bCancelled) {
      ::VariantClear(&vURL);
      return;
    }

    // A page that is not there.
    if (m_strURL.Left(4) == _T("bad:")) {
      VARIANT vFrame = Str(_T("main")), vStatus = Int(404);
      VARIANT_BOOL bCancelErr = VARIANT_FALSE;
      VARIANT eargs[5] = {Disp(this), Ref(&vURL), Ref(&vFrame), Ref(&vStatus),
                          RefBool(&bCancelErr)};
      Fire(DISPID_NAVIGATEERROR, eargs, 5);
      m_bCancelled = bCancelErr != VARIANT_FALSE;
      ::VariantClear(&vFrame);
      ::VariantClear(&vURL);
      return;
    }

    Fire(DISPID_DOWNLOADBEGIN);
    m_strLocation = m_strURL;
    m_strTitle = _T("Title of ") + m_strURL;
    VARIANT cargs[2] = {Disp(this), Ref(&vURL)};
    Fire(DISPID_NAVIGATECOMPLETE2, cargs, 2);
    FireString(DISPID_TITLECHANGE, m_strTitle);
    FireString(DISPID_STATUSTEXTCHANGE, _T("Done"));
    VARIANT pargs[2] = {Int(100), Int(100)};
    Fire(DISPID_PROGRESSCHANGE, pargs, 2);
    VARIANT sargs[2] = {Int(CSC_NAVIGATEBACK), Bool(TRUE)};
    Fire(DISPID_COMMANDSTATECHANGE, sargs, 2);
    FireString(DISPID_PROPERTYCHANGE, _T("Prop"));
    Fire(DISPID_DOWNLOADCOMPLETE);
    Fire(DISPID_DOCUMENTCOMPLETE, cargs, 2);
    ::VariantClear(&vURL);
  }

  // ----- Activation

  HRESULT Activate(LPOLECLIENTSITE pSite, HWND hWndParent, LPCRECT lprcPos) {
    if (m_hWnd != NULL) return S_OK;
    if (pSite == NULL) pSite = m_pClientSite;
    if (pSite == NULL) return E_UNEXPECTED;
    if (FAILED(pSite->QueryInterface(IID_IOleInPlaceSite, (void**)&m_pIPSite))) return E_FAIL;
    if (m_pHostUI == NULL) pSite->QueryInterface(IID_IDocHostUIHandler, (void**)&m_pHostUI);
    m_pIPSite->OnInPlaceActivate();
    HWND hWndSite = NULL;
    m_pIPSite->GetWindow(&hWndSite);
    RECT rc = {0, 0, 100, 100};
    if (lprcPos != NULL) rc = *lprcPos;
    m_hWnd = ::CreateWindowEx(0, kBrowserClass, NULL, WS_CHILD | WS_VISIBLE, rc.left, rc.top,
                              rc.right - rc.left, rc.bottom - rc.top,
                              hWndParent != NULL ? hWndParent : hWndSite, NULL,
                              AfxGetInstanceHandle(), NULL);
    return m_hWnd != NULL ? S_OK : E_FAIL;
  }
  void Deactivate() {
    if (m_hWnd != NULL) {
      HWND h = m_hWnd;
      m_hWnd = NULL;
      ::DestroyWindow(h);
    }
    if (m_pIPSite != NULL) {
      m_pIPSite->OnInPlaceDeactivate();
      m_pIPSite->Release();
      m_pIPSite = NULL;
    }
  }

  // ----- IUnknown

  STDMETHOD(QueryInterface)(REFIID iid, void** ppv) {
    if (iid == IID_IUnknown || iid == IID_IDispatch || iid == IID_IWebBrowser ||
        iid == IID_IWebBrowserApp || iid == IID_IWebBrowser2)
      *ppv = static_cast<IWebBrowser2*>(this);
    else if (iid == IID_IOleObject)
      *ppv = static_cast<IOleObject*>(this);
    else if (iid == IID_IOleInPlaceObject || iid == IID_IOleWindow)
      *ppv = static_cast<IOleInPlaceObject*>(this);
    else if (iid == IID_IOleInPlaceActiveObject)
      *ppv = static_cast<IOleInPlaceActiveObject*>(this);
    else if (iid == IID_IOleControl)
      *ppv = static_cast<IOleControl*>(this);
    else if (iid == IID_IProvideClassInfo || iid == IID_IProvideClassInfo2)
      *ppv = static_cast<IProvideClassInfo2*>(this);
    else if (iid == IID_IConnectionPointContainer)
      *ppv = static_cast<IConnectionPointContainer*>(this);
    else {
      *ppv = NULL;
      return E_NOINTERFACE;
    }
    AddRef();
    return S_OK;
  }
  STDMETHOD_(ULONG, AddRef)() { return (ULONG)::InterlockedIncrement(&m_nRef); }
  STDMETHOD_(ULONG, Release)() {
    LONG n = ::InterlockedDecrement(&m_nRef);
    if (n == 0) delete this;
    return (ULONG)n;
  }

  // ----- IDispatch (the view uses the vtable)

  STDMETHOD(GetTypeInfoCount)(UINT* pc) {
    *pc = 0;
    return S_OK;
  }
  STDMETHOD(GetTypeInfo)(UINT, LCID, ITypeInfo**) { return E_NOTIMPL; }
  STDMETHOD(GetIDsOfNames)(REFIID, LPOLESTR*, UINT, LCID, DISPID*) { return E_NOTIMPL; }
  STDMETHOD(Invoke)(DISPID, REFIID, LCID, WORD, DISPPARAMS*, VARIANT*, EXCEPINFO*, UINT*) {
    return DISP_E_MEMBERNOTFOUND;
  }

  // ----- IWebBrowser

  STDMETHOD(GoBack)() { m_strLastCall = _T("GoBack"); return S_OK; }
  STDMETHOD(GoForward)() { m_strLastCall = _T("GoForward"); return S_OK; }
  STDMETHOD(GoHome)() { m_strLastCall = _T("GoHome"); return S_OK; }
  STDMETHOD(GoSearch)() { m_strLastCall = _T("GoSearch"); return S_OK; }
  STDMETHOD(Navigate)(BSTR URL, VARIANT* Flags, VARIANT* Target, VARIANT* Post,
                      VARIANT* Headers) {
    m_strLastCall = _T("Navigate");
    VARIANT v;
    v.vt = VT_BSTR;
    v.bstrVal = URL;
    DoNavigate(&v, Flags, Target, Post, Headers);
    return S_OK;
  }
  STDMETHOD(Refresh)() { m_strLastCall = _T("Refresh"); return S_OK; }
  STDMETHOD(Refresh2)(VARIANT* Level) {
    m_strLastCall.Format(_T("Refresh2 %ld"),
                         Level != NULL && Level->vt == VT_I4 ? (long)Level->lVal : -1L);
    return S_OK;
  }
  STDMETHOD(Stop)() { m_strLastCall = _T("Stop"); return S_OK; }
  STDMETHOD(get_Application)(IDispatch** pp) {
    *pp = static_cast<IWebBrowser2*>(this);
    AddRef();
    return S_OK;
  }
  STDMETHOD(get_Parent)(IDispatch** pp) { return get_Application(pp); }
  STDMETHOD(get_Container)(IDispatch** pp) {
    *pp = NULL;
    return S_OK;
  }
  STDMETHOD(get_Document)(IDispatch** pp) {
    *pp = m_pDocOverride != NULL ? m_pDocOverride : static_cast<IDispatch*>(m_pDoc);
    (*pp)->AddRef();
    return S_OK;
  }
  STDMETHOD(get_TopLevelContainer)(VARIANT_BOOL* p) {
    *p = VARIANT_TRUE;
    return S_OK;
  }
  STDMETHOD(get_Type)(BSTR* p) {
    *p = ::SysAllocString(L"HTML Document");
    return S_OK;
  }
#define FAKE_LONG(name, member)                    \
  STDMETHOD(get_##name)(LONG * p) {                \
    *p = member;                                   \
    return S_OK;                                   \
  }                                                \
  STDMETHOD(put_##name)(LONG n) {                  \
    member = n;                                    \
    return S_OK;                                   \
  }
  FAKE_LONG(Left, m_nLeft)
  FAKE_LONG(Top, m_nTop)
  FAKE_LONG(Width, m_nWidth)
  FAKE_LONG(Height, m_nHeight)
  STDMETHOD(get_LocationName)(BSTR* p) {
    *p = ::SysAllocString(CStringW(m_strTitle));
    return S_OK;
  }
  STDMETHOD(get_LocationURL)(BSTR* p) {
    *p = ::SysAllocString(CStringW(m_strLocation));
    return S_OK;
  }
  STDMETHOD(get_Busy)(VARIANT_BOOL* p) {
    *p = VARIANT_FALSE;
    return S_OK;
  }

  // ----- IWebBrowserApp

  STDMETHOD(Quit)() { return S_OK; }
  STDMETHOD(ClientToWindow)(int*, int*) { return E_NOTIMPL; }
  STDMETHOD(PutProperty)(BSTR Property, VARIANT vtValue) {
    CString strName(Property);
    for (int i = 0; i < m_aPropNames.GetSize(); i++)
      if (m_aPropNames[i] == strName) {
        m_aPropValues[i] = vtValue;
        return S_OK;
      }
    m_aPropNames.Add(strName);
    m_aPropValues.Add(vtValue);
    return S_OK;
  }
  STDMETHOD(GetProperty)(BSTR Property, VARIANT* pvtValue) {
    ::VariantInit(pvtValue);
    CString strName(Property);
    for (int i = 0; i < m_aPropNames.GetSize(); i++)
      if (m_aPropNames[i] == strName) return ::VariantCopy(pvtValue, &m_aPropValues[i]);
    return S_OK;  // VT_EMPTY, as a browser answers for a name it does not have
  }
  STDMETHOD(get_Name)(BSTR* p) {
    *p = ::SysAllocString(L"Fake Browser");
    return S_OK;
  }
  STDMETHOD(get_HWND)(SHANDLE_PTR* p) {
    *p = (SHANDLE_PTR)m_hWnd;
    return S_OK;
  }
  STDMETHOD(get_FullName)(BSTR* p) {
    *p = ::SysAllocString(L"C:\\fake\\browser.exe");
    return S_OK;
  }
  STDMETHOD(get_Path)(BSTR* p) {
    *p = ::SysAllocString(L"C:\\fake\\");
    return S_OK;
  }
#define FAKE_BOOL(name, member, dispid)               \
  STDMETHOD(get_##name)(VARIANT_BOOL * p) {           \
    *p = member ? VARIANT_TRUE : VARIANT_FALSE;       \
    return S_OK;                                      \
  }                                                   \
  STDMETHOD(put_##name)(VARIANT_BOOL b) {             \
    member = b != VARIANT_FALSE;                      \
    if (dispid != 0) FireBool(dispid, member);        \
    return S_OK;                                      \
  }
  FAKE_BOOL(Visible, m_bVisible, DISPID_ONVISIBLE)
  FAKE_BOOL(StatusBar, m_bStatusBar, DISPID_ONSTATUSBAR)
  STDMETHOD(get_StatusText)(BSTR* p) {
    *p = ::SysAllocString(L"");
    return S_OK;
  }
  STDMETHOD(put_StatusText)(BSTR) { return S_OK; }
  STDMETHOD(get_ToolBar)(int* p) {
    *p = m_nToolBar;
    return S_OK;
  }
  STDMETHOD(put_ToolBar)(int n) {
    m_nToolBar = n;
    FireBool(DISPID_ONTOOLBAR, n != 0);
    return S_OK;
  }
  FAKE_BOOL(MenuBar, m_bMenuBar, DISPID_ONMENUBAR)
  FAKE_BOOL(FullScreen, m_bFullScreen, DISPID_ONFULLSCREEN)

  // ----- IWebBrowser2

  STDMETHOD(Navigate2)(VARIANT* URL, VARIANT* Flags, VARIANT* Target, VARIANT* Post,
                       VARIANT* Headers) {
    m_strLastCall = _T("Navigate2");
    DoNavigate(URL, Flags, Target, Post, Headers);
    return S_OK;
  }
  STDMETHOD(QueryStatusWB)(OLECMDID cmdID, OLECMDF* pcmdf) {
    *pcmdf = cmdID == OLECMDID_PRINT ? (OLECMDF)(OLECMDF_SUPPORTED | OLECMDF_ENABLED)
                                     : (OLECMDF)0;
    return S_OK;
  }
  STDMETHOD(ExecWB)(OLECMDID cmdID, OLECMDEXECOPT opt, VARIANT*, VARIANT*) {
    m_nLastExecWB = cmdID;
    m_nLastExecOpt = opt;
    return S_OK;
  }
  STDMETHOD(ShowBrowserBar)(VARIANT*, VARIANT*, VARIANT*) { return E_NOTIMPL; }
  STDMETHOD(get_ReadyState)(READYSTATE* p) {
    *p = READYSTATE_COMPLETE;
    return S_OK;
  }
  FAKE_BOOL(Offline, m_bOffline, 0)
  FAKE_BOOL(Silent, m_bSilent, 0)
  FAKE_BOOL(RegisterAsBrowser, m_bRegBrowser, 0)
  FAKE_BOOL(RegisterAsDropTarget, m_bRegDrop, 0)
  FAKE_BOOL(TheaterMode, m_bTheater, DISPID_ONTHEATERMODE)
  FAKE_BOOL(AddressBar, m_bAddressBar, 0)
  STDMETHOD(get_Resizable)(VARIANT_BOOL* p) {
    *p = VARIANT_TRUE;
    return S_OK;
  }
  STDMETHOD(put_Resizable)(VARIANT_BOOL) { return S_OK; }

  // ----- IOleObject

  STDMETHOD(SetClientSite)(LPOLECLIENTSITE pSite) {
    if (pSite != NULL) pSite->AddRef();
    if (m_pClientSite != NULL) m_pClientSite->Release();
    m_pClientSite = pSite;
    if (pSite == NULL && m_pHostUI != NULL) {
      m_pHostUI->Release();
      m_pHostUI = NULL;
    }
    return S_OK;
  }
  STDMETHOD(GetClientSite)(LPOLECLIENTSITE* pp) {
    *pp = m_pClientSite;
    if (*pp != NULL) (*pp)->AddRef();
    return S_OK;
  }
  STDMETHOD(SetHostNames)(LPCOLESTR, LPCOLESTR) { return S_OK; }
  STDMETHOD(Close)(DWORD) {
    Deactivate();
    return S_OK;
  }
  STDMETHOD(SetMoniker)(DWORD, LPMONIKER) { return E_NOTIMPL; }
  STDMETHOD(GetMoniker)(DWORD, DWORD, LPMONIKER* pp) {
    *pp = NULL;
    return E_NOTIMPL;
  }
  STDMETHOD(InitFromData)(LPDATAOBJECT, BOOL, DWORD) { return E_NOTIMPL; }
  STDMETHOD(GetClipboardData)(DWORD, LPDATAOBJECT* pp) {
    *pp = NULL;
    return E_NOTIMPL;
  }
  STDMETHOD(DoVerb)(LONG iVerb, LPMSG, LPOLECLIENTSITE pSite, LONG, HWND hWndParent,
                    LPCRECT lprcPos) {
    switch (iVerb) {
      case OLEIVERB_PRIMARY:
      case OLEIVERB_SHOW:
      case OLEIVERB_UIACTIVATE:
      case OLEIVERB_INPLACEACTIVATE:
        return Activate(pSite, hWndParent, lprcPos);
      case OLEIVERB_HIDE:
        if (m_hWnd != NULL) ::ShowWindow(m_hWnd, SW_HIDE);
        return S_OK;
    }
    return OLEOBJ_S_INVALIDVERB;
  }
  STDMETHOD(EnumVerbs)(LPENUMOLEVERB* pp) {
    *pp = NULL;
    return E_NOTIMPL;
  }
  STDMETHOD(Update)() { return S_OK; }
  STDMETHOD(IsUpToDate)() { return S_OK; }
  STDMETHOD(GetUserClassID)(CLSID* p) {
    *p = CLSID_WebBrowser;
    return S_OK;
  }
  STDMETHOD(GetUserType)(DWORD, LPOLESTR* pp) {
    *pp = NULL;
    return E_NOTIMPL;
  }
  STDMETHOD(SetExtent)(DWORD, SIZEL*) { return S_OK; }
  STDMETHOD(GetExtent)(DWORD, SIZEL* p) {
    p->cx = p->cy = 0;
    return S_OK;
  }
  STDMETHOD(Advise)(LPADVISESINK, DWORD* pdw) {
    *pdw = 0;
    return OLE_E_ADVISENOTSUPPORTED;
  }
  STDMETHOD(Unadvise)(DWORD) { return OLE_E_NOCONNECTION; }
  STDMETHOD(EnumAdvise)(LPENUMSTATDATA* pp) {
    *pp = NULL;
    return OLE_E_ADVISENOTSUPPORTED;
  }
  STDMETHOD(GetMiscStatus)(DWORD, DWORD* p) {
    *p = OLEMISC_RECOMPOSEONRESIZE | OLEMISC_ACTIVATEWHENVISIBLE | OLEMISC_SETCLIENTSITEFIRST |
         OLEMISC_INSIDEOUT | OLEMISC_CANTLINKINSIDE;
    return S_OK;
  }
  STDMETHOD(SetColorScheme)(LOGPALETTE*) { return E_NOTIMPL; }

  // ----- IOleWindow, IOleInPlaceObject, IOleInPlaceActiveObject

  STDMETHOD(GetWindow)(HWND* p) {
    *p = m_hWnd;
    return m_hWnd != NULL ? S_OK : E_FAIL;
  }
  STDMETHOD(ContextSensitiveHelp)(BOOL) { return E_NOTIMPL; }
  STDMETHOD(InPlaceDeactivate)() {
    Deactivate();
    return S_OK;
  }
  STDMETHOD(UIDeactivate)() { return S_OK; }
  STDMETHOD(SetObjectRects)(LPCRECT pos, LPCRECT) {
    if (m_hWnd != NULL)
      ::MoveWindow(m_hWnd, pos->left, pos->top, pos->right - pos->left, pos->bottom - pos->top,
                   TRUE);
    return S_OK;
  }
  STDMETHOD(ReactivateAndUndo)() { return E_NOTIMPL; }
  // F5 is the page's own key.
  STDMETHOD(TranslateAccelerator)(LPMSG lpmsg) {
    if (lpmsg->message == WM_KEYDOWN && lpmsg->wParam == VK_F5) {
      m_nAccelerators++;
      return S_OK;
    }
    return S_FALSE;
  }
  STDMETHOD(OnFrameWindowActivate)(BOOL) { return S_OK; }
  STDMETHOD(OnDocWindowActivate)(BOOL) { return S_OK; }
  STDMETHOD(ResizeBorder)(LPCRECT, IOleInPlaceUIWindow*, BOOL) { return S_OK; }
  STDMETHOD(EnableModeless)(BOOL) { return S_OK; }

  // ----- IOleControl

  STDMETHOD(GetControlInfo)(CONTROLINFO* p) {
    p->hAccel = NULL;
    p->cAccel = 0;
    p->dwFlags = 0;
    return S_OK;
  }
  STDMETHOD(OnMnemonic)(LPMSG) { return E_NOTIMPL; }
  STDMETHOD(OnAmbientPropertyChange)(DISPID) { return S_OK; }
  STDMETHOD(FreezeEvents)(BOOL) { return S_OK; }

  // ----- IProvideClassInfo2: the events are DWebBrowserEvents2.

  STDMETHOD(GetClassInfo)(ITypeInfo** pp) {
    *pp = NULL;
    return E_NOTIMPL;
  }
  STDMETHOD(GetGUID)(DWORD dwKind, GUID* p) {
    if (dwKind != GUIDKIND_DEFAULT_SOURCE_DISP_IID) return E_INVALIDARG;
    *p = DIID_DWebBrowserEvents2;
    return S_OK;
  }

  // ----- IConnectionPointContainer, and its one connection point

  STDMETHOD(EnumConnectionPoints)(IEnumConnectionPoints** pp) {
    *pp = NULL;
    return E_NOTIMPL;
  }
  STDMETHOD(FindConnectionPoint)(REFIID iid, IConnectionPoint** pp) {
    *pp = NULL;
    if (iid != DIID_DWebBrowserEvents2) return CONNECT_E_NOCONNECTION;
    *pp = &m_point;
    AddRef();
    return S_OK;
  }
};
int CFakeBrowser::s_nAlive = 0;
CFakeBrowser* CFakeBrowser::s_pLast = NULL;

static LRESULT CALLBACK BrowserWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  if (msg == WM_PAINT) {
    PAINTSTRUCT ps;
    HDC hdc = ::BeginPaint(hWnd, &ps);
    RECT rc;
    ::GetClientRect(hWnd, &rc);
    ::FillRect(hdc, &rc, (HBRUSH)::GetStockObject(WHITE_BRUSH));
    if (CFakeBrowser::s_pLast != NULL)
      ::DrawText(hdc, CFakeBrowser::s_pLast->m_strTitle, -1, &rc,
                 DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    ::EndPaint(hWnd, &ps);
    return 0;
  }
  return ::DefWindowProc(hWnd, msg, wParam, lParam);
}

class CFakeBrowserFactory : public IClassFactory {
public:
  STDMETHOD(QueryInterface)(REFIID iid, void** ppv) {
    if (iid == IID_IUnknown || iid == IID_IClassFactory) {
      *ppv = this;
      return S_OK;
    }
    *ppv = NULL;
    return E_NOINTERFACE;
  }
  STDMETHOD_(ULONG, AddRef)() { return 2; }
  STDMETHOD_(ULONG, Release)() { return 1; }
  STDMETHOD(CreateInstance)(LPUNKNOWN pOuter, REFIID iid, void** ppv) {
    *ppv = NULL;
    if (pOuter != NULL) return CLASS_E_NOAGGREGATION;
    CFakeBrowser* p = new CFakeBrowser;
    HRESULT hr = p->QueryInterface(iid, ppv);
    p->Release();
    return hr;
  }
  STDMETHOD(LockServer)(BOOL) { return S_OK; }
};
static CFakeBrowserFactory g_factory;

// =====================================================================
// The view

// The external object the view gives the page.
class CExternal : public CCmdTarget {
public:
  CExternal() { EnableAutomation(); }
};

class CTestHtmlView : public CHtmlView {
  DECLARE_DYNCREATE(CTestHtmlView)
public:
  CString m_strLog;  // the events, in order
  CString m_strBeforeURL, m_strBeforeTarget, m_strBeforeHeaders;
  DWORD m_nBeforeFlags;
  CByteArray m_baBeforePost;
  CString m_strError;
  DWORD m_dwError;
  long m_nProgress, m_nProgressMax;
  long m_nCommand;
  BOOL m_bCommandEnabled;
  int m_nVisibleEvents;
  BOOL m_bLastVisible;
  CExternal m_external;
  int m_nContextMenus;

  CTestHtmlView()
      : m_nBeforeFlags(0),
        m_dwError(0),
        m_nProgress(0),
        m_nProgressMax(0),
        m_nCommand(0),
        m_bCommandEnabled(FALSE),
        m_nVisibleEvents(0),
        m_bLastVisible(TRUE),
        m_nContextMenus(0) {}

  void Log(LPCTSTR lpsz) {
    m_strLog += lpsz;
    m_strLog += _T(";");
  }

  virtual void OnBeforeNavigate2(LPCTSTR lpszURL, DWORD nFlags, LPCTSTR lpszTargetFrameName,
                                 CByteArray& baPostedData, LPCTSTR lpszHeaders, BOOL* pbCancel) {
    Log(_T("before"));
    m_strBeforeURL = lpszURL;
    m_nBeforeFlags = nFlags;
    m_strBeforeTarget = lpszTargetFrameName;
    m_strBeforeHeaders = lpszHeaders;
    m_baBeforePost.Copy(baPostedData);
    // The view keeps the browser away from some pages.
    if (_tcsstr(lpszURL, _T("blocked")) != NULL) *pbCancel = TRUE;
  }
  virtual void OnNavigateComplete2(LPCTSTR strURL) {
    Log(CString(_T("complete ")) + strURL);
  }
  virtual void OnDocumentComplete(LPCTSTR lpszURL) { Log(CString(_T("doc ")) + lpszURL); }
  virtual void OnDownloadBegin() { Log(_T("dlbegin")); }
  virtual void OnDownloadComplete() { Log(_T("dlend")); }
  virtual void OnTitleChange(LPCTSTR lpszText) { Log(CString(_T("title ")) + lpszText); }
  virtual void OnStatusTextChange(LPCTSTR lpszText) {
    Log(CString(_T("status ")) + lpszText);
    CHtmlView::OnStatusTextChange(lpszText);
  }
  virtual void OnPropertyChange(LPCTSTR lpszProperty) {
    Log(CString(_T("prop ")) + lpszProperty);
  }
  virtual void OnProgressChange(long nProgress, long nProgressMax) {
    m_nProgress = nProgress;
    m_nProgressMax = nProgressMax;
  }
  virtual void OnCommandStateChange(long nCommand, BOOL bEnable) {
    m_nCommand = nCommand;
    m_bCommandEnabled = bEnable;
  }
  virtual void OnNavigateError(LPCTSTR lpszURL, LPCTSTR lpszFrame, DWORD dwError,
                               BOOL* pbCancel) {
    Log(_T("error"));
    m_strError.Format(_T("%s|%s"), lpszURL, lpszFrame);
    m_dwError = dwError;
    *pbCancel = TRUE;
  }
  virtual void OnNewWindow2(LPDISPATCH* ppDisp, BOOL* Cancel) {
    Log(_T("newwindow"));
    CHECK(ppDisp != NULL && *ppDisp == NULL);
    *Cancel = TRUE;
  }
  virtual void OnVisible(BOOL bVisible) {
    m_nVisibleEvents++;
    m_bLastVisible = bVisible;
  }
  virtual void OnTheaterMode(BOOL b) { Log(b ? _T("theater on") : _T("theater off")); }

  // The browser's questions.
  virtual HRESULT OnGetHostInfo(DOCHOSTUIINFO* pInfo) {
    pInfo->dwFlags = DOCHOSTUIFLAG_NO3DBORDER | DOCHOSTUIFLAG_SCROLL_NO;
    pInfo->dwDoubleClick = DOCHOSTUIDBLCLK_DEFAULT;
    return S_OK;
  }
  virtual HRESULT OnShowContextMenu(DWORD, LPPOINT, LPUNKNOWN, LPDISPATCH) {
    m_nContextMenus++;
    return S_OK;  // no menu
  }
  virtual HRESULT OnGetExternal(LPDISPATCH* lppDispatch) {
    *lppDispatch = m_external.GetIDispatch(TRUE);
    return S_OK;
  }
  virtual HRESULT OnTranslateUrl(DWORD, OLECHAR* pchURLIn, OLECHAR** ppchURLOut) {
    if (wcscmp(pchURLIn, L"old:") != 0) return CHtmlView::OnTranslateUrl(0, pchURLIn, ppchURLOut);
    static const WCHAR kNew[] = L"new:";
    *ppchURLOut = (OLECHAR*)::CoTaskMemAlloc(sizeof(kNew));
    memcpy(*ppchURLOut, kNew, sizeof(kNew));
    return S_OK;
  }
};
IMPLEMENT_DYNCREATE(CTestHtmlView, CHtmlView)

// =====================================================================
// CDHtmlDialog

// The ActiveX control in the page, an <object> element's object.
class CPageControl : public CCmdTarget {
public:
  CString m_strCaption;
  long m_nCount;
  CPageControl() : m_strCaption(_T("start")), m_nCount(3) { EnableAutomation(); }
  DECLARE_DISPATCH_MAP()
};
BEGIN_DISPATCH_MAP(CPageControl, CCmdTarget)
  DISP_PROPERTY_ID(CPageControl, "Caption", 1, m_strCaption, VT_BSTR)
  DISP_PROPERTY_ID(CPageControl, "Count", 2, m_nCount, VT_I4)
END_DISPATCH_MAP()

static CFakeHtmlDoc* MakePage(CPageControl* pCtl) {
  CFakeHtmlDoc* pDoc = new CFakeHtmlDoc;
  pDoc->m_strTitle = _T("Page Title");
  pDoc->Add(_T("INPUT"), _T("name"));
  pDoc->Add(_T("INPUT"), _T("age"));
  pDoc->Add(_T("INPUT"), _T("rate"));
  pDoc->Add(_T("INPUT"), _T("agree"));
  for (int i = 0; i < 3; i++) pDoc->Add(_T("INPUT"), _T("color"));
  static const LPCTSTR kSizes[3][2] = {{_T("Small"), _T("s")}, {_T("Medium"), _T("m")},
                                       {_T("Large"), _T("l")}};
  CFakeElement* pSize = pDoc->Add(_T("SELECT"), _T("size"));
  for (int i = 0; i < 3; i++) {
    CFakeElement* pOpt = new CFakeElement(_T("OPTION"), _T(""));
    pOpt->SetProp(DISPID_IHTMLOPTIONELEMENT_TEXT, COleVariant(kSizes[i][0]));
    pOpt->SetProp(DISPID_IHTMLOPTIONELEMENT_VALUE, COleVariant(kSizes[i][1]));
    pSize->m_options.Add(pOpt);
  }
  pSize->SetProp(DISPID_IHTMLSELECTELEMENT_SELECTEDINDEX, COleVariant(0L, VT_I4));
  CFakeElement* pNote = pDoc->Add(_T("DIV"), _T("note"));
  pNote->m_strText = _T("hello");
  CFakeElement* pRich = pDoc->Add(_T("DIV"), _T("rich"));
  pRich->m_strHtml = _T("<b>x</b>");
  pDoc->Add(_T("BUTTON"), _T("btnGo"), _T("hot big"));
  pDoc->Add(_T("SPAN"), _T("label"), _T("hot"));
  CFakeElement* pObj = pDoc->Add(_T("OBJECT"), _T("ctl"));
  VARIANT v;
  v.vt = VT_DISPATCH;
  v.pdispVal = pCtl->GetIDispatch(FALSE);
  pObj->SetProp(DISPID_IHTMLOBJECTELEMENT_OBJECT, v);
  return pDoc;
}

class CTestDHtmlDlg : public CDHtmlDialog {
public:
  CString m_strName;
  int m_nAge;
  double m_dRate;
  int m_nAgree;
  long m_nColor;
  long m_nSizeIndex;
  CString m_strSizeText, m_strSizeValue;
  CString m_strNote, m_strRich;
  COleVariant m_varCaption;
  int m_nGo, m_nHot, m_nFocus, m_nDocClicks;
  CString m_strLastId;  // the element the last handler got
  BOOL m_bHadEvent;     // GetEvent worked in the handler
  int m_nExchanges;

  CTestDHtmlDlg()
      : CDHtmlDialog(IDD_HTMLDLG, IDR_DLGPAGE),
        m_strName(_T("Bob")),
        m_nAge(30),
        m_dRate(1.25),
        m_nAgree(1),
        m_nColor(2),
        m_nSizeIndex(1),
        m_strNote(_T("note text")),
        m_strRich(_T("<i>rich</i>")),
        m_varCaption(_T("from dialog")),
        m_nGo(0),
        m_nHot(0),
        m_nFocus(0),
        m_nDocClicks(0),
        m_bHadEvent(FALSE),
        m_nExchanges(0) {
    m_bUseHtmlTitle = TRUE;
  }

  virtual void DoDataExchange(CDataExchange* pDX) {
    CDHtmlDialog::DoDataExchange(pDX);
    m_nExchanges++;
    DDX_DHtml_ElementValue(pDX, _T("name"), m_strName);
    DDX_DHtml_ElementValue(pDX, _T("age"), m_nAge);
    DDX_DHtml_ElementValue(pDX, _T("rate"), m_dRate);
    DDX_DHtml_CheckBox(pDX, _T("agree"), m_nAgree);
    DDX_DHtml_Radio(pDX, _T("color"), m_nColor);
    DDX_DHtml_SelectIndex(pDX, _T("size"), m_nSizeIndex);
    DDX_DHtml_ElementInnerText(pDX, _T("note"), m_strNote);
    DDX_DHtml_ElementInnerHtml(pDX, _T("rich"), m_strRich);
    DDX_DHtml_AxControl(pDX, _T("ctl"), _T("Caption"), m_varCaption);
  }

  void Took(IHTMLElement* pElement) {
    m_strLastId.Empty();
    BSTR bstr = NULL;
    if (pElement != NULL && SUCCEEDED(pElement->get_id(&bstr))) m_strLastId = bstr;
    ::SysFreeString(bstr);
    CComPtr<IHTMLEventObj> spEvent;
    m_bHadEvent = SUCCEEDED(GetEvent(&spEvent)) && spEvent != NULL;
  }
  HRESULT OnGo(IHTMLElement* p) {
    m_nGo++;
    Took(p);
    return S_OK;
  }
  HRESULT OnHot(IHTMLElement* p) {
    m_nHot++;
    Took(p);
    return S_OK;
  }
  HRESULT OnInputFocus(IHTMLElement* p) {
    m_nFocus++;
    Took(p);
    return S_OK;
  }
  HRESULT OnDocClick(IHTMLElement* p) {
    m_nDocClicks++;
    Took(p);
    return S_OK;
  }
  DECLARE_DHTML_EVENT_MAP()
};

BEGIN_DHTML_EVENT_MAP(CTestDHtmlDlg)
  DHTML_EVENT_ONCLICK(_T("btnGo"), OnGo)
  DHTML_EVENT_CLASS(DISPID_HTMLELEMENTEVENTS2_ONMOUSEOVER, _T("hot"), OnHot)
  DHTML_EVENT_TAG(DISPID_HTMLELEMENTEVENTS2_ONFOCUS, _T("input"), OnInputFocus)
  DHTML_EVENT_ONCLICK(NULL, OnDocClick)
END_DHTML_EVENT_MAP()

static CString ElemValue(CFakeElement* p, DISPID dispid) {
  COleVariant v = p->GetProp(dispid);
  if (v.vt == VT_EMPTY) return CString();
  v.ChangeType(VT_BSTR);
  return CString(v.bstrVal);
}

static void RunDHtmlDialog(CWnd* pParent) {
  TestLog("dhtml dialog");
  CPageControl ctl;
  CFakeHtmlDoc* pDoc = MakePage(&ctl);
  g_pNextDoc = static_cast<IHTMLDocument2*>(pDoc);
  int nBrowsersBefore = CFakeBrowser::s_nAlive;
  CTestDHtmlDlg* pDlg = new CTestDHtmlDlg;
  BOOL bMade = pDlg->Create(IDD_HTMLDLG, pParent);
  g_pNextDoc = NULL;
  CHECK(bMade);
  if (!bMade) {
    delete pDlg;
    pDoc->Release();
    return;
  }
  CFakeBrowser* pBrowser = CFakeBrowser::s_pLast;
  CHECK(CFakeBrowser::s_nAlive == nBrowsersBefore + 1);

  // The browser, its page from the resources, and the page's document.
  CHECK(pDlg->m_pBrowserApp == static_cast<IWebBrowser2*>(pBrowser));
  CHECK(pDlg->m_wndBrowser.m_hWnd == pBrowser->m_hWnd);
  CHECK(pDlg->m_wndBrowser.GetDlgCtrlID() == AFX_IDC_BROWSER);
  CRect rcDlg, rcBrowser;
  pDlg->GetClientRect(&rcDlg);
  ::GetWindowRect(pBrowser->m_hWnd, &rcBrowser);
  pDlg->ScreenToClient(&rcBrowser);
  CHECK(rcBrowser == rcDlg);
  CString strExe;
  ::GetModuleFileName(NULL, strExe.GetBuffer(MAX_PATH), MAX_PATH);
  strExe.ReleaseBuffer();
  CString strWant;
  strWant.Format(_T("res://%s/%d"), (LPCTSTR)strExe, IDR_DLGPAGE);
  CString strUrl;
  pDlg->GetCurrentUrl(strUrl);
  CHECK(strUrl == strWant && pBrowser->m_strURL == strWant);
  CHECK(pDlg->m_spHtmlDoc == static_cast<IHTMLDocument2*>(pDoc));
  CComPtr<IHTMLDocument2> spDoc;
  CHECK(SUCCEEDED(pDlg->GetDHtmlDocument(&spDoc)) && spDoc == static_cast<IHTMLDocument2*>(pDoc));
  // The page's title.
  CString strTitle;
  pDlg->GetWindowText(strTitle);
  CHECK(strTitle == _T("Page Title"));

  // When the page was there, the dialog's data went into it (and once
  // before, from CDialog::OnInitDialog, when there was no page).
  CHECK(pDlg->m_nExchanges == 2);
  CHECK(ElemValue(pDoc->Find(_T("name")), DISPID_IHTMLINPUTELEMENT_VALUE) == _T("Bob"));
  CHECK(ElemValue(pDoc->Find(_T("age")), DISPID_IHTMLINPUTELEMENT_VALUE) == _T("30"));
  CHECK(ElemValue(pDoc->Find(_T("rate")), DISPID_IHTMLINPUTELEMENT_VALUE) == _T("1.25"));
  CHECK(pDoc->Find(_T("agree"))->GetProp(DISPID_IHTMLINPUTELEMENT_CHECKED).boolVal == VARIANT_TRUE);
  CHECK(pDoc->Find(_T("color"), 0)->GetProp(DISPID_IHTMLINPUTELEMENT_CHECKED).boolVal == VARIANT_FALSE);
  CHECK(pDoc->Find(_T("color"), 2)->GetProp(DISPID_IHTMLINPUTELEMENT_CHECKED).boolVal == VARIANT_TRUE);
  CHECK(pDoc->Find(_T("size"))->GetProp(DISPID_IHTMLSELECTELEMENT_SELECTEDINDEX).lVal == 1);
  CHECK(pDoc->Find(_T("note"))->m_strText == _T("note text"));
  CHECK(pDoc->Find(_T("rich"))->m_strHtml == _T("<i>rich</i>"));
  CHECK(ctl.m_strCaption == _T("from dialog"));

  // The user changes the page; UpdateData brings it back.
  pDoc->Find(_T("name"))->SetProp(DISPID_IHTMLINPUTELEMENT_VALUE, COleVariant(_T("Carol")));
  pDoc->Find(_T("age"))->SetProp(DISPID_IHTMLINPUTELEMENT_VALUE, COleVariant(_T(" 52 ")));
  pDoc->Find(_T("rate"))->SetProp(DISPID_IHTMLINPUTELEMENT_VALUE, COleVariant(_T("0.5")));
  VARIANT vFalse, vTrue;
  vFalse.vt = vTrue.vt = VT_BOOL;
  vFalse.boolVal = VARIANT_FALSE;
  vTrue.boolVal = VARIANT_TRUE;
  pDoc->Find(_T("agree"))->SetProp(DISPID_IHTMLINPUTELEMENT_CHECKED, vFalse);
  pDoc->Find(_T("color"), 2)->SetProp(DISPID_IHTMLINPUTELEMENT_CHECKED, vFalse);
  pDoc->Find(_T("color"), 0)->SetProp(DISPID_IHTMLINPUTELEMENT_CHECKED, vTrue);
  pDoc->Find(_T("size"))->SetProp(DISPID_IHTMLSELECTELEMENT_SELECTEDINDEX, COleVariant(2L, VT_I4));
  pDoc->Find(_T("note"))->m_strText = _T("typed");
  pDoc->Find(_T("rich"))->m_strHtml = _T("<u>u</u>");
  ctl.m_strCaption = _T("from control");
  CHECK(pDlg->UpdateData(TRUE));
  CHECK(pDlg->m_strName == _T("Carol"));
  CHECK(pDlg->m_nAge == 52);
  CHECK(pDlg->m_dRate == 0.5);
  CHECK(pDlg->m_nAgree == 0);
  CHECK(pDlg->m_nColor == 0);
  CHECK(pDlg->m_nSizeIndex == 2);
  CHECK(pDlg->m_strNote == _T("typed"));
  CHECK(pDlg->m_strRich == _T("<u>u</u>"));
  CHECK(pDlg->m_varCaption.vt == VT_BSTR && CString(pDlg->m_varCaption.bstrVal) == _T("from control"));

  // The select's other forms.
  {
    CDataExchange dxSave(pDlg, TRUE), dxLoad(pDlg, FALSE);
    CString strText, strValue;
    pDlg->DDX_DHtml_SelectString(&dxSave, _T("size"), strText);
    pDlg->DDX_DHtml_SelectValue(&dxSave, _T("size"), strValue);
    CHECK(strText == _T("Large") && strValue == _T("l"));
    strText = _T("Small");
    pDlg->DDX_DHtml_SelectString(&dxLoad, _T("size"), strText);
    CHECK(pDoc->Find(_T("size"))->GetProp(DISPID_IHTMLSELECTELEMENT_SELECTEDINDEX).lVal == 0);
    strValue = _T("m");
    pDlg->DDX_DHtml_SelectValue(&dxLoad, _T("size"), strValue);
    CHECK(pDoc->Find(_T("size"))->GetProp(DISPID_IHTMLSELECTELEMENT_SELECTEDINDEX).lVal == 1);
    // An indeterminate check box is 2.
    pDoc->Find(_T("agree"))->SetProp(DISPID_IHTMLINPUTELEMENT_INDETERMINATE, vTrue);
    int nCheck = 0;
    pDlg->DDX_DHtml_CheckBox(&dxSave, _T("agree"), nCheck);
    CHECK(nCheck == 2);
    nCheck = 1;
    pDlg->DDX_DHtml_CheckBox(&dxLoad, _T("agree"), nCheck);
    CHECK(pDoc->Find(_T("agree"))->GetProp(DISPID_IHTMLINPUTELEMENT_INDETERMINATE).boolVal ==
          VARIANT_FALSE);
    CHECK(pDoc->Find(_T("agree"))->GetProp(DISPID_IHTMLINPUTELEMENT_CHECKED).boolVal == VARIANT_TRUE);
    // Numbers and amounts as text.
    DWORD dw = 4000000000UL;
    pDlg->DDX_DHtml_ElementInnerText(&dxLoad, _T("note"), dw);
    CHECK(pDoc->Find(_T("note"))->m_strText == _T("4000000000"));
    COleCurrency cur;
    pDoc->Find(_T("note"))->m_strText = _T("12.5");
    pDlg->DDX_DHtml_ElementInnerText(&dxSave, _T("note"), cur);
    CHECK(cur.m_cur.int64 == 125000);
  }

  // The elements.
  CComPtr<IDispatch> spDisp;
  BOOL bCollection = TRUE;
  CHECK(SUCCEEDED(pDlg->GetElement(_T("name"), &spDisp, &bCollection)) && !bCollection);
  spDisp.Release();
  CHECK(SUCCEEDED(pDlg->GetElement(_T("color"), &spDisp, &bCollection)) && bCollection);
  spDisp.Release();
  CHECK(FAILED(pDlg->GetElement(_T("nothing"), &spDisp, &bCollection)) && spDisp == NULL);
  CComPtr<IHTMLElement> spElem;
  CHECK(SUCCEEDED(pDlg->GetElement(_T("note"), &spElem)) && spElem != NULL);
  spElem.Release();
  CHECK(SUCCEEDED(pDlg->GetElementInterface(_T("label"), &spElem)) && spElem != NULL);
  BSTR bstr = pDlg->GetElementText(_T("note"));
  CHECK(bstr != NULL && wcscmp(bstr, L"12.5") == 0);
  ::SysFreeString(bstr);
  pDlg->SetElementText(_T("note"), CComBSTR(L"set by id"));
  CHECK(pDoc->Find(_T("note"))->m_strText == _T("set by id"));
  pDlg->SetElementText(spElem, CComBSTR(L"set on element"));
  CHECK(pDoc->Find(_T("label"))->m_strText == _T("set on element"));
  bstr = pDlg->GetElementHtml(_T("rich"));
  CHECK(bstr != NULL && wcscmp(bstr, L"<u>u</u>") == 0);
  ::SysFreeString(bstr);
  pDlg->SetElementHtml(_T("rich"), CComBSTR(L"<p>p</p>"));
  CHECK(pDoc->Find(_T("rich"))->m_strHtml == _T("<p>p</p>"));
  VARIANT var = pDlg->GetElementProperty(_T("name"), DISPID_IHTMLINPUTELEMENT_VALUE);
  CHECK(var.vt == VT_BSTR && wcscmp(var.bstrVal, L"Carol") == 0);
  ::VariantClear(&var);
  CComVariant varSet(L"Dave");
  pDlg->SetElementProperty(_T("name"), DISPID_IHTMLINPUTELEMENT_VALUE, &varSet);
  CHECK(ElemValue(pDoc->Find(_T("name")), DISPID_IHTMLINPUTELEMENT_VALUE) == _T("Dave"));

  // The page's control.
  CComPtr<IDispatch> spCtl;
  CHECK(SUCCEEDED(pDlg->GetControlDispatch(_T("ctl"), &spCtl)) &&
        spCtl == ctl.GetIDispatch(FALSE));
  var = pDlg->GetControlProperty(_T("ctl"), _T("Count"));
  CHECK(var.vt == VT_I4 && var.lVal == 3);
  var = pDlg->GetControlProperty(_T("ctl"), (DISPID)2);
  CHECK(var.vt == VT_I4 && var.lVal == 3);
  CComVariant varCount(9);
  pDlg->SetControlProperty(_T("ctl"), _T("Count"), &varCount);
  CHECK(ctl.m_nCount == 9);
  varCount = CComVariant(11);
  pDlg->SetControlProperty(spCtl, (DISPID)2, &varCount);
  CHECK(ctl.m_nCount == 11);

  // The event map: by id, by class, by tag, and the document's.
  CHECK(pDoc->Find(_T("btnGo"))->Connected());
  CHECK(pDoc->Find(_T("label"))->Connected());
  CHECK(pDoc->Find(_T("name"))->Connected() && pDoc->Find(_T("color"), 2)->Connected());
  CHECK(!pDoc->Find(_T("note"))->Connected());
  CHECK(pDoc->m_events.m_pSink != NULL);
  pDoc->Find(_T("btnGo"))->Fire(DISPID_HTMLELEMENTEVENTS2_ONCLICK);
  CHECK(pDlg->m_nGo == 1 && pDlg->m_strLastId == _T("btnGo") && pDlg->m_bHadEvent);
  // After the handler, there is no event.
  CComPtr<IHTMLEventObj> spEvent;
  CHECK(FAILED(pDlg->GetEvent(&spEvent)));
  // "hot big" is in class hot; the button's mouse-over is not its click.
  pDoc->Find(_T("btnGo"))->Fire(DISPID_HTMLELEMENTEVENTS2_ONMOUSEOVER);
  pDoc->Find(_T("label"))->Fire(DISPID_HTMLELEMENTEVENTS2_ONMOUSEOVER);
  CHECK(pDlg->m_nHot == 2 && pDlg->m_nGo == 1 && pDlg->m_strLastId == _T("label"));
  pDoc->Find(_T("rate"))->Fire(DISPID_HTMLELEMENTEVENTS2_ONFOCUS);
  CHECK(pDlg->m_nFocus == 1 && pDlg->m_strLastId == _T("rate"));
  pDoc->Find(_T("rate"))->Fire(DISPID_HTMLELEMENTEVENTS2_ONCLICK);  // not in the map
  CHECK(pDlg->m_nGo == 1 && pDlg->m_nDocClicks == 0);
  // A click on the page: the handler gets where it happened.
  pDoc->Fire(DISPID_HTMLDOCUMENTEVENTS2_ONCLICK, pDoc->Find(_T("note")));
  CHECK(pDlg->m_nDocClicks == 1 && pDlg->m_strLastId == _T("note"));

  // The host: a dialog's look, and window.external only for local pages.
  IDocHostUIHandler* pUI = pBrowser->m_pHostUI;
  CHECK(pUI != NULL);
  if (pUI != NULL) {
    DOCHOSTUIINFO info;
    memset(&info, 0, sizeof(info));
    info.cbSize = sizeof(info);
    CHECK(pUI->GetHostInfo(&info) == S_OK);
    CHECK(info.dwFlags == (DOCHOSTUIFLAG_NO3DBORDER | DOCHOSTUIFLAG_SCROLL_NO | DOCHOSTUIFLAG_DIALOG));
    pDlg->SetHostFlags(DOCHOSTUIFLAG_FLAT_SCROLLBAR);
    CHECK(pUI->GetHostInfo(&info) == S_OK && info.dwFlags == DOCHOSTUIFLAG_FLAT_SCROLLBAR);
    LPDISPATCH pExt = (LPDISPATCH)1;
    CHECK(pUI->GetExternal(&pExt) == E_NOTIMPL && pExt == NULL);  // none set
    CExternal external;
    pDlg->SetExternalDispatch(external.GetIDispatch(FALSE));
    BOOL bLocal = pDlg->CanAccessExternal();
    TestLog("res:// page may reach external: %d", bLocal);
    CHECK(bLocal);
    CHECK(pUI->GetExternal(&pExt) == S_OK && pExt == external.GetIDispatch(FALSE));
    if (pExt != NULL) pExt->Release();
    CHECK(pUI->ShowContextMenu(0, NULL, NULL, NULL) == S_FALSE);
  }

  // Another page: the old page's handlers go, the new page's come.
  int nExchanges = pDlg->m_nExchanges;
  pDlg->Navigate(_T("http://example.test/next"));
  CString strNext;
  pDlg->GetCurrentUrl(strNext);
  CHECK(strNext == _T("http://example.test/next"));
  CHECK(pDlg->m_nExchanges == nExchanges + 1);
  CHECK(pDoc->Find(_T("btnGo"))->Connected());
  CHECK(!pDlg->CanAccessExternal());  // a page from the Internet
  if (pUI != NULL) {
    LPDISPATCH pExt = (LPDISPATCH)1;
    CHECK(pUI->GetExternal(&pExt) == E_NOTIMPL && pExt == NULL);
  }
  pDoc->Find(_T("btnGo"))->Fire(DISPID_HTMLELEMENTEVENTS2_ONCLICK);
  CHECK(pDlg->m_nGo == 2);
  TestSaveWindowBitmap(pDlg->m_hWnd, _T("dhtmldlg.bmp"));

  // Closing lets go of everything.
  pDlg->DestroyWindow();
  CHECK(!pDoc->Find(_T("btnGo"))->Connected() && pDoc->m_events.m_pSink == NULL);
  CHECK(CFakeBrowser::s_nAlive == nBrowsersBefore);
  delete pDlg;
  pDoc->Release();
}

// =====================================================================
// CMultiPageDHtmlDialog and DHTML_EVENT_AXCONTROL

static const IID kDIID_AxEvents = {
    0x6d1f3e20, 0x4b7a, 0x11d0, {0x9a, 0x51, 0x00, 0x60, 0x97, 0x1a, 0x2c, 0x33}};

// An ActiveX control in the page that fires its own events.
class CFakeAxControl final : public IDispatch,
                             public IProvideClassInfo2,
                             public IConnectionPointContainer {
public:
  FAKE_REFCOUNT
  CFakeEvents m_events;
  CFakeAxControl() : m_nRef(1), m_events(static_cast<IDispatch*>(this), kDIID_AxEvents) {
    g_nDomAlive++;
  }
  ~CFakeAxControl() { g_nDomAlive--; }

  HRESULT Fire(DISPID dispid, VARIANT* pArgs, UINT nArgs) {
    if (m_events.m_pSink == NULL) return E_FAIL;
    DISPPARAMS dp = {pArgs, NULL, nArgs, 0};
    return m_events.m_pSink->Invoke(dispid, IID_NULL, 0, DISPATCH_METHOD, &dp, NULL, NULL, NULL);
  }

  STDMETHOD(QueryInterface)(REFIID iid, void** ppv) {
    if (iid == IID_IUnknown || iid == IID_IDispatch)
      *ppv = static_cast<IDispatch*>(this);
    else if (iid == IID_IProvideClassInfo || iid == IID_IProvideClassInfo2)
      *ppv = static_cast<IProvideClassInfo2*>(this);
    else if (iid == IID_IConnectionPointContainer)
      *ppv = static_cast<IConnectionPointContainer*>(this);
    else {
      *ppv = NULL;
      return E_NOINTERFACE;
    }
    AddRef();
    return S_OK;
  }
  FAKE_DISPATCH_INFO
  STDMETHOD(Invoke)(DISPID, REFIID, LCID, WORD, DISPPARAMS*, VARIANT*, EXCEPINFO*, UINT*) {
    return DISP_E_MEMBERNOTFOUND;
  }
  STDMETHOD(GetClassInfo)(ITypeInfo** pp) {
    *pp = NULL;
    return E_NOTIMPL;
  }
  STDMETHOD(GetGUID)(DWORD dwKind, GUID* pGUID) {
    if (dwKind != GUIDKIND_DEFAULT_SOURCE_DISP_IID) return E_INVALIDARG;
    *pGUID = kDIID_AxEvents;
    return S_OK;
  }
  STDMETHOD(EnumConnectionPoints)(IEnumConnectionPoints** pp) {
    *pp = NULL;
    return E_NOTIMPL;
  }
  STDMETHOD(FindConnectionPoint)(REFIID iid, IConnectionPoint** pp) {
    *pp = NULL;
    if (iid != kDIID_AxEvents) return CONNECT_E_NOCONNECTION;
    *pp = &m_events;
    AddRef();
    return S_OK;
  }
};

#define IDC_AX_CHANGE 5
#define IDC_AX_ASK 6

class CTestMultiDlg : public CMultiPageDHtmlDialog {
public:
  int m_nNext, m_nBack, m_nDocClicks;
  long m_nAxValue;
  CString m_strAxText;
  int m_nAsks;

  CTestMultiDlg()
      : CMultiPageDHtmlDialog(IDD_HTMLDLG, IDR_DLGPAGE),
        m_nNext(0),
        m_nBack(0),
        m_nDocClicks(0),
        m_nAxValue(0),
        m_nAsks(0) {}

  HRESULT OnNext(IHTMLElement*) {
    m_nNext++;
    return S_OK;
  }
  HRESULT OnBack(IHTMLElement*) {
    m_nBack++;
    return S_OK;
  }
  HRESULT OnDocClick(IHTMLElement*) {
    m_nDocClicks++;
    return S_OK;
  }
  // The control's events: one __stdcall, as in Visual C++'s examples.
  void __stdcall OnAxChange(long nValue, BSTR bstrText) {
    m_nAxValue = nValue;
    m_strAxText = bstrText;
  }
  void OnAxAsk(VARIANT_BOOL* pbCancel, VARIANT* pvAnswer) {
    m_nAsks++;
    *pbCancel = VARIANT_TRUE;
    ::VariantClear(pvAnswer);
    pvAnswer->vt = VT_BSTR;
    pvAnswer->bstrVal = ::SysAllocString(L"answer");
  }

  // The events of every page.
  DECLARE_DHTML_EVENT_MAP()

  BEGIN_EMBED_DHTML_EVENT_MAP(CTestMultiDlg, page1)
    DHTML_EVENT_ONCLICK(_T("btnGo"), OnNext)
    DHTML_EVENT_AXCONTROL(IDC_AX_CHANGE, _T("ax"), OnAxChange)
    DHTML_EVENT_AXCONTROL(IDC_AX_ASK, _T("ax"), OnAxAsk)
  END_EMBED_DHTML_EVENT_MAP()

  BEGIN_EMBED_DHTML_EVENT_MAP(CTestMultiDlg, page2)
    DHTML_EVENT_ONCLICK(_T("btnBack"), OnBack)
  END_EMBED_DHTML_EVENT_MAP()

  DECLARE_DHTML_URL_EVENT_MAP()
};

BEGIN_DHTML_EVENT_MAP(CTestMultiDlg)
  DHTML_EVENT_ONCLICK(NULL, OnDocClick)
END_DHTML_EVENT_MAP()

BEGIN_DHTML_URL_EVENT_MAP(CTestMultiDlg)
  BEGIN_URL_ENTRIES(CTestMultiDlg)
    URL_EVENT_ENTRY(CTestMultiDlg, IDR_DLGPAGE, page1)
    URL_EVENT_ENTRY(CTestMultiDlg, _T("page2.htm"), page2)
  END_URL_ENTRIES()
END_DHTML_URL_EVENT_MAP(CTestMultiDlg)

// The browser shows another document.
static void ShowDoc(CFakeBrowser* pBrowser, IDispatch* pDoc) {
  if (pBrowser->m_pDocOverride != NULL) pBrowser->m_pDocOverride->Release();
  pBrowser->m_pDocOverride = pDoc;
  pDoc->AddRef();
}

static void RunMultiPageDialog(CWnd* pParent) {
  TestLog("multi-page dhtml dialog");
  CPageControl ctl;
  CFakeHtmlDoc* pDoc1 = MakePage(&ctl);
  CFakeAxControl* pAx = new CFakeAxControl;
  VARIANT v;
  v.vt = VT_DISPATCH;
  v.pdispVal = static_cast<IDispatch*>(pAx);
  pDoc1->Add(_T("OBJECT"), _T("ax"))->SetProp(DISPID_IHTMLOBJECTELEMENT_OBJECT, v);
  CFakeHtmlDoc* pDoc2 = new CFakeHtmlDoc;
  pDoc2->Add(_T("BUTTON"), _T("btnBack"));
  pDoc2->Add(_T("BUTTON"), _T("btnGo"));
  CFakeHtmlDoc* pDoc3 = new CFakeHtmlDoc;
  pDoc3->Add(_T("BUTTON"), _T("btnGo"));
  pDoc3->Add(_T("BUTTON"), _T("btnBack"));

  g_pNextDoc = static_cast<IHTMLDocument2*>(pDoc1);
  CTestMultiDlg* pDlg = new CTestMultiDlg;
  BOOL bMade = pDlg->Create(IDD_HTMLDLG, pParent);
  g_pNextDoc = NULL;
  CHECK(bMade);
  if (bMade) {
    CFakeBrowser* pBrowser = CFakeBrowser::s_pLast;

    // The first page (the resource): its embedded map and the dialog's.
    CHECK(pDoc1->Find(_T("btnGo"))->Connected());
    CHECK(pDoc1->m_events.m_pSink != NULL);
    CHECK(pAx->m_events.m_pSink != NULL);
    pDoc1->Find(_T("btnGo"))->Fire(DISPID_HTMLELEMENTEVENTS2_ONCLICK);
    CHECK(pDlg->m_nNext == 1);
    pDoc1->Fire(DISPID_HTMLELEMENTEVENTS2_ONCLICK, pDoc1->Find(_T("note")));
    CHECK(pDlg->m_nDocClicks == 1);

    // The control's events, with their arguments (last first).
    VARIANT aChange[2];
    aChange[0].vt = VT_BSTR;
    aChange[0].bstrVal = ::SysAllocString(L"hi there");
    aChange[1].vt = VT_I4;
    aChange[1].lVal = 42;
    CHECK(SUCCEEDED(pAx->Fire(IDC_AX_CHANGE, aChange, 2)));
    ::VariantClear(&aChange[0]);
    CHECK(pDlg->m_nAxValue == 42 && pDlg->m_strAxText == _T("hi there"));
    // By reference: what the handler writes comes back.
    VARIANT_BOOL bCancel = VARIANT_FALSE;
    VARIANT vAnswer;
    ::VariantInit(&vAnswer);
    VARIANT aAsk[2];
    aAsk[0].vt = VT_BYREF | VT_VARIANT;
    aAsk[0].pvarVal = &vAnswer;
    aAsk[1].vt = VT_BYREF | VT_BOOL;
    aAsk[1].pboolVal = &bCancel;
    CHECK(SUCCEEDED(pAx->Fire(IDC_AX_ASK, aAsk, 2)));
    CHECK(pDlg->m_nAsks == 1 && bCancel == VARIANT_TRUE);
    CHECK(vAnswer.vt == VT_BSTR && wcscmp(vAnswer.bstrVal, L"answer") == 0);
    ::VariantClear(&vAnswer);
    // An event the map does not name.
    CHECK(SUCCEEDED(pAx->Fire(99, NULL, 0)));

    // The second page: its own map, not the first's.
    ShowDoc(pBrowser, static_cast<IHTMLDocument2*>(pDoc2));
    pDlg->Navigate(_T("res://test.exe/page2.htm"));
    CString strUrl;
    pDlg->GetCurrentUrl(strUrl);
    CHECK(strUrl == _T("res://test.exe/page2.htm"));
    CHECK(!pDoc1->Find(_T("btnGo"))->Connected() && pDoc1->m_events.m_pSink == NULL);
    CHECK(pAx->m_events.m_pSink == NULL);
    CHECK(pDoc2->Find(_T("btnBack"))->Connected() && !pDoc2->Find(_T("btnGo"))->Connected());
    CHECK(pDoc2->m_events.m_pSink != NULL);
    pDoc2->Find(_T("btnBack"))->Fire(DISPID_HTMLELEMENTEVENTS2_ONCLICK);
    CHECK(pDlg->m_nBack == 1 && pDlg->m_nNext == 1);
    pDoc2->Fire(DISPID_HTMLELEMENTEVENTS2_ONCLICK, pDoc2->Find(_T("btnBack")));
    CHECK(pDlg->m_nDocClicks == 2);

    // A page the URL map does not name: only the dialog's own map.
    ShowDoc(pBrowser, static_cast<IHTMLDocument2*>(pDoc3));
    pDlg->Navigate(_T("http://example.test/other.htm#top"));
    CHECK(!pDoc2->Find(_T("btnBack"))->Connected());
    CHECK(!pDoc3->Find(_T("btnGo"))->Connected() && !pDoc3->Find(_T("btnBack"))->Connected());
    CHECK(pDoc3->m_events.m_pSink != NULL);

    // Back to the first page by its resource URL.
    ShowDoc(pBrowser, static_cast<IHTMLDocument2*>(pDoc1));
    pDlg->LoadFromResource(IDR_DLGPAGE);
    CHECK(pDoc1->Find(_T("btnGo"))->Connected() && pAx->m_events.m_pSink != NULL);
    CHECK(pDoc3->m_events.m_pSink == NULL);

    pDlg->DestroyWindow();
    CHECK(pAx->m_events.m_pSink == NULL && !pDoc1->Find(_T("btnGo"))->Connected());
  }
  delete pDlg;
  pDoc1->Release();
  pDoc2->Release();
  pDoc3->Release();
  pAx->Release();
}

// A CDHtmlDialog with no map of its own.
static void RunPlainDHtmlDialog(CWnd* pParent) {
  TestLog("plain dhtml dialog");
  CPageControl ctl;
  CFakeHtmlDoc* pDoc = MakePage(&ctl);
  g_pNextDoc = static_cast<IHTMLDocument2*>(pDoc);
  CDHtmlDialog* pDlg = new CDHtmlDialog(IDD_HTMLDLG, IDR_DLGPAGE);
  BOOL bMade = pDlg->Create(IDD_HTMLDLG, pParent);
  g_pNextDoc = NULL;
  CHECK(bMade);
  if (bMade) {
    CHECK(pDlg->m_spHtmlDoc == static_cast<IHTMLDocument2*>(pDoc));
    CHECK(!pDoc->Find(_T("btnGo"))->Connected() && pDoc->m_events.m_pSink == NULL);
    pDlg->DestroyWindow();
  }
  delete pDlg;
  pDoc->Release();
}

void TestDHtmlDialog(CWnd* pParent) {
  RunDHtmlDialog(pParent);
  RunMultiPageDialog(pParent);
  RunPlainDHtmlDialog(pParent);
  // Nothing kept the page's objects.
  CHECK(g_nDomAlive == 0);
}

// =====================================================================
// HTML editing: CHtmlEditView, CHtmlEditDoc and CHtmlEditCtrl

#define ID_TEST_BOLD 32901
#define ID_TEST_MYBOLD 32902
#define ID_TEST_IMAGE 32903

class CTestEditView : public CHtmlEditView {
public:
  int m_nMyBold;
  UINT m_nImageID;
  CTestEditView() : m_nMyBold(0), m_nImageID(0) {}
  void OnMyBold() { m_nMyBold++; }
  void OnImage(UINT nID) { m_nImageID = nID; }
  DECLARE_DHTMLEDITING_CMDMAP(CTestEditView)
};

BEGIN_DHTMLEDITING_CMDMAP(CTestEditView)
  DHTMLEDITING_CMD_ENTRY_TYPE(ID_TEST_BOLD, IDM_BOLD, AFX_UI_ELEMTYPE_CHECKBOX)
  DHTMLEDITING_CMD_ENTRY_FUNC(ID_TEST_MYBOLD, IDM_BOLD, OnMyBold)
  DHTMLEDITING_CMD_ENTRY_FUNC_TYPE(ID_TEST_IMAGE, IDM_IMAGE, OnImage, AFX_UI_ELEMTYPE_RADIO)
END_DHTMLEDITING_CMDMAP()

// What an update handler did.
class CTestCmdUI : public CCmdUI {
public:
  BOOL m_bEnabled;
  int m_nCheck;
  int m_nRadio;
  CTestCmdUI(UINT nID) : m_bEnabled(-1), m_nCheck(-1), m_nRadio(-1) {
    m_nID = nID;
    m_nIndexMax = 1;
  }
  virtual void Enable(BOOL bOn) {
    m_bEnabled = bOn;
    m_bEnableChanged = TRUE;
  }
  virtual void SetCheck(int nCheck) { m_nCheck = nCheck; }
  virtual void SetRadio(BOOL bOn) { m_nRadio = bOn; }
};

static CTestCmdUI UpdateOf(CView* pView, UINT nID) {
  CTestCmdUI ui(nID);
  ui.DoUpdate(pView, FALSE);
  return ui;
}

static BOOL LastIn(CFakeHtmlDoc* pDoc, LPCWSTR lpsz) {
  return pDoc->m_varLastIn.vt == VT_BSTR && wcscmp(pDoc->m_varLastIn.bstrVal, lpsz) == 0;
}

static void TestHtmlEditView(CWnd* pParent) {
  TestLog("html edit view");
  CFakeHtmlDoc* pDoc = new CFakeHtmlDoc;
  pDoc->m_strSource = L"<p>hi</p>";
  pDoc->m_strTitle = _T("Edit Me");
  g_pNextDoc = static_cast<IHTMLDocument2*>(pDoc);
  int nBrowsersBefore = CFakeBrowser::s_nAlive;
  CHtmlEditDoc* pEditDoc = new CHtmlEditDoc;
  pEditDoc->m_bAutoDelete = FALSE;  // the test deletes it
  CTestEditView* pView = new CTestEditView;
  BOOL bMade = pView->Create(NULL, NULL, WS_CHILD | WS_VISIBLE, CRect(0, 0, 200, 120), pParent,
                             0xE901, NULL);
  g_pNextDoc = NULL;
  CHECK(bMade);
  if (!bMade) {
    delete pEditDoc;
    pDoc->Release();
    return;
  }
  pEditDoc->AddView(pView);
  CFakeBrowser* pBrowser = CFakeBrowser::s_pLast;

  // The start page, in design mode.
  CHECK(pBrowser->m_strURL == _T("about:blank"));
  CHECK(pDoc->m_strDesignMode == _T("On"));
  CComPtr<IHTMLDocument2> spDoc;
  CHECK(pView->GetDHtmlDocument(&spDoc) && spDoc == static_cast<IHTMLDocument2*>(pDoc));
  CHECK(pView->GetDocument() == pEditDoc && pEditDoc->GetView() == pView);
  CString str;
  CHECK(SUCCEEDED(pView->GetDocumentTitle(str)) && str == _T("Edit Me"));
  CHECK(pView->SetDesignMode(FALSE) && pDoc->m_strDesignMode == _T("Off"));
  pView->SetDesignMode(TRUE);

  // Commands: plain, with a string, a number and a flag.
  CHECK(SUCCEEDED(pView->Bold()));
  CHECK(pDoc->m_nLastCmd == IDM_BOLD && pDoc->m_nLastOpt == OLECMDEXECOPT_DODEFAULT &&
        pDoc->m_varLastIn.vt == VT_EMPTY);
  CHECK(SUCCEEDED(pView->SetFontFace(_T("Verdana"))));
  CHECK(pDoc->m_nLastCmd == IDM_FONTNAME && pDoc->m_nLastOpt == OLECMDEXECOPT_DONTPROMPTUSER &&
        LastIn(pDoc, L"Verdana"));
  CHECK(SUCCEEDED(pView->SetFontSize(5)));
  CHECK(pDoc->m_nLastCmd == IDM_FONTSIZE && pDoc->m_varLastIn.vt == VT_I4 &&
        pDoc->m_varLastIn.lVal == 5);
  CHECK(SUCCEEDED(pView->ShowBorders(true)));
  CHECK(pDoc->m_nLastCmd == IDM_SHOWZEROBORDERATDESIGNTIME && pDoc->m_varLastIn.vt == VT_BOOL &&
        pDoc->m_varLastIn.boolVal == VARIANT_TRUE);
  CHECK(SUCCEEDED(pView->Image()));  // no URL: MSHTML asks
  CHECK(pDoc->m_nLastCmd == IDM_IMAGE && pDoc->m_nLastOpt == OLECMDEXECOPT_DODEFAULT &&
        pDoc->m_varLastIn.vt == VT_EMPTY);
  CHECK(SUCCEEDED(pView->SetHyperLink(_T("http://example.test/"))));
  CHECK(pDoc->m_nLastCmd == IDM_HYPERLINK && LastIn(pDoc, L"http://example.test/"));
  CHECK(SUCCEEDED(pView->SetForeColor(0x00ff00)) && pDoc->m_nLastCmd == IDM_FORECOLOR);
  // Another command group goes to the document as given.
  static const GUID kOther = {1, 2, 3, {4, 5, 6, 7, 8, 9, 10, 11}};
  CHECK(pView->ExecCommand(&kOther, IDM_BOLD, OLECMDEXECOPT_DODEFAULT) ==
        OLECMDERR_E_UNKNOWNGROUP);

  // The current values.
  CHECK(SUCCEEDED(pView->GetFontFace(str)) && str == _T("Arial"));
  short nSize = 0;
  CHECK(SUCCEEDED(pView->GetFontSize(nSize)) && nSize == 3);
  int nColor = 0;
  CHECK(SUCCEEDED(pView->GetForeColor(nColor)) && nColor == 0xff);
  CHECK(pView->GetBackColor(nColor) == S_FALSE && nColor == 0);  // more than one
  CHECK(SUCCEEDED(pView->GetBlockFormat(str)) && str == _T("Normal"));
  CStringArray saFormats;
  CHECK(SUCCEEDED(pView->GetBlockFormatNames(saFormats)) && saFormats.GetSize() == 2 &&
        saFormats[1] == _T("Heading 1"));
  CHECK((pView->QueryStatus(IDM_PASTE) & OLECMDF_ENABLED) == 0);
  CHECK((pView->QueryStatus(IDM_COPY) & OLECMDF_ENABLED) != 0);

  // The page's text.
  CHECK(SUCCEEDED(pView->GetDocumentHTML(str)) && str == _T("<p>hi</p>"));
  CHECK(SUCCEEDED(pView->SetDocumentHTML(_T("<p>new</p>"))));
  CHECK(pDoc->m_strSource == L"<p>new</p>");
  CHECK(pView->IsDirty() == S_OK && pEditDoc->IsModified());
  pDoc->m_bDirty = FALSE;
  CHECK(pView->IsDirty() == S_FALSE && !pEditDoc->IsModified());

  // The editing command map: CHtmlEditView's and the class's own.
  CTestCmdUI uiPaste = UpdateOf(pView, ID_EDIT_PASTE);
  CHECK(uiPaste.m_bEnabled == FALSE);
  CTestCmdUI uiCopy = UpdateOf(pView, ID_EDIT_COPY);
  CHECK(uiCopy.m_bEnabled == TRUE && uiCopy.m_nCheck == -1);
  pDoc->m_bBold = FALSE;
  CHECK(UpdateOf(pView, ID_TEST_BOLD).m_nCheck == 0);
  pDoc->m_bBold = TRUE;
  CHECK(UpdateOf(pView, ID_TEST_BOLD).m_nCheck == 1);
  CTestCmdUI uiImage = UpdateOf(pView, ID_TEST_IMAGE);
  CHECK(uiImage.m_bEnabled == TRUE && uiImage.m_nRadio == FALSE);
  pView->SendMessage(WM_COMMAND, ID_EDIT_COPY);
  CHECK(pDoc->m_nLastCmd == IDM_COPY);
  pView->SendMessage(WM_COMMAND, ID_TEST_BOLD);
  CHECK(pDoc->m_nLastCmd == IDM_BOLD && pDoc->m_bBold == FALSE);
  // A handler instead of the command.
  int nCmds = pDoc->m_nCmds;
  pView->SendMessage(WM_COMMAND, ID_TEST_MYBOLD);
  CHECK(pView->m_nMyBold == 1 && pDoc->m_nCmds == nCmds);
  pView->SendMessage(WM_COMMAND, ID_TEST_IMAGE);
  CHECK(pView->m_nImageID == ID_TEST_IMAGE && pDoc->m_nCmds == nCmds);

  // CHtmlEditDoc: saving writes the text and clears the change.
  TCHAR szTemp[MAX_PATH], szFile[MAX_PATH];
  ::GetTempPath(MAX_PATH, szTemp);
  ::GetTempFileName(szTemp, _T("hte"), 0, szFile);
  pDoc->m_bDirty = TRUE;
  CHECK(pEditDoc->OnSaveDocument(szFile));
  CHECK(pDoc->m_nLastCmd == IDM_SETDIRTY && !pDoc->m_bDirty && !pEditDoc->IsModified());
  CFile file;
  if (file.Open(szFile, CFile::modeRead)) {
    char buf[64] = {0};
    UINT n = file.Read(buf, sizeof(buf) - 1);
    file.Close();
    CHECK(n == 13 && memcmp(buf, "\xEF\xBB\xBF<p>new</p>", 13) == 0);
  } else {
    CHECK(!"saved file");
  }
  ::DeleteFile(szFile);
  // A new document starts empty.
  int nInit = pDoc->m_nInitNew;
  CHECK(pEditDoc->OnNewDocument());
  CHECK(pDoc->m_nInitNew == nInit + 1 && pDoc->m_strSource.IsEmpty());
  // Opening one is navigating to it.
  CHECK(pEditDoc->OpenURL(_T("http://example.test/edit")));
  CHECK(pBrowser->m_strURL == _T("http://example.test/edit"));

  pView->DestroyWindow();  // the view deletes itself
  CHECK(CFakeBrowser::s_nAlive == nBrowsersBefore);
  CHECK(pEditDoc->GetView() == NULL);
  delete pEditDoc;
  pDoc->Release();
}

static void TestHtmlEditCtrl(CWnd* pParent) {
  TestLog("html edit control");
  CFakeHtmlDoc* pDoc = new CFakeHtmlDoc;
  g_pNextDoc = static_cast<IHTMLDocument2*>(pDoc);
  int nBrowsersBefore = CFakeBrowser::s_nAlive;
  CHtmlEditCtrl* pCtl = new CHtmlEditCtrl;
  BOOL bMade = pCtl->Create(NULL, WS_CHILD | WS_VISIBLE, CRect(0, 0, 160, 90), pParent, 0x1234);
  g_pNextDoc = NULL;
  CHECK(bMade);
  if (bMade) {
    CFakeBrowser* pBrowser = CFakeBrowser::s_pLast;
    CHECK(pBrowser->m_strURL == _T("about:blank"));
    // Its own connection to the browser put the page in design mode.
    CHECK(pBrowser->m_pSink2 != NULL);
    CHECK(pDoc->m_strDesignMode == _T("On"));
    CComPtr<IHTMLDocument2> spDoc;
    CHECK(pCtl->GetDHtmlDocument(&spDoc) && spDoc == static_cast<IHTMLDocument2*>(pDoc));
    spDoc.Release();
    CHECK(SUCCEEDED(pCtl->Italic()) && pDoc->m_nLastCmd == IDM_ITALIC);
    CHECK(SUCCEEDED(pCtl->Bookmark(_T("here"))) && pDoc->m_nLastCmd == IDM_BOOKMARK &&
          LastIn(pDoc, L"here"));
    pCtl->DestroyWindow();
    CHECK(CFakeBrowser::s_nAlive == nBrowsersBefore);
  }
  delete pCtl;
  pDoc->Release();
}

// =====================================================================
// The frame and the steps

class CMainFrame : public CFrameWnd {
public:
  CTestHtmlView* GetView() { return (CTestHtmlView*)GetDlgItem(AFX_IDW_PANE_FIRST); }
  void RunTests();
  void TestCreate(CTestHtmlView* pView, CFakeBrowser* pBrowser);
  void TestNavigate(CTestHtmlView* pView, CFakeBrowser* pBrowser);
  void TestProperties(CTestHtmlView* pView, CFakeBrowser* pBrowser);
  void TestDocument(CTestHtmlView* pView, CFakeBrowser* pBrowser);
  void TestHostUI(CTestHtmlView* pView, CFakeBrowser* pBrowser);

  afx_msg int OnCreate(LPCREATESTRUCT lpcs) {
    if (CFrameWnd::OnCreate(lpcs) == -1) return -1;
    if (!m_wndStatusBar.Create(this)) return -1;
    static const UINT indicators[] = {ID_SEPARATOR};
    m_wndStatusBar.SetIndicators(indicators, 1);
    SetTimer(1, 100, NULL);
    return 0;
  }
  afx_msg void OnTimer(UINT_PTR) {
    KillTimer(1);
    RunTests();
    PostMessage(WM_CLOSE);
  }
  CStatusBar m_wndStatusBar;
  DECLARE_MESSAGE_MAP()
};

BEGIN_MESSAGE_MAP(CMainFrame, CFrameWnd)
  ON_WM_CREATE()
  ON_WM_TIMER()
END_MESSAGE_MAP()

static void PumpMessages() {
  MSG msg;
  while (::PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
    ::TranslateMessage(&msg);
    ::DispatchMessage(&msg);
  }
}

void CMainFrame::TestCreate(CTestHtmlView* pView, CFakeBrowser* pBrowser) {
  TestLog("create");
  CHECK(pView->IsKindOf(RUNTIME_CLASS(CHtmlView)) && pView->IsKindOf(RUNTIME_CLASS(CFormView)));
  CHECK(pView->m_pBrowserApp == static_cast<IWebBrowser2*>(pBrowser));
  CHECK(pView->m_wndBrowser.m_hWnd != NULL && pView->m_wndBrowser.m_hWnd == pBrowser->m_hWnd);
  CHECK(::GetParent(pBrowser->m_hWnd) == pView->m_hWnd);
  CHECK(pView->m_wndBrowser.GetDlgCtrlID() == AFX_IDW_PANE_FIRST);
  CHECK(pView->m_wndBrowser.GetControlUnknown() != NULL);
  // The site answers the browser's IDocHostUIHandler.
  CHECK(pBrowser->m_pHostUI != NULL);
  // Its events come to the view.
  CHECK(pBrowser->m_pSink != NULL);

  // The browser fills the view, and follows it.
  CRect rcView, rcBrowser;
  pView->GetClientRect(&rcView);
  ::GetWindowRect(pBrowser->m_hWnd, &rcBrowser);
  pView->ScreenToClient(&rcBrowser);
  CHECK(rcBrowser == rcView);
  CRect rcFrame;
  GetWindowRect(&rcFrame);
  SetWindowPos(NULL, 0, 0, rcFrame.Width() + 40, rcFrame.Height() + 30,
               SWP_NOMOVE | SWP_NOZORDER);
  PumpMessages();
  pView->GetClientRect(&rcView);
  ::GetWindowRect(pBrowser->m_hWnd, &rcBrowser);
  pView->ScreenToClient(&rcBrowser);
  CHECK(rcBrowser == rcView);
  TestLog("view %dx%d, browser %dx%d", rcView.Width(), rcView.Height(), rcBrowser.Width(),
          rcBrowser.Height());
}

void CMainFrame::TestNavigate(CTestHtmlView* pView, CFakeBrowser* pBrowser) {
  TestLog("navigate");
  // Navigate: a URL, and its arguments.
  pView->m_strLog.Empty();
  pView->Navigate(_T("http://example.test/a"), navNoHistory, _T("frame1"),
                  _T("X-Test: 1\r\n"), (LPVOID) "q=1", 3);
  CHECK(pBrowser->m_strLastCall == _T("Navigate"));
  CHECK(pBrowser->m_strURL == _T("http://example.test/a"));
  CHECK(pBrowser->m_nNavFlags == navNoHistory);
  CHECK(pBrowser->m_strTarget == _T("frame1"));
  CHECK(pBrowser->m_strHeaders == _T("X-Test: 1\r\n"));
  CHECK(pBrowser->m_baPost.GetSize() == 3 && memcmp(pBrowser->m_baPost.GetData(), "q=1", 3) == 0);
  // The events, in the browser's order, with the VARIANTs made strings.
  CHECK(pView->m_strLog ==
        _T("before;dlbegin;complete http://example.test/a;title Title of http://example.test/a;")
        _T("status Done;prop Prop;dlend;doc http://example.test/a;"));
  TestLog("log: %s", (LPCSTR)CStringA(pView->m_strLog));
  CHECK(pView->m_strBeforeURL == _T("http://example.test/a"));
  CHECK(pView->m_nBeforeFlags == navNoHistory);
  CHECK(pView->m_strBeforeTarget == _T("frame1"));
  CHECK(pView->m_strBeforeHeaders == _T("X-Test: 1\r\n"));
  CHECK(pView->m_baBeforePost.GetSize() == 3 &&
        memcmp(pView->m_baBeforePost.GetData(), "q=1", 3) == 0);
  CHECK(pView->m_nProgress == 100 && pView->m_nProgressMax == 100);
  CHECK(pView->m_nCommand == CSC_NAVIGATEBACK && pView->m_bCommandEnabled);
  CHECK(!pBrowser->m_bCancelled);
  CHECK(pView->GetLocationURL() == _T("http://example.test/a"));
  CHECK(pView->GetLocationName() == _T("Title of http://example.test/a"));
  // The status text went on the frame's status bar.
  CString strStatus;
  m_wndStatusBar.GetPaneText(0, strStatus);
  CHECK(strStatus == _T("Done"));

  // Navigate2 with a URL; no post data is VT_EMPTY.
  pView->m_strLog.Empty();
  pView->Navigate2(_T("http://example.test/b"));
  CHECK(pBrowser->m_strLastCall == _T("Navigate2"));
  CHECK(pBrowser->m_vtURL == VT_BSTR && pBrowser->m_strURL == _T("http://example.test/b"));
  CHECK(pBrowser->m_nNavFlags == 0 && pBrowser->m_baPost.GetSize() == 0);
  CHECK(pView->m_baBeforePost.GetSize() == 0 && pView->m_strBeforeTarget.IsEmpty());

  // Navigate2 with posted data in a CByteArray.
  CByteArray ba;
  ba.Add('x');
  ba.Add('=');
  ba.Add('9');
  ba.Add(0);
  pView->Navigate2(_T("http://example.test/c"), navOpenInNewWindow, ba, _T("_top"),
                   _T("Content-Type: text/plain\r\n"));
  CHECK(pBrowser->m_nNavFlags == navOpenInNewWindow);
  CHECK(pBrowser->m_baPost.GetSize() == 4 && memcmp(pBrowser->m_baPost.GetData(), "x=9", 4) == 0);
  CHECK(pBrowser->m_strTarget == _T("_top"));
  CHECK(pView->m_baBeforePost.GetSize() == 4);
  CHECK(pView->m_strBeforeHeaders == _T("Content-Type: text/plain\r\n"));

  // Navigate2 with an item list: its bytes and the terminator.
  BYTE idl[3 + 5 + 2] = {3, 0, 'a', 5, 0, 'b', 'c', 'd', 0, 0};
  pView->Navigate2((LPITEMIDLIST)idl, 0, NULL);
  CHECK(pBrowser->m_vtURL == (VT_ARRAY | VT_UI1));
  CHECK(pBrowser->m_cbURL == sizeof(idl));

  // A handler that cancels.
  pView->m_strLog.Empty();
  pView->Navigate2(_T("http://example.test/blocked"));
  CHECK(pBrowser->m_bCancelled);
  CHECK(pView->m_strLog == _T("before;"));
  CHECK(pView->GetLocationURL() == _T("http://example.test/c") ||
        pView->GetLocationURL() == _T("pidl:"));

  // A page that is not there.
  pView->m_strLog.Empty();
  pView->Navigate2(_T("bad:page"));
  CHECK(pView->m_strLog == _T("before;error;"));
  CHECK(pView->m_strError == _T("bad:page|main"));
  CHECK(pView->m_dwError == 404);
  CHECK(pBrowser->m_bCancelled);

  // A page in the program's resources.
  CString strExe;
  ::GetModuleFileName(NULL, strExe.GetBuffer(MAX_PATH), MAX_PATH);
  strExe.ReleaseBuffer();
  CHECK(pView->LoadFromResource(IDR_PAGE));
  CString strWant;
  strWant.Format(_T("res://%s/%d"), (LPCTSTR)strExe, IDR_PAGE);
  CHECK(pBrowser->m_strURL == strWant);
  CHECK(pView->LoadFromResource(_T("PAGE.HTM")));
  CHECK(pBrowser->m_strURL == _T("res://") + strExe + _T("/PAGE.HTM"));

  // The rest of the moves.
  pView->GoBack();
  CHECK(pBrowser->m_strLastCall == _T("GoBack"));
  pView->GoForward();
  CHECK(pBrowser->m_strLastCall == _T("GoForward"));
  pView->GoHome();
  CHECK(pBrowser->m_strLastCall == _T("GoHome"));
  pView->GoSearch();
  CHECK(pBrowser->m_strLastCall == _T("GoSearch"));
  pView->Refresh();
  CHECK(pBrowser->m_strLastCall == _T("Refresh"));
  pView->Refresh2(REFRESH_COMPLETELY);
  CHECK(pBrowser->m_strLastCall == _T("Refresh2 3"));
  pView->Stop();
  CHECK(pBrowser->m_strLastCall == _T("Stop"));

  // NewWindow2: the handler's cancel goes back by reference.
  pView->m_strLog.Empty();
  CHECK(pBrowser->FireNewWindow());
  CHECK(pView->m_strLog == _T("newwindow;"));
}

void CMainFrame::TestProperties(CTestHtmlView* pView, CFakeBrowser* pBrowser) {
  TestLog("properties");
  CHECK(pView->GetType() == _T("HTML Document"));
  CHECK(pView->GetFullName() == _T("C:\\fake\\browser.exe"));
  CHECK(pView->GetReadyState() == READYSTATE_COMPLETE);
  CHECK(pView->GetTopLevelContainer());
  CHECK(!pView->GetBusy());
  pView->SetLeft(11);
  pView->SetTop(22);
  pView->SetWidth(333);
  pView->SetHeight(444);
  CHECK(pView->GetLeft() == 11 && pView->GetTop() == 22);
  CHECK(pView->GetWidth() == 333 && pView->GetHeight() == 444);
  CHECK(pBrowser->m_nWidth == 333);

  // The flags, both ways.
  pView->SetOffline(TRUE);
  CHECK(pView->GetOffline() && pBrowser->m_bOffline);
  pView->SetSilent(TRUE);
  CHECK(pView->GetSilent() && pBrowser->m_bSilent);
  pView->SetMenuBar(FALSE);
  CHECK(!pView->GetMenuBar());
  pView->SetStatusBar(FALSE);
  CHECK(!pView->GetStatusBar());
  pView->SetFullScreen(TRUE);
  CHECK(pView->GetFullScreen());
  pView->SetRegisterAsBrowser(TRUE);
  CHECK(pView->GetRegisterAsBrowser());
  pView->SetRegisterAsDropTarget(FALSE);
  CHECK(!pView->GetRegisterAsDropTarget());
  pView->SetAddressBar(FALSE);
  CHECK(!pView->GetAddressBar());
  pView->SetToolBar(0);
  CHECK(pView->GetToolBar() == 0);

  // Setting some fires their events.
  pView->m_nVisibleEvents = 0;
  pView->SetVisible(FALSE);
  CHECK(!pView->GetVisible());
  CHECK(pView->m_nVisibleEvents == 1 && !pView->m_bLastVisible);
  pView->SetVisible(TRUE);
  CHECK(pView->m_nVisibleEvents == 2 && pView->m_bLastVisible);
  pView->m_strLog.Empty();
  pView->SetTheaterMode(TRUE);
  CHECK(pView->GetTheaterMode());
  CHECK(pView->m_strLog == _T("theater on;"));

  // The objects.
  LPDISPATCH pApp = pView->GetApplication();
  CHECK(pApp == static_cast<IDispatch*>(static_cast<IWebBrowser2*>(pBrowser)));
  if (pApp != NULL) pApp->Release();
  LPDISPATCH pParent = pView->GetParentBrowser();
  CHECK(pParent != NULL);
  if (pParent != NULL) pParent->Release();
  CHECK(pView->GetContainer() == NULL);

  // Properties the page keeps.
  pView->PutProperty(_T("name"), _T("value"));
  CString str;
  CHECK(pView->GetProperty(_T("name"), str) && str == _T("value"));
  pView->PutProperty(_T("count"), 42L);
  COleVariant var = pView->GetProperty(_T("count"));
  CHECK(var.vt == VT_I4 && var.lVal == 42);
  pView->PutProperty(_T("small"), (short)7);
  var = pView->GetProperty(_T("small"));
  CHECK(var.vt == VT_I2 && var.iVal == 7);
  pView->PutProperty(_T("real"), 2.5);
  var = pView->GetProperty(_T("real"));
  CHECK(var.vt == VT_R8 && var.dblVal == 2.5);
  CHECK(pView->GetProperty(_T("count"), str) && str == _T("42"));
  CHECK(!pView->GetProperty(_T("missing"), str) && str.IsEmpty());

  // Commands.
  CHECK(pView->QueryStatusWB(OLECMDID_PRINT) == (OLECMDF_SUPPORTED | OLECMDF_ENABLED));
  CHECK(pView->QueryStatusWB(OLECMDID_COPY) == 0);
  pView->ExecWB(OLECMDID_COPY, OLECMDEXECOPT_DONTPROMPTUSER, NULL, NULL);
  CHECK(pBrowser->m_nLastExecWB == OLECMDID_COPY &&
        pBrowser->m_nLastExecOpt == OLECMDEXECOPT_DONTPROMPTUSER);
  // File Print goes to the browser.
  pView->SendMessage(WM_COMMAND, ID_FILE_PRINT);
  CHECK(pBrowser->m_nLastExecWB == OLECMDID_PRINT &&
        pBrowser->m_nLastExecOpt == OLECMDEXECOPT_DODEFAULT);
}

void CMainFrame::TestDocument(CTestHtmlView* pView, CFakeBrowser* pBrowser) {
  TestLog("document");
  LPDISPATCH pDoc = pView->GetHtmlDocument();
  CHECK(pDoc != NULL);
  if (pDoc != NULL) pDoc->Release();

  // The page's source, from UTF-8.
  CString strSource;
  CHECK(pView->GetSource(strSource));
#ifdef _UNICODE
  CHECK(strSource == L"<html><body>Caf\u00e9</body></html>");
#else
  CHECK(strSource.Left(15) == "<html><body>Caf" && strSource.Right(14) == "</body></html>");
#endif

  // The forms commands.
  BOOL bSupported = FALSE, bEnabled = FALSE, bChecked = FALSE;
  CHECK(SUCCEEDED(pView->QueryFormsCommand(7, &bSupported, &bEnabled, &bChecked)));
  CHECK(bSupported && bEnabled && bChecked);
  CHECK(SUCCEEDED(pView->QueryFormsCommand(8, &bSupported, &bEnabled, &bChecked)));
  CHECK(!bSupported && !bEnabled && !bChecked);
  VARIANT vIn, vOut;
  ::VariantInit(&vIn);
  ::VariantInit(&vOut);
  vIn.vt = VT_I4;
  vIn.lVal = 1;
  CHECK(SUCCEEDED(pView->ExecFormsCommand(7, &vIn, &vOut)));
  CHECK(pBrowser->m_pDoc->m_nLastExec == 7);
  CHECK(vOut.vt == VT_I4 && vOut.lVal == 15);
}

void CMainFrame::TestHostUI(CTestHtmlView* pView, CFakeBrowser* pBrowser) {
  TestLog("host UI");
  IDocHostUIHandler* pUI = pBrowser->m_pHostUI;
  if (pUI == NULL) return;
  DOCHOSTUIINFO info;
  memset(&info, 0, sizeof(info));
  info.cbSize = sizeof(info);
  CHECK(pUI->GetHostInfo(&info) == S_OK);
  CHECK(info.dwFlags == (DOCHOSTUIFLAG_NO3DBORDER | DOCHOSTUIFLAG_SCROLL_NO));
  POINT pt = {5, 5};
  CHECK(pUI->ShowContextMenu(CONTEXT_MENU_DEFAULT, &pt, NULL, NULL) == S_OK);
  CHECK(pView->m_nContextMenus == 1);
  // window.external is the view's object.
  LPDISPATCH pExt = NULL;
  CHECK(pUI->GetExternal(&pExt) == S_OK && pExt == pView->m_external.GetIDispatch(FALSE));
  if (pExt != NULL) pExt->Release();
  OLECHAR* pszOut = NULL;
  CHECK(pUI->TranslateUrl(0, (OLECHAR*)L"old:", &pszOut) == S_OK && pszOut != NULL &&
        wcscmp(pszOut, L"new:") == 0);
  if (pszOut != NULL) ::CoTaskMemFree(pszOut);
  CHECK(pUI->TranslateUrl(0, (OLECHAR*)L"same:", &pszOut) == S_FALSE && pszOut == NULL);
  // The defaults leave the browser to do its own thing.
  MSG msg;
  memset(&msg, 0, sizeof(msg));
  CHECK(pUI->TranslateAccelerator(&msg, NULL, 0) == S_FALSE);
  CHECK(pUI->ShowUI(0, NULL, NULL, NULL, NULL) == S_FALSE);
  CHECK(pUI->HideUI() == E_NOTIMPL);
  LPDATAOBJECT pData = (LPDATAOBJECT)1;
  CHECK(pUI->FilterDataObject(NULL, &pData) == S_FALSE && pData == NULL);
  // It is the site's: the same object as the client site.
  IUnknown* pUnk1 = NULL;
  IUnknown* pUnk2 = NULL;
  pUI->QueryInterface(IID_IUnknown, (void**)&pUnk1);
  pBrowser->m_pClientSite->QueryInterface(IID_IUnknown, (void**)&pUnk2);
  CHECK(pUnk1 != NULL && pUnk1 == pUnk2);
  if (pUnk1 != NULL) pUnk1->Release();
  if (pUnk2 != NULL) pUnk2->Release();

  // The page has its keys before the frame's.
  MSG key;
  memset(&key, 0, sizeof(key));
  key.hwnd = pBrowser->m_hWnd;
  key.message = WM_KEYDOWN;
  key.wParam = VK_F5;
  CHECK(pView->PreTranslateMessage(&key));
  CHECK(pBrowser->m_nAccelerators == 1);
  key.message = WM_MOUSEMOVE;
  pView->PreTranslateMessage(&key);
  CHECK(pBrowser->m_nAccelerators == 1);
}

void CMainFrame::RunTests() {
  CTestHtmlView* pView = GetView();
  CFakeBrowser* pBrowser = CFakeBrowser::s_pLast;
  CHECK(pView != NULL && pBrowser != NULL && CFakeBrowser::s_nAlive == 1);
  if (pView == NULL || pBrowser == NULL) return;
  TestCreate(pView, pBrowser);
  TestNavigate(pView, pBrowser);
  TestProperties(pView, pBrowser);
  TestDocument(pView, pBrowser);
  TestHostUI(pView, pBrowser);
  TestDHtmlDialog(this);
  TestHtmlEditView(this);
  TestHtmlEditCtrl(this);
  pView->Navigate2(_T("http://example.test/shown"));
  ::InvalidateRect(pBrowser->m_hWnd, NULL, TRUE);
  PumpMessages();
  TestSaveWindowBitmap(m_hWnd, _T("htmltest.bmp"));
}

// =====================================================================

class CTestApp : public CWinApp {
public:
  DWORD m_dwRegister;
  CTestApp() : m_dwRegister(0) {}
  virtual BOOL InitInstance() {
    TestOpenLog(_T("htmltest.log"));
    if (!AfxOleInit()) {
      TestLog("AfxOleInit failed");
      return FALSE;
    }
    AfxEnableControlContainer();
    WNDCLASS wc;
    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = BrowserWndProc;
    wc.hInstance = AfxGetInstanceHandle();
    wc.hCursor = ::LoadCursor(NULL, IDC_ARROW);
    wc.lpszClassName = kBrowserClass;
    ::RegisterClass(&wc);
    HRESULT hr = ::CoRegisterClassObject(CLSID_WebBrowser, &g_factory, CLSCTX_INPROC_SERVER,
                                         REGCLS_MULTIPLEUSE, &m_dwRegister);
    CHECK(SUCCEEDED(hr));

    CMainFrame* pFrame = new CMainFrame;
    CCreateContext context;
    context.m_pNewViewClass = RUNTIME_CLASS(CTestHtmlView);
    if (!pFrame->Create(NULL, _T("htmltest"), WS_OVERLAPPEDWINDOW, CRect(0, 0, 420, 320), NULL,
                        NULL, 0, &context)) {
      TestLog("frame not made");
      g_nFailures++;
      return FALSE;
    }
    m_pMainWnd = pFrame;
    pFrame->InitialUpdateFrame(NULL, TRUE);
    return TRUE;
  }
  virtual int ExitInstance() {
    if (m_dwRegister != 0) ::CoRevokeClassObject(m_dwRegister);
    // The view let the browser go.
    CHECK(CFakeBrowser::s_nAlive == 0);
    CWinApp::ExitInstance();
    return TestFinish();
  }
};

CTestApp theApp;
