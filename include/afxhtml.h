// afxhtml.h - views and dialogs that show web pages: CHtmlView hosts the
// WebBrowser control in a document's view, CDHtmlDialog hosts it in a
// dialog whose controls are elements of an HTML page, with DHTML event
// maps for the elements' events and DDX_DHtml functions for their values.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#ifndef __AFXHTML_H__
#define __AFXHTML_H__

#include "afxwin.h"
#include "afxext.h"
#include "afxole.h"  // first: it settles the print flags docobj.h names too
#include "afxocc.h"
#include <exdisp.h>
#include <exdispid.h>
#include <mshtml.h>
#include <mshtmhst.h>
#include <mshtmdid.h>
#include "atlcomcli.h"

class CHtmlView;

// ---------------------------------------------------------------------
// CHtmlControlSite: the WebBrowser control's site in a CHtmlView. Its
// IDocHostUIHandler hands the browser's questions to the view.

class CHtmlControlSite : public COleControlSite {
public:
  explicit CHtmlControlSite(COleControlContainer* pParentWnd);
  CHtmlView* GetView() const;

protected:
  BEGIN_INTERFACE_PART(DocHostUIHandler, IDocHostUIHandler)
    STDMETHOD(ShowContextMenu)(DWORD, LPPOINT, LPUNKNOWN, LPDISPATCH);
    STDMETHOD(GetHostInfo)(DOCHOSTUIINFO*);
    STDMETHOD(ShowUI)(DWORD, LPOLEINPLACEACTIVEOBJECT, LPOLECOMMANDTARGET, LPOLEINPLACEFRAME,
                      LPOLEINPLACEUIWINDOW);
    STDMETHOD(HideUI)();
    STDMETHOD(UpdateUI)();
    STDMETHOD(EnableModeless)(BOOL);
    STDMETHOD(OnDocWindowActivate)(BOOL);
    STDMETHOD(OnFrameWindowActivate)(BOOL);
    STDMETHOD(ResizeBorder)(LPCRECT, LPOLEINPLACEUIWINDOW, BOOL);
    STDMETHOD(TranslateAccelerator)(LPMSG, const GUID*, DWORD);
    STDMETHOD(GetOptionKeyPath)(LPOLESTR*, DWORD);
    STDMETHOD(GetDropTarget)(LPDROPTARGET, LPDROPTARGET*);
    STDMETHOD(GetExternal)(LPDISPATCH*);
    STDMETHOD(TranslateUrl)(DWORD, OLECHAR*, OLECHAR**);
    STDMETHOD(FilterDataObject)(LPDATAOBJECT, LPDATAOBJECT*);
  END_INTERFACE_PART(DocHostUIHandler)

  DECLARE_INTERFACE_MAP()
};

// ---------------------------------------------------------------------
// CHtmlView

class CHtmlView : public CFormView {
  DECLARE_DYNCREATE(CHtmlView)
public:
  CHtmlView();
  virtual ~CHtmlView();

  virtual BOOL Create(LPCTSTR lpszClassName, LPCTSTR lpszWindowName, DWORD dwStyle,
                      const RECT& rect, CWnd* pParentWnd, UINT nID,
                      CCreateContext* pContext = NULL);

  // The browser's properties.
  CString GetType() const;
  LONG GetLeft() const;
  void SetLeft(LONG nNewValue);
  LONG GetTop() const;
  void SetTop(LONG nNewValue);
  LONG GetHeight() const;
  void SetHeight(LONG nNewValue);
  LONG GetWidth() const;
  void SetWidth(LONG nNewValue);
  BOOL GetVisible() const;
  void SetVisible(BOOL bNewValue);
  CString GetLocationName() const;
  READYSTATE GetReadyState() const;
  BOOL GetOffline() const;
  void SetOffline(BOOL bNewValue);
  BOOL GetSilent() const;
  void SetSilent(BOOL bNewValue);
  BOOL GetTopLevelContainer() const;
  CString GetLocationURL() const;
  BOOL GetBusy() const;
  LPDISPATCH GetApplication() const;
  LPDISPATCH GetParentBrowser() const;
  LPDISPATCH GetContainer() const;
  LPDISPATCH GetHtmlDocument() const;
  CString GetFullName() const;
  int GetToolBar() const;
  void SetToolBar(int nNewValue);
  BOOL GetMenuBar() const;
  void SetMenuBar(BOOL bNewValue);
  BOOL GetFullScreen() const;
  void SetFullScreen(BOOL bNewValue);
  BOOL GetStatusBar() const;
  void SetStatusBar(BOOL bNewValue);
  BOOL GetRegisterAsBrowser() const;
  void SetRegisterAsBrowser(BOOL bNewValue);
  BOOL GetRegisterAsDropTarget() const;
  void SetRegisterAsDropTarget(BOOL bNewValue);
  BOOL GetTheaterMode() const;
  void SetTheaterMode(BOOL bNewValue);
  BOOL GetAddressBar() const;
  void SetAddressBar(BOOL bNewValue);
  OLECMDF QueryStatusWB(OLECMDID cmdID) const;

  // Properties the page keeps with the browser.
  BOOL GetProperty(LPCTSTR lpszProperty, CString& strValue);
  COleVariant GetProperty(LPCTSTR lpszProperty);
  void PutProperty(LPCTSTR lpszProperty, const VARIANT& vtValue);
  void PutProperty(LPCTSTR lpszPropertyName, double dValue);
  void PutProperty(LPCTSTR lpszPropertyName, LPCTSTR lpszValue);
  void PutProperty(LPCTSTR lpszPropertyName, long lValue);
  void PutProperty(LPCTSTR lpszPropertyName, short nValue);

  // The page's HTML.
  virtual BOOL GetSource(CString& refString);

  // Going places.
  void GoBack();
  void GoForward();
  void GoHome();
  void GoSearch();
  void Navigate(LPCTSTR URL, DWORD dwFlags = 0, LPCTSTR lpszTargetFrameName = NULL,
                LPCTSTR lpszHeaders = NULL, LPVOID lpvPostData = NULL, DWORD dwPostDataLen = 0);
  void Navigate2(LPITEMIDLIST pIDL, DWORD dwFlags = 0, LPCTSTR lpszTargetFrameName = NULL);
  void Navigate2(LPCTSTR lpszURL, DWORD dwFlags = 0, LPCTSTR lpszTargetFrameName = NULL,
                 LPCTSTR lpszHeaders = NULL, LPVOID lpvPostData = NULL,
                 DWORD dwPostDataLen = 0);
  void Navigate2(LPCTSTR lpszURL, DWORD dwFlags, CByteArray& baPostedData,
                 LPCTSTR lpszTargetFrameName = NULL, LPCTSTR lpszHeader = NULL);
  BOOL LoadFromResource(LPCTSTR lpszResource);
  BOOL LoadFromResource(UINT nRes);
  void Refresh();
  void Refresh2(int nLevel);
  void Stop();
  void ExecWB(OLECMDID cmdID, OLECMDEXECOPT cmdexecopt, VARIANT* pvaIn, VARIANT* pvaOut);

  // The document's forms commands (CMDSETID_Forms3).
  HRESULT QueryFormsCommand(DWORD dwCommandID, BOOL* pbSupported, BOOL* pbEnabled,
                            BOOL* pbChecked);
  HRESULT ExecFormsCommand(DWORD dwCommandID, VARIANT* pVarIn, VARIANT* pVarOut);

  // The browser's events.
  virtual void OnNavigateComplete2(LPCTSTR strURL);
  virtual void OnBeforeNavigate2(LPCTSTR lpszURL, DWORD nFlags, LPCTSTR lpszTargetFrameName,
                                 CByteArray& baPostedData, LPCTSTR lpszHeaders, BOOL* pbCancel);
  virtual void OnStatusTextChange(LPCTSTR lpszText);
  virtual void OnProgressChange(long nProgress, long nProgressMax);
  virtual void OnCommandStateChange(long nCommand, BOOL bEnable);
  virtual void OnDownloadBegin();
  virtual void OnDownloadComplete();
  virtual void OnTitleChange(LPCTSTR lpszText);
  virtual void OnPropertyChange(LPCTSTR lpszProperty);
  virtual void OnNewWindow2(LPDISPATCH* ppDisp, BOOL* Cancel);
  virtual void OnDocumentComplete(LPCTSTR lpszURL);
  virtual void OnQuit();
  virtual void OnVisible(BOOL bVisible);
  virtual void OnToolBar(BOOL bToolBar);
  virtual void OnMenuBar(BOOL bMenuBar);
  virtual void OnStatusBar(BOOL bStatusBar);
  virtual void OnFullScreen(BOOL bFullScreen);
  virtual void OnTheaterMode(BOOL bTheaterMode);
  virtual void OnNavigateError(LPCTSTR lpszURL, LPCTSTR lpszFrame, DWORD dwError,
                               BOOL* pbCancel);

  // The browser's questions to its host (IDocHostUIHandler).
  virtual HRESULT OnShowContextMenu(DWORD dwID, LPPOINT ppt, LPUNKNOWN pcmdtReserved,
                                    LPDISPATCH pdispReserved);
  virtual HRESULT OnGetHostInfo(DOCHOSTUIINFO* pInfo);
  virtual HRESULT OnShowUI(DWORD dwID, LPOLEINPLACEACTIVEOBJECT pActiveObject,
                           LPOLECOMMANDTARGET pCommandTarget, LPOLEINPLACEFRAME pFrame,
                           LPOLEINPLACEUIWINDOW pDoc);
  virtual HRESULT OnHideUI();
  virtual HRESULT OnUpdateUI();
  virtual HRESULT OnEnableModeless(BOOL fEnable);
  virtual HRESULT OnDocWindowActivate(BOOL fActivate);
  virtual HRESULT OnFrameWindowActivate(BOOL fActivate);
  virtual HRESULT OnResizeBorder(LPCRECT prcBorder, LPOLEINPLACEUIWINDOW pUIWindow,
                                 BOOL fFrameWindow);
  virtual HRESULT OnTranslateAccelerator(LPMSG lpMsg, const GUID* pguidCmdGroup, DWORD nCmdID);
  virtual HRESULT OnGetOptionKeyPath(LPOLESTR* pchKey, DWORD dwReserved);
  virtual HRESULT OnGetDropTarget(LPDROPTARGET pDropTarget, LPDROPTARGET* ppDropTarget);
  virtual HRESULT OnGetExternal(LPDISPATCH* lppDispatch);
  virtual HRESULT OnTranslateUrl(DWORD dwTranslate, OLECHAR* pchURLIn, OLECHAR** ppchURLOut);
  virtual HRESULT OnFilterDataObject(LPDATAOBJECT pDataObject, LPDATAOBJECT* ppDataObject);

  virtual void OnDraw(CDC* pDC);
  virtual BOOL PreTranslateMessage(MSG* pMsg);
  virtual BOOL CreateControlSite(COleControlContainer* pContainer, COleControlSite** ppSite,
                                 UINT nID, REFCLSID clsid);

  IWebBrowser2* m_pBrowserApp;
  CWnd m_wndBrowser;

protected:
  // The event sink's entry points; they turn the events' VARIANTs into
  // the arguments of the handlers above.
  virtual void NavigateComplete2(LPDISPATCH pDisp, VARIANT* URL);
  virtual void BeforeNavigate2(LPDISPATCH pDisp, VARIANT* URL, VARIANT* Flags,
                               VARIANT* TargetFrameName, VARIANT* PostData, VARIANT* Headers,
                               BOOL* Cancel);
  virtual void DocumentComplete(LPDISPATCH pDisp, VARIANT* URL);
  virtual void NavigateError(LPDISPATCH pDisp, VARIANT* pvURL, VARIANT* pvFrame,
                             VARIANT* pvStatusCode, BOOL* pvbCancel);

  afx_msg void OnFilePrint();
  afx_msg void OnSize(UINT nType, int cx, int cy);
  afx_msg void OnPaint();
  afx_msg BOOL OnEraseBkgnd(CDC* pDC);
  afx_msg void OnDestroy();

  DECLARE_EVENTSINK_MAP()
  DECLARE_MESSAGE_MAP()
};

// ---------------------------------------------------------------------
// DHTML event maps: handlers for the events of a page's elements, found
// by the element's id, class or tag name, or for the document's own.

#ifndef AFX_IDC_BROWSER
#define AFX_IDC_BROWSER 30990  // the WebBrowser control of a CDHtmlDialog
#endif

class CDHtmlEventSink;
typedef HRESULT (CDHtmlEventSink::*DHEVTFUNC)(IHTMLElement* pElement);

enum DHtmlEventMapEntryType {
  DHTMLEVENTMAPENTRY_NAME,     // the element with this id; the document when NULL
  DHTMLEVENTMAPENTRY_CLASS,    // the elements of this class
  DHTMLEVENTMAPENTRY_TAG,      // the elements with this tag
  DHTMLEVENTMAPENTRY_ELEMENT,  // the element with this id, also inside a frame
  DHTMLEVENTMAPENTRY_CONTROL,  // the ActiveX control of the <object> with this id
  DHTMLEVENTMAPENTRY_END
};

// Calls a DHTML_EVENT_AXCONTROL handler with the event's arguments.
typedef HRESULT (*AFX_DHTMLAXINVOKE)(CDHtmlEventSink* pSink, DISPPARAMS* pDispParams,
                                     VARIANT* pVarResult);

struct DHtmlEventMapEntry {
  DHtmlEventMapEntryType nType;
  DISPID dispId;
  LPCTSTR szName;
  DHEVTFUNC pfnEventFunc;
  AFX_DHTMLAXINVOKE pfnAxInvoke;  // DHTMLEVENTMAPENTRY_CONTROL's handler
};

// A page of a CMultiPageDHtmlDialog and the embedded map of its events.
struct DHtmlUrlEventMapEntry {
  LPCTSTR szUrl;  // a resource id (MAKEINTRESOURCE) or a name; NULL ends the map
  const DHtmlEventMapEntry* (*pfnGetMap)();
};

// A URL_EVENT_ENTRY's page, given as a resource id or a name.
inline LPCTSTR _AfxDHtmlUrl(LPCTSTR lpsz) { return lpsz; }
inline LPCTSTR _AfxDHtmlUrl(int nID) { return MAKEINTRESOURCE(nID); }
inline LPCTSTR _AfxDHtmlUrl(UINT nID) { return MAKEINTRESOURCE(nID); }
inline LPCTSTR _AfxDHtmlUrl(long nID) { return MAKEINTRESOURCE(nID); }
inline LPCTSTR _AfxDHtmlUrl(unsigned long nID) { return MAKEINTRESOURCE(nID); }

#define DECLARE_DHTML_EVENT_MAP() \
public:                           \
  virtual const DHtmlEventMapEntry* GetDHtmlEventMap();

#define BEGIN_DHTML_EVENT_MAP(className)                         \
  const DHtmlEventMapEntry* className::GetDHtmlEventMap() {      \
    typedef className theClass;                                  \
    (void)sizeof(theClass*);                                     \
    static const DHtmlEventMapEntry _dhtmlEventEntries[] = {
#define BEGIN_DHTML_EVENT_MAP_INLINE(className)                  \
  virtual const DHtmlEventMapEntry* GetDHtmlEventMap() {         \
    typedef className theClass;                                  \
    (void)sizeof(theClass*);                                     \
    static const DHtmlEventMapEntry _dhtmlEventEntries[] = {
#define END_DHTML_EVENT_MAP()                                    \
      {DHTMLEVENTMAPENTRY_END, 0, NULL, (DHEVTFUNC)NULL}};       \
    return _dhtmlEventEntries;                                   \
  }
#define END_DHTML_EVENT_MAP_INLINE() END_DHTML_EVENT_MAP()

// Embedded maps, one for each page of a CMultiPageDHtmlDialog: in the
// class definition, with the page's entries between them.
#define BEGIN_EMBED_DHTML_EVENT_MAP(className, mapName)          \
  static const DHtmlEventMapEntry* _GetDHtmlEventMap_##mapName() { \
    typedef className theClass;                                  \
    (void)sizeof(theClass*);                                     \
    static const DHtmlEventMapEntry _dhtmlEventEntries[] = {
#define END_EMBED_DHTML_EVENT_MAP() END_DHTML_EVENT_MAP()

// The URL map of a CMultiPageDHtmlDialog: which embedded map each page
// uses. In the implementation file:
//   BEGIN_DHTML_URL_EVENT_MAP(CMyDlg)
//     BEGIN_URL_ENTRIES(CMyDlg)
//       URL_EVENT_ENTRY(CMyDlg, IDR_PAGE1, page1)
//       URL_EVENT_ENTRY(CMyDlg, _T("page2.htm"), page2)
//     END_URL_ENTRIES()
//   END_DHTML_URL_EVENT_MAP(CMyDlg)
#define DECLARE_DHTML_URL_EVENT_MAP() \
public:                               \
  virtual const DHtmlUrlEventMapEntry* GetDHtmlUrlEventMap();
#define BEGIN_DHTML_URL_EVENT_MAP(className)                      \
  const DHtmlUrlEventMapEntry* className::GetDHtmlUrlEventMap() { \
    typedef className theClass;                                   \
    (void)sizeof(theClass*);
#define BEGIN_URL_ENTRIES(className) static const DHtmlUrlEventMapEntry _dhtmlUrlEntries[] = {
#define URL_EVENT_ENTRY(className, url, mapName) \
  {_AfxDHtmlUrl(url), &className::_GetDHtmlEventMap_##mapName},
#define END_URL_ENTRIES() \
  {(LPCTSTR)NULL, NULL}};
#define END_DHTML_URL_EVENT_MAP(className) \
  return _dhtmlUrlEntries;                 \
  }

#define _AFX_DHTML_HANDLER(memberFxn) \
  static_cast<DHEVTFUNC>(static_cast<HRESULT (theClass::*)(IHTMLElement*)>(_AFX_PMF(theClass, memberFxn)))
#define DHTML_EVENT(dispid, elemName, memberFxn) \
  {DHTMLEVENTMAPENTRY_NAME, (DISPID)(dispid), elemName, _AFX_DHTML_HANDLER(memberFxn)},
#define DHTML_EVENT_CLASS(dispid, elemName, memberFxn) \
  {DHTMLEVENTMAPENTRY_CLASS, (DISPID)(dispid), elemName, _AFX_DHTML_HANDLER(memberFxn)},
#define DHTML_EVENT_TAG(dispid, elemName, memberFxn) \
  {DHTMLEVENTMAPENTRY_TAG, (DISPID)(dispid), elemName, _AFX_DHTML_HANDLER(memberFxn)},
#define DHTML_EVENT_ELEMENT(dispid, elemName, memberFxn) \
  {DHTMLEVENTMAPENTRY_ELEMENT, (DISPID)(dispid), elemName, _AFX_DHTML_HANDLER(memberFxn)},
// An event of an ActiveX control in the page. The handler takes the
// event's parameters, as their types (long, BSTR, VARIANT*, a
// VARIANT_BOOL* to write back...), and may be __stdcall.
#define DHTML_EVENT_AXCONTROL(dispid, controlName, memberFxn)                  \
  {DHTMLEVENTMAPENTRY_CONTROL, (DISPID)(dispid), controlName, (DHEVTFUNC)NULL, \
   &_AfxDHtmlAx<_AFX_PMF(theClass, memberFxn)>::Invoke},

// The events by name; an element of NULL is the document.
#define DHTML_EVENT_ONHELP(elemName, memberFxn) DHTML_EVENT(DISPID_HTMLELEMENTEVENTS2_ONHELP, elemName, memberFxn)
#define DHTML_EVENT_ONCLICK(elemName, memberFxn) DHTML_EVENT(DISPID_HTMLELEMENTEVENTS2_ONCLICK, elemName, memberFxn)
#define DHTML_EVENT_ONDBLCLICK(elemName, memberFxn) DHTML_EVENT(DISPID_HTMLELEMENTEVENTS2_ONDBLCLICK, elemName, memberFxn)
#define DHTML_EVENT_ONKEYPRESS(elemName, memberFxn) DHTML_EVENT(DISPID_HTMLELEMENTEVENTS2_ONKEYPRESS, elemName, memberFxn)
#define DHTML_EVENT_ONKEYDOWN(elemName, memberFxn) DHTML_EVENT(DISPID_HTMLELEMENTEVENTS2_ONKEYDOWN, elemName, memberFxn)
#define DHTML_EVENT_ONKEYUP(elemName, memberFxn) DHTML_EVENT(DISPID_HTMLELEMENTEVENTS2_ONKEYUP, elemName, memberFxn)
#define DHTML_EVENT_ONMOUSEOUT(elemName, memberFxn) DHTML_EVENT(DISPID_HTMLELEMENTEVENTS2_ONMOUSEOUT, elemName, memberFxn)
#define DHTML_EVENT_ONMOUSEOVER(elemName, memberFxn) DHTML_EVENT(DISPID_HTMLELEMENTEVENTS2_ONMOUSEOVER, elemName, memberFxn)
#define DHTML_EVENT_ONMOUSEMOVE(elemName, memberFxn) DHTML_EVENT(DISPID_HTMLELEMENTEVENTS2_ONMOUSEMOVE, elemName, memberFxn)
#define DHTML_EVENT_ONMOUSEDOWN(elemName, memberFxn) DHTML_EVENT(DISPID_HTMLELEMENTEVENTS2_ONMOUSEDOWN, elemName, memberFxn)
#define DHTML_EVENT_ONMOUSEUP(elemName, memberFxn) DHTML_EVENT(DISPID_HTMLELEMENTEVENTS2_ONMOUSEUP, elemName, memberFxn)
#define DHTML_EVENT_ONSELECTSTART(elemName, memberFxn) DHTML_EVENT(DISPID_HTMLELEMENTEVENTS2_ONSELECTSTART, elemName, memberFxn)
#define DHTML_EVENT_ONFILTERCHANGE(elemName, memberFxn) DHTML_EVENT(DISPID_HTMLELEMENTEVENTS2_ONFILTERCHANGE, elemName, memberFxn)
#define DHTML_EVENT_ONDRAGSTART(elemName, memberFxn) DHTML_EVENT(DISPID_HTMLELEMENTEVENTS2_ONDRAGSTART, elemName, memberFxn)
#define DHTML_EVENT_ONBEFOREUPDATE(elemName, memberFxn) DHTML_EVENT(DISPID_HTMLELEMENTEVENTS2_ONBEFOREUPDATE, elemName, memberFxn)
#define DHTML_EVENT_ONAFTERUPDATE(elemName, memberFxn) DHTML_EVENT(DISPID_HTMLELEMENTEVENTS2_ONAFTERUPDATE, elemName, memberFxn)
#define DHTML_EVENT_ONERRORUPDATE(elemName, memberFxn) DHTML_EVENT(DISPID_HTMLELEMENTEVENTS2_ONERRORUPDATE, elemName, memberFxn)
#define DHTML_EVENT_ONROWEXIT(elemName, memberFxn) DHTML_EVENT(DISPID_HTMLELEMENTEVENTS2_ONROWEXIT, elemName, memberFxn)
#define DHTML_EVENT_ONROWENTER(elemName, memberFxn) DHTML_EVENT(DISPID_HTMLELEMENTEVENTS2_ONROWENTER, elemName, memberFxn)
#define DHTML_EVENT_ONDATASETCHANGED(elemName, memberFxn) DHTML_EVENT(DISPID_HTMLELEMENTEVENTS2_ONDATASETCHANGED, elemName, memberFxn)
#define DHTML_EVENT_ONDATAAVAILABLE(elemName, memberFxn) DHTML_EVENT(DISPID_HTMLELEMENTEVENTS2_ONDATAAVAILABLE, elemName, memberFxn)
#define DHTML_EVENT_ONDATASETCOMPLETE(elemName, memberFxn) DHTML_EVENT(DISPID_HTMLELEMENTEVENTS2_ONDATASETCOMPLETE, elemName, memberFxn)
#define DHTML_EVENT_ONLOSECAPTURE(elemName, memberFxn) DHTML_EVENT(DISPID_HTMLELEMENTEVENTS2_ONLOSECAPTURE, elemName, memberFxn)
#define DHTML_EVENT_ONPROPERTYCHANGE(elemName, memberFxn) DHTML_EVENT(DISPID_HTMLELEMENTEVENTS2_ONPROPERTYCHANGE, elemName, memberFxn)
#define DHTML_EVENT_ONSCROLL(elemName, memberFxn) DHTML_EVENT(DISPID_HTMLELEMENTEVENTS2_ONSCROLL, elemName, memberFxn)
#define DHTML_EVENT_ONFOCUS(elemName, memberFxn) DHTML_EVENT(DISPID_HTMLELEMENTEVENTS2_ONFOCUS, elemName, memberFxn)
#define DHTML_EVENT_ONBLUR(elemName, memberFxn) DHTML_EVENT(DISPID_HTMLELEMENTEVENTS2_ONBLUR, elemName, memberFxn)
#define DHTML_EVENT_ONRESIZE(elemName, memberFxn) DHTML_EVENT(DISPID_HTMLELEMENTEVENTS2_ONRESIZE, elemName, memberFxn)
#define DHTML_EVENT_ONDRAG(elemName, memberFxn) DHTML_EVENT(DISPID_HTMLELEMENTEVENTS2_ONDRAG, elemName, memberFxn)
#define DHTML_EVENT_ONDRAGEND(elemName, memberFxn) DHTML_EVENT(DISPID_HTMLELEMENTEVENTS2_ONDRAGEND, elemName, memberFxn)
#define DHTML_EVENT_ONDRAGENTER(elemName, memberFxn) DHTML_EVENT(DISPID_HTMLELEMENTEVENTS2_ONDRAGENTER, elemName, memberFxn)
#define DHTML_EVENT_ONDRAGOVER(elemName, memberFxn) DHTML_EVENT(DISPID_HTMLELEMENTEVENTS2_ONDRAGOVER, elemName, memberFxn)
#define DHTML_EVENT_ONDRAGLEAVE(elemName, memberFxn) DHTML_EVENT(DISPID_HTMLELEMENTEVENTS2_ONDRAGLEAVE, elemName, memberFxn)
#define DHTML_EVENT_ONDROP(elemName, memberFxn) DHTML_EVENT(DISPID_HTMLELEMENTEVENTS2_ONDROP, elemName, memberFxn)
#define DHTML_EVENT_ONBEFORECUT(elemName, memberFxn) DHTML_EVENT(DISPID_HTMLELEMENTEVENTS2_ONBEFORECUT, elemName, memberFxn)
#define DHTML_EVENT_ONCUT(elemName, memberFxn) DHTML_EVENT(DISPID_HTMLELEMENTEVENTS2_ONCUT, elemName, memberFxn)
#define DHTML_EVENT_ONBEFORECOPY(elemName, memberFxn) DHTML_EVENT(DISPID_HTMLELEMENTEVENTS2_ONBEFORECOPY, elemName, memberFxn)
#define DHTML_EVENT_ONCOPY(elemName, memberFxn) DHTML_EVENT(DISPID_HTMLELEMENTEVENTS2_ONCOPY, elemName, memberFxn)
#define DHTML_EVENT_ONBEFOREPASTE(elemName, memberFxn) DHTML_EVENT(DISPID_HTMLELEMENTEVENTS2_ONBEFOREPASTE, elemName, memberFxn)
#define DHTML_EVENT_ONPASTE(elemName, memberFxn) DHTML_EVENT(DISPID_HTMLELEMENTEVENTS2_ONPASTE, elemName, memberFxn)
#define DHTML_EVENT_ONCONTEXTMENU(elemName, memberFxn) DHTML_EVENT(DISPID_HTMLELEMENTEVENTS2_ONCONTEXTMENU, elemName, memberFxn)
#define DHTML_EVENT_ONROWSDELETE(elemName, memberFxn) DHTML_EVENT(DISPID_HTMLELEMENTEVENTS2_ONROWSDELETE, elemName, memberFxn)
#define DHTML_EVENT_ONROWSINSERTED(elemName, memberFxn) DHTML_EVENT(DISPID_HTMLELEMENTEVENTS2_ONROWSINSERTED, elemName, memberFxn)
#define DHTML_EVENT_ONCELLCHANGE(elemName, memberFxn) DHTML_EVENT(DISPID_HTMLELEMENTEVENTS2_ONCELLCHANGE, elemName, memberFxn)
#define DHTML_EVENT_ONREADYSTATECHANGE(elemName, memberFxn) DHTML_EVENT(DISPID_HTMLELEMENTEVENTS2_ONREADYSTATECHANGE, elemName, memberFxn)
#define DHTML_EVENT_ONBEFOREEDITFOCUS(elemName, memberFxn) DHTML_EVENT(DISPID_EVMETH_ONBEFOREEDITFOCUS, elemName, memberFxn)
#define DHTML_EVENT_ONLAYOUTCOMPLETE(elemName, memberFxn) DHTML_EVENT(DISPID_HTMLELEMENTEVENTS2_ONLAYOUTCOMPLETE, elemName, memberFxn)
#define DHTML_EVENT_ONPAGE(elemName, memberFxn) DHTML_EVENT(DISPID_HTMLELEMENTEVENTS2_ONPAGE, elemName, memberFxn)
#define DHTML_EVENT_ONBEFOREDEACTIVATE(elemName, memberFxn) DHTML_EVENT(DISPID_HTMLELEMENTEVENTS2_ONBEFOREDEACTIVATE, elemName, memberFxn)
#define DHTML_EVENT_ONBEFOREACTIVATE(elemName, memberFxn) DHTML_EVENT(DISPID_HTMLELEMENTEVENTS2_ONBEFOREACTIVATE, elemName, memberFxn)
#define DHTML_EVENT_ONMOVE(elemName, memberFxn) DHTML_EVENT(DISPID_HTMLELEMENTEVENTS2_ONMOVE, elemName, memberFxn)
#define DHTML_EVENT_ONCONTROLSELECT(elemName, memberFxn) DHTML_EVENT(DISPID_HTMLELEMENTEVENTS2_ONCONTROLSELECT, elemName, memberFxn)
#define DHTML_EVENT_ONMOVESTART(elemName, memberFxn) DHTML_EVENT(DISPID_HTMLELEMENTEVENTS2_ONMOVESTART, elemName, memberFxn)
#define DHTML_EVENT_ONMOVEEND(elemName, memberFxn) DHTML_EVENT(DISPID_HTMLELEMENTEVENTS2_ONMOVEEND, elemName, memberFxn)
#define DHTML_EVENT_ONRESIZESTART(elemName, memberFxn) DHTML_EVENT(DISPID_HTMLELEMENTEVENTS2_ONRESIZESTART, elemName, memberFxn)
#define DHTML_EVENT_ONRESIZEEND(elemName, memberFxn) DHTML_EVENT(DISPID_HTMLELEMENTEVENTS2_ONRESIZEEND, elemName, memberFxn)
#define DHTML_EVENT_ONMOUSEENTER(elemName, memberFxn) DHTML_EVENT(DISPID_HTMLELEMENTEVENTS2_ONMOUSEENTER, elemName, memberFxn)
#define DHTML_EVENT_ONMOUSELEAVE(elemName, memberFxn) DHTML_EVENT(DISPID_HTMLELEMENTEVENTS2_ONMOUSELEAVE, elemName, memberFxn)
#define DHTML_EVENT_ONMOUSEWHEEL(elemName, memberFxn) DHTML_EVENT(DISPID_HTMLELEMENTEVENTS2_ONMOUSEWHEEL, elemName, memberFxn)
#define DHTML_EVENT_ONACTIVATE(elemName, memberFxn) DHTML_EVENT(DISPID_HTMLELEMENTEVENTS2_ONACTIVATE, elemName, memberFxn)
#define DHTML_EVENT_ONDEACTIVATE(elemName, memberFxn) DHTML_EVENT(DISPID_HTMLELEMENTEVENTS2_ONDEACTIVATE, elemName, memberFxn)
#define DHTML_EVENT_ONFOCUSIN(elemName, memberFxn) DHTML_EVENT(DISPID_HTMLELEMENTEVENTS2_ONFOCUSIN, elemName, memberFxn)
#define DHTML_EVENT_ONFOCUSOUT(elemName, memberFxn) DHTML_EVENT(DISPID_HTMLELEMENTEVENTS2_ONFOCUSOUT, elemName, memberFxn)

// ---------------------------------------------------------------------
// CDHtmlEventSink: connects a DHTML event map to a document's elements.

class CDHtmlSinkHandler;

class CDHtmlEventSink {
public:
  CDHtmlEventSink();
  virtual ~CDHtmlEventSink();

  virtual const DHtmlEventMapEntry* GetDHtmlEventMap() = 0;
  virtual HRESULT GetDHtmlDocument(IHTMLDocument2** pphtmlDoc) = 0;

  // The document's own events (the entries without an element).
  HRESULT ConnectDHtmlEvents(IUnknown* punkDoc);
  void DisconnectDHtmlEvents();
  // The elements' events, found in the document GetDHtmlDocument gives.
  HRESULT ConnectDHtmlElementEvents(DWORD_PTR dwThunkOffset = 0);
  void DisconnectDHtmlElementEvents();

  // Calls the map's handlers for an event of pElement; FALSE when none.
  BOOL DHtmlEventHook(HRESULT* phr, DISPID dispIdMember, DISPPARAMS* pDispParams,
                      VARIANT* pVarResult, EXCEPINFO* pExcepInfo, UINT* puArgErr);

  // The event being handled, while a handler runs.
  HRESULT GetEvent(IHTMLEventObj** ppEventObj);

  // Connects one element's (or the document's) events.
  HRESULT ConnectElement(IDispatch* pdispElem, REFIID iidEvents, BOOL bDocument);

protected:
  BOOL IsSinkedElement(IDispatch* pdispElem);
  void FireFor(CDHtmlSinkHandler* pHandler, DISPID dispid, DISPPARAMS* pDispParams,
               VARIANT* pVarResult, HRESULT* phr);
  // The maps in use: GetDHtmlEventMap's, then the page's embedded map.
  int GetMaps(const DHtmlEventMapEntry* apMaps[2]);
  HRESULT ConnectControl(IHTMLElementCollection* pAll, LPCTSTR szName);
  CPtrArray m_aSinks;  // CDHtmlSinkHandler*, one for each connected element
  IHTMLEventObj* m_pCurrentEvent;
  const DHtmlEventMapEntry* m_pPageMap;  // a CMultiPageDHtmlDialog's page's map
  friend class CDHtmlSinkHandler;
};

// DHTML_EVENT_AXCONTROL's call: the event's arguments (last first in the
// DISPPARAMS) as the handler's parameter types.
void AFXAPI _AfxDHtmlAxArg(AFX_DISPARG& a, VARIANT* pSrc);
template <class T>
inline auto _AfxDHtmlAxGet(AFX_DISPARG& a, VARIANT*, T* pTag) -> decltype(_AfxDispGet(a, pTag)) {
  return _AfxDispGet(a, pTag);
}
// A pointer to a by-reference argument is the caller's own, so what the
// handler writes goes back.
template <class T>
inline T* _AfxDHtmlAxGet(AFX_DISPARG& a, VARIANT* pSrc, T** pTag) {
  if (!std::is_base_of<IUnknown, T>::value && !std::is_same<T, VARIANT>::value &&
      (pSrc->vt & VT_BYREF) != 0 && pSrc->vt != (VT_BYREF | VT_VARIANT))
    return (T*)pSrc->byref;
  return _AfxDispGet(a, pTag);
}

template <class C, class M, class R, class... A, size_t... I>
inline void _AfxDHtmlAxCall(C* p, M pfn, AFX_DISPARG* args, VARIANT** src, VARIANT* r,
                            std::index_sequence<I...>) {
  (void)args;
  (void)src;
  _AfxDispRet<R>::Call(r, [&]() -> R {
    return (p->*pfn)(_AfxDHtmlAxGet(args[I], src[I], (typename std::decay<A>::type*)0)...);
  });
}

template <class C, class M, class R, class... A>
inline HRESULT _AfxDHtmlAxInvoke(CDHtmlEventSink* pSink, M pfn, DISPPARAMS* pdp, VARIANT* pResult) {
  UINT n = pdp != NULL ? pdp->cArgs : 0;
  if (n < sizeof...(A)) return DISP_E_BADPARAMCOUNT;
  AFX_DISPARG args[sizeof...(A) + 1];
  VARIANT* src[sizeof...(A) + 1];
  for (UINT i = 0; i < sizeof...(A); i++) {
    src[i] = &pdp->rgvarg[n - 1 - i];
    _AfxDHtmlAxArg(args[i], src[i]);
  }
  VARIANT vr;
  ::VariantInit(&vr);
  _AfxDHtmlAxCall<C, M, R, A...>(static_cast<C*>(pSink), pfn, args, src, &vr,
                                 std::index_sequence_for<A...>());
  if (pResult != NULL)
    *pResult = vr;
  else
    ::VariantClear(&vr);
  return S_OK;
}

template <auto pfn, class M = decltype(pfn)>
struct _AfxDHtmlAx;
template <auto pfn, class C, class R, class... A>
struct _AfxDHtmlAx<pfn, R (C::*)(A...)> {
  static HRESULT Invoke(CDHtmlEventSink* pSink, DISPPARAMS* pdp, VARIANT* pResult) {
    return _AfxDHtmlAxInvoke<C, R (C::*)(A...), R, A...>(pSink, pfn, pdp, pResult);
  }
};
#if defined(__i386__) || defined(_M_IX86)
template <auto pfn, class C, class R, class... A>
struct _AfxDHtmlAx<pfn, R (__stdcall C::*)(A...)> {
  static HRESULT Invoke(CDHtmlEventSink* pSink, DISPPARAMS* pdp, VARIANT* pResult) {
    return _AfxDHtmlAxInvoke<C, R (__stdcall C::*)(A...), R, A...>(pSink, pfn, pdp, pResult);
  }
};
#endif

// ---------------------------------------------------------------------
// CDHtmlControlSite: the WebBrowser control's site in a CDHtmlDialog.

class CDHtmlDialog;

class CDHtmlControlSite : public COleControlSite {
public:
  explicit CDHtmlControlSite(COleControlContainer* pParentWnd, CDHtmlDialog* pHandler);
  CDHtmlDialog* m_pHandler;

protected:
  BEGIN_INTERFACE_PART(DocHostUIHandler, IDocHostUIHandler)
    STDMETHOD(ShowContextMenu)(DWORD, LPPOINT, LPUNKNOWN, LPDISPATCH);
    STDMETHOD(GetHostInfo)(DOCHOSTUIINFO*);
    STDMETHOD(ShowUI)(DWORD, LPOLEINPLACEACTIVEOBJECT, LPOLECOMMANDTARGET, LPOLEINPLACEFRAME,
                      LPOLEINPLACEUIWINDOW);
    STDMETHOD(HideUI)();
    STDMETHOD(UpdateUI)();
    STDMETHOD(EnableModeless)(BOOL);
    STDMETHOD(OnDocWindowActivate)(BOOL);
    STDMETHOD(OnFrameWindowActivate)(BOOL);
    STDMETHOD(ResizeBorder)(LPCRECT, LPOLEINPLACEUIWINDOW, BOOL);
    STDMETHOD(TranslateAccelerator)(LPMSG, const GUID*, DWORD);
    STDMETHOD(GetOptionKeyPath)(LPOLESTR*, DWORD);
    STDMETHOD(GetDropTarget)(LPDROPTARGET, LPDROPTARGET*);
    STDMETHOD(GetExternal)(LPDISPATCH*);
    STDMETHOD(TranslateUrl)(DWORD, OLECHAR*, OLECHAR**);
    STDMETHOD(FilterDataObject)(LPDATAOBJECT, LPDATAOBJECT*);
  END_INTERFACE_PART(DocHostUIHandler)

  DECLARE_INTERFACE_MAP()
};

// ---------------------------------------------------------------------
// CDHtmlDialog

class CDHtmlDialog : public CDialog, public CDHtmlEventSink {
  DECLARE_DYNAMIC(CDHtmlDialog)
public:
  CDHtmlDialog();
  CDHtmlDialog(LPCTSTR lpszTemplateName, LPCTSTR szHtmlResID, CWnd* pParentWnd = NULL);
  CDHtmlDialog(UINT nIDTemplate, UINT nHtmlResID = 0, CWnd* pParentWnd = NULL);
  virtual ~CDHtmlDialog();

  virtual BOOL OnInitDialog();
  virtual BOOL PreTranslateMessage(MSG* pMsg);
  virtual BOOL CreateControlSite(COleControlContainer* pContainer, COleControlSite** ppSite,
                                 UINT nID, REFCLSID clsid);
  virtual void DestroyModeless();

  // The page.
  BOOL LoadFromResource(LPCTSTR lpszResource);
  BOOL LoadFromResource(UINT nRes);
  void Navigate(LPCTSTR lpszURL, DWORD dwFlags = 0, LPCTSTR lpszTargetFrameName = NULL,
                LPCTSTR lpszHeaders = NULL, LPVOID lpvPostData = NULL,
                DWORD dwPostDataLen = 0);
  void GetCurrentUrl(CString& szUrl);
  virtual HRESULT GetDHtmlDocument(IHTMLDocument2** pphtmlDoc);

  // The browser's events.
  virtual void OnBeforeNavigate(LPDISPATCH pDisp, LPCTSTR szUrl);
  virtual void OnNavigateComplete(LPDISPATCH pDisp, LPCTSTR szUrl);
  virtual void OnDocumentComplete(LPDISPATCH pDisp, LPCTSTR szUrl);

  // The page's elements.
  HRESULT GetElement(LPCTSTR szElementId, IDispatch** ppdisp, BOOL* pbCollection = NULL);
  HRESULT GetElement(LPCTSTR szElementId, IHTMLElement** pphtmlElement);
  HRESULT GetElementInterface(LPCTSTR szElementId, REFIID riid, void** ppvObj);
  template <class Q>
  HRESULT GetElementInterface(LPCTSTR szElementId, Q** ppvObj) {
    return GetElementInterface(szElementId, __uuidof(Q), (void**)ppvObj);
  }
  BSTR GetElementText(LPCTSTR szElementId);
  void SetElementText(LPCTSTR szElementId, BSTR bstrText);
  void SetElementText(IUnknown* punkElem, BSTR bstrText);
  BSTR GetElementHtml(LPCTSTR szElementId);
  void SetElementHtml(LPCTSTR szElementId, BSTR bstrText);
  void SetElementHtml(IUnknown* punkElem, BSTR bstrText);
  VARIANT GetElementProperty(LPCTSTR szElementId, DISPID dispid);
  void SetElementProperty(LPCTSTR szElementId, DISPID dispid, VARIANT* pVar);

  // ActiveX controls in the page (<object> elements).
  HRESULT GetControlDispatch(LPCTSTR szId, IDispatch** ppdisp);
  VARIANT GetControlProperty(LPCTSTR szId, LPCTSTR szPropName);
  VARIANT GetControlProperty(LPCTSTR szId, DISPID dispid);
  VARIANT GetControlProperty(IDispatch* pdispControl, DISPID dispid);
  void SetControlProperty(LPCTSTR szElementId, DISPID dispid, VARIANT* pVar);
  void SetControlProperty(IDispatch* pdispControl, DISPID dispid, VARIANT* pVar);
  void SetControlProperty(LPCTSTR szElementId, LPCTSTR szPropName, VARIANT* pVar);

  // Dialog data exchange with the page's elements.
  void DDX_DHtml_ElementValue(CDataExchange* pDX, LPCTSTR szId, CString& value);
  void DDX_DHtml_ElementValue(CDataExchange* pDX, LPCTSTR szId, short& value);
  void DDX_DHtml_ElementValue(CDataExchange* pDX, LPCTSTR szId, int& value);
  void DDX_DHtml_ElementValue(CDataExchange* pDX, LPCTSTR szId, long& value);
  void DDX_DHtml_ElementValue(CDataExchange* pDX, LPCTSTR szId, DWORD& value);
  void DDX_DHtml_ElementValue(CDataExchange* pDX, LPCTSTR szId, float& value);
  void DDX_DHtml_ElementValue(CDataExchange* pDX, LPCTSTR szId, double& value);
  void DDX_DHtml_ElementValue(CDataExchange* pDX, LPCTSTR szId, COleDateTime& value);
  void DDX_DHtml_ElementValue(CDataExchange* pDX, LPCTSTR szId, COleCurrency& value);
  void DDX_DHtml_ElementInnerText(CDataExchange* pDX, LPCTSTR szId, CString& value);
  void DDX_DHtml_ElementInnerText(CDataExchange* pDX, LPCTSTR szId, short& value);
  void DDX_DHtml_ElementInnerText(CDataExchange* pDX, LPCTSTR szId, int& value);
  void DDX_DHtml_ElementInnerText(CDataExchange* pDX, LPCTSTR szId, long& value);
  void DDX_DHtml_ElementInnerText(CDataExchange* pDX, LPCTSTR szId, DWORD& value);
  void DDX_DHtml_ElementInnerText(CDataExchange* pDX, LPCTSTR szId, float& value);
  void DDX_DHtml_ElementInnerText(CDataExchange* pDX, LPCTSTR szId, double& value);
  void DDX_DHtml_ElementInnerText(CDataExchange* pDX, LPCTSTR szId, COleDateTime& value);
  void DDX_DHtml_ElementInnerText(CDataExchange* pDX, LPCTSTR szId, COleCurrency& value);
  void DDX_DHtml_ElementInnerHtml(CDataExchange* pDX, LPCTSTR szId, CString& value);
  void DDX_DHtml_Anchor_Href(CDataExchange* pDX, LPCTSTR szId, CString& value);
  void DDX_DHtml_Anchor_Target(CDataExchange* pDX, LPCTSTR szId, CString& value);
  void DDX_DHtml_Img_Src(CDataExchange* pDX, LPCTSTR szId, CString& value);
  void DDX_DHtml_Frame_Src(CDataExchange* pDX, LPCTSTR szId, CString& value);
  void DDX_DHtml_IFrame_Src(CDataExchange* pDX, LPCTSTR szId, CString& value);
  void DDX_DHtml_CheckBox(CDataExchange* pDX, LPCTSTR szId, int& value);
  void DDX_DHtml_Radio(CDataExchange* pDX, LPCTSTR szId, long& value);
  void DDX_DHtml_SelectValue(CDataExchange* pDX, LPCTSTR szId, CString& value);
  void DDX_DHtml_SelectString(CDataExchange* pDX, LPCTSTR szId, CString& value);
  void DDX_DHtml_SelectIndex(CDataExchange* pDX, LPCTSTR szId, long& value);
  void DDX_DHtml_AxControl(CDataExchange* pDX, LPCTSTR szId, DISPID dispid, VARIANT& var);
  void DDX_DHtml_AxControl(CDataExchange* pDX, LPCTSTR szId, LPCTSTR szPropName, VARIANT& var);

  // window.external, and when the page may have it.
  void SetExternalDispatch(IDispatch* pdispExternal);
  virtual BOOL CanAccessExternal();
  virtual BOOL IsExternalDispatchSafe();
  void SetHostFlags(DWORD dwFlags);

  // The browser's questions to its host (IDocHostUIHandler).
  STDMETHOD(ShowContextMenu)(DWORD dwID, POINT* ppt, IUnknown* pcmdtReserved,
                             IDispatch* pdispReserved);
  STDMETHOD(GetHostInfo)(DOCHOSTUIINFO* pInfo);
  STDMETHOD(ShowUI)(DWORD dwID, IOleInPlaceActiveObject* pActiveObject,
                    IOleCommandTarget* pCommandTarget, IOleInPlaceFrame* pFrame,
                    IOleInPlaceUIWindow* pDoc);
  STDMETHOD(HideUI)();
  STDMETHOD(UpdateUI)();
  STDMETHOD(EnableModeless)(BOOL fEnable);
  STDMETHOD(OnDocWindowActivate)(BOOL fActivate);
  STDMETHOD(OnFrameWindowActivate)(BOOL fActivate);
  STDMETHOD(ResizeBorder)(LPCRECT prcBorder, IOleInPlaceUIWindow* pUIWindow,
                          BOOL fFrameWindow);
  STDMETHOD(TranslateAccelerator)(LPMSG lpMsg, const GUID* pguidCmdGroup, DWORD nCmdID);
  STDMETHOD(GetOptionKeyPath)(LPOLESTR* pchKey, DWORD dw);
  STDMETHOD(GetDropTarget)(IDropTarget* pDropTarget, IDropTarget** ppDropTarget);
  STDMETHOD(GetExternal)(IDispatch** ppDispatch);
  STDMETHOD(TranslateUrl)(DWORD dwTranslate, OLECHAR* pchURLIn, OLECHAR** ppchURLOut);
  STDMETHOD(FilterDataObject)(IDataObject* pDO, IDataObject** ppDORet);

  CString m_strCurrentUrl;
  LPTSTR m_szHtmlResID;
  UINT m_nHtmlResID;
  CComPtr<IWebBrowser2> m_pBrowserApp;
  CComPtr<IHTMLDocument2> m_spHtmlDoc;
  CWnd m_wndBrowser;
  BOOL m_bUseHtmlTitle;  // the dialog takes the page's title

protected:
  void Initialize();
  void _OnBeforeNavigate2(LPDISPATCH pDisp, VARIANT* URL, VARIANT* Flags,
                          VARIANT* TargetFrameName, VARIANT* PostData, VARIANT* Headers,
                          BOOL* Cancel);
  void _OnNavigateComplete2(LPDISPATCH pDisp, VARIANT* URL);
  void _OnDocumentComplete(LPDISPATCH pDisp, VARIANT* URL);
  // The value of a property of an element, for the DDX functions.
  void DDX_DHtml_ElementText(CDataExchange* pDX, LPCTSTR szId, DISPID dispid, CString& value,
                             BOOL bSave);
  void DDX_DHtml_ElementVariant(CDataExchange* pDX, LPCTSTR szId, DISPID dispid, VARIANT& var,
                                VARTYPE vt);

  DWORD m_dwHostFlags;
  CComPtr<IDispatch> m_spExternalDisp;

  afx_msg void OnDestroy();
  DECLARE_DHTML_EVENT_MAP()
  DECLARE_EVENTSINK_MAP()
  DECLARE_MESSAGE_MAP()
};

// ---------------------------------------------------------------------
// CMultiPageDHtmlDialog: a CDHtmlDialog whose pages each have their own
// event map, an embedded map its URL map names.

class CMultiPageDHtmlDialog : public CDHtmlDialog {
  DECLARE_DYNAMIC(CMultiPageDHtmlDialog)
public:
  CMultiPageDHtmlDialog();
  CMultiPageDHtmlDialog(LPCTSTR lpszTemplateName, LPCTSTR szHtmlResID = NULL,
                        CWnd* pParentWnd = NULL);
  CMultiPageDHtmlDialog(UINT nIDTemplate, UINT nHtmlResID = 0, CWnd* pParentWnd = NULL);
  virtual ~CMultiPageDHtmlDialog();

  virtual void OnBeforeNavigate(LPDISPATCH pDisp, LPCTSTR szUrl);
  virtual void OnDocumentComplete(LPDISPATCH pDisp, LPCTSTR szUrl);

  DECLARE_DHTML_URL_EVENT_MAP()

protected:
  // The embedded map of the page at this URL; NULL when none.
  const DHtmlEventMapEntry* FindPageMap(LPCTSTR szUrl);
};

// ---------------------------------------------------------------------
// HTML editing: CHtmlEditCtrlBase, the editing commands of MSHTML's
// design mode, for CHtmlEditCtrl (a control) and CHtmlEditView (a view),
// and CHtmlEditDoc, the document of a CHtmlEditView.

#include <mshtmcid.h>

// The helpers CHtmlEditCtrlBase's functions call.
HRESULT AFXAPI _AfxHtmlEditExec(IHTMLDocument2* pDoc, const GUID* pGuid, long cmdID,
                                long cmdExecOpt, VARIANT* pInVar, VARIANT* pOutVar);
long AFXAPI _AfxHtmlEditQueryStatus(IHTMLDocument2* pDoc, long cmdID);
HRESULT AFXAPI _AfxHtmlEditExecText(IHTMLDocument2* pDoc, long cmdID, LPCTSTR lpsz);
HRESULT AFXAPI _AfxHtmlEditExecBool(IHTMLDocument2* pDoc, long cmdID, bool b);
HRESULT AFXAPI _AfxHtmlEditExecInt(IHTMLDocument2* pDoc, long cmdID, long n);
HRESULT AFXAPI _AfxHtmlEditGetText(IHTMLDocument2* pDoc, long cmdID, CString& str);
HRESULT AFXAPI _AfxHtmlEditGetInt(IHTMLDocument2* pDoc, long cmdID, long& n);
HRESULT AFXAPI _AfxHtmlEditGetBlockFormatNames(IHTMLDocument2* pDoc, CStringArray& sa);
BOOL AFXAPI _AfxHtmlEditSetDesignMode(IHTMLDocument2* pDoc, BOOL bMode);
HRESULT AFXAPI _AfxHtmlEditGetHTML(IUnknown* pDoc, CString& str);
HRESULT AFXAPI _AfxHtmlEditSetHTML(IUnknown* pDoc, LPCTSTR lpszHTML);
HRESULT AFXAPI _AfxHtmlEditNewDocument(IHTMLDocument2* pDoc);
HRESULT AFXAPI _AfxHtmlEditIsDirty(IHTMLDocument2* pDoc);
HRESULT AFXAPI _AfxHtmlEditGetEvent(IHTMLDocument2* pDoc, IHTMLEventObj** ppEventObj);
HRESULT AFXAPI _AfxHtmlEditGetSelectionType(IHTMLDocument2* pDoc, CString& str);

template <class T>
class CHtmlEditCtrlBase {
public:
  // T's document, through its GetDHtmlDocument.
  CComPtr<IHTMLDocument2> _GetDoc() const {
    CComPtr<IHTMLDocument2> sp;
    static_cast<const T*>(this)->GetDHtmlDocument(&sp);
    return sp;
  }

  BOOL SetDesignMode(BOOL bMode) const { return _AfxHtmlEditSetDesignMode(_GetDoc(), bMode); }
  HRESULT ExecCommand(long cmdID, long cmdExecOpt, VARIANT* pInVar = NULL,
                      VARIANT* pOutVar = NULL) const {
    return _AfxHtmlEditExec(_GetDoc(), NULL, cmdID, cmdExecOpt, pInVar, pOutVar);
  }
  HRESULT ExecCommand(const GUID* pGuid, long cmdID, long cmdExecOpt, VARIANT* pInVar = NULL,
                      VARIANT* pOutVar = NULL) const {
    return _AfxHtmlEditExec(_GetDoc(), pGuid, cmdID, cmdExecOpt, pInVar, pOutVar);
  }
  // The command's OLECMDF flags; 0 when there is no document.
  long QueryStatus(long cmdID) const { return _AfxHtmlEditQueryStatus(_GetDoc(), cmdID); }
  HRESULT ExecHelperNN(UINT nID, long nExecOpt = OLECMDEXECOPT_DODEFAULT) const {
    return ExecCommand((long)nID, nExecOpt);
  }
  HRESULT ExecHelperSetVal(UINT nCmdID, LPCTSTR szID = NULL,
                           long nExecOpt = OLECMDEXECOPT_DODEFAULT) const {
    (void)nExecOpt;
    return _AfxHtmlEditExecText(_GetDoc(), (long)nCmdID, szID);
  }

  // The document.
  HRESULT GetDocumentHTML(CString& szHTML) const { return _AfxHtmlEditGetHTML(_GetDoc(), szHTML); }
  HRESULT SetDocumentHTML(LPCTSTR pszHTMLContent) const {
    return _AfxHtmlEditSetHTML(_GetDoc(), pszHTMLContent);
  }
  HRESULT GetDocumentTitle(CString& szTitle) const {
    szTitle.Empty();
    CComPtr<IHTMLDocument2> sp = _GetDoc();
    if (sp == NULL) return E_NOINTERFACE;
    BSTR bstr = NULL;
    HRESULT hr = sp->get_title(&bstr);
    if (SUCCEEDED(hr) && bstr != NULL) szTitle = bstr;
    ::SysFreeString(bstr);
    return hr;
  }
  HRESULT NewDocument() const { return _AfxHtmlEditNewDocument(_GetDoc()); }
  HRESULT RefreshDocument() const { return ExecHelperNN(IDM_REFRESH); }
  // S_OK when the document was changed, S_FALSE when not.
  HRESULT IsDirty() const { return _AfxHtmlEditIsDirty(_GetDoc()); }
  HRESULT SaveAs(LPCTSTR szPath = NULL) const { return ExecHelperSetVal(IDM_SAVEAS, szPath); }
  HRESULT Print(bool bDisplayPrintDlg = false) const {
    return ExecHelperNN(IDM_PRINT, bDisplayPrintDlg ? OLECMDEXECOPT_PROMPTUSER
                                                    : OLECMDEXECOPT_DONTPROMPTUSER);
  }
  HRESULT PrintPreview() const { return ExecHelperNN(IDM_PRINTPREVIEW); }
  HRESULT GetEvent(IHTMLEventObj** ppEventObj) const {
    return _AfxHtmlEditGetEvent(_GetDoc(), ppEventObj);
  }
  HRESULT GetEventSrcElement(IHTMLElement** ppSrcElement) const {
    if (ppSrcElement == NULL) return E_POINTER;
    *ppSrcElement = NULL;
    CComPtr<IHTMLEventObj> spEvent;
    HRESULT hr = GetEvent(&spEvent);
    if (FAILED(hr) || spEvent == NULL) return FAILED(hr) ? hr : E_FAIL;
    return spEvent->get_srcElement(ppSrcElement);
  }
  // "None", "Text" or "Control".
  HRESULT GetSelectionType(CString& szType) const {
    return _AfxHtmlEditGetSelectionType(_GetDoc(), szType);
  }

  // Editing.
  HRESULT Copy() const { return ExecHelperNN(IDM_COPY); }
  HRESULT Cut() const { return ExecHelperNN(IDM_CUT); }
  HRESULT Paste() const { return ExecHelperNN(IDM_PASTE); }
  HRESULT Delete() const { return ExecHelperNN(IDM_DELETE); }
  HRESULT Undo() const { return ExecHelperNN(IDM_UNDO); }
  HRESULT Redo() const { return ExecHelperNN(IDM_REDO); }
  HRESULT SelectAll() const { return ExecHelperNN(IDM_SELECTALL); }
  HRESULT ClearSelection() const { return ExecHelperNN(IDM_CLEARSELECTION); }
  HRESULT RemoveFormat() const { return ExecHelperNN(IDM_REMOVEFORMAT); }

  // Character and paragraph formats.
  HRESULT Bold() const { return ExecHelperNN(IDM_BOLD); }
  HRESULT Italic() const { return ExecHelperNN(IDM_ITALIC); }
  HRESULT Underline() const { return ExecHelperNN(IDM_UNDERLINE); }
  HRESULT StrikeThrough() const { return ExecHelperNN(IDM_STRIKETHROUGH); }
  HRESULT Superscript() const { return ExecHelperNN(IDM_SUPERSCRIPT); }
  HRESULT Subscript() const { return ExecHelperNN(IDM_SUBSCRIPT); }
  HRESULT Font() const { return ExecHelperNN(IDM_FONT); }
  HRESULT GetFontFace(CString& szFace) const {
    return _AfxHtmlEditGetText(_GetDoc(), IDM_FONTNAME, szFace);
  }
  HRESULT SetFontFace(LPCTSTR szFace) const {
    return _AfxHtmlEditExecText(_GetDoc(), IDM_FONTNAME, szFace);
  }
  HRESULT GetFontSize(short& nSize) const {
    long n = 0;
    HRESULT hr = _AfxHtmlEditGetInt(_GetDoc(), IDM_FONTSIZE, n);
    nSize = (short)n;
    return hr;
  }
  HRESULT SetFontSize(unsigned short size) const {
    return _AfxHtmlEditExecInt(_GetDoc(), IDM_FONTSIZE, (long)size);
  }
  HRESULT GetForeColor(int& nColor) const {
    long n = 0;
    HRESULT hr = _AfxHtmlEditGetInt(_GetDoc(), IDM_FORECOLOR, n);
    nColor = (int)n;
    return hr;
  }
  HRESULT SetForeColor(LPCTSTR szColor) const {
    return _AfxHtmlEditExecText(_GetDoc(), IDM_FORECOLOR, szColor);
  }
  HRESULT SetForeColor(int nColor) const {
    return _AfxHtmlEditExecInt(_GetDoc(), IDM_FORECOLOR, (long)nColor);
  }
  HRESULT GetBackColor(int& nColor) const {
    long n = 0;
    HRESULT hr = _AfxHtmlEditGetInt(_GetDoc(), IDM_BACKCOLOR, n);
    nColor = (int)n;
    return hr;
  }
  HRESULT SetBackColor(LPCTSTR szColor) const {
    return _AfxHtmlEditExecText(_GetDoc(), IDM_BACKCOLOR, szColor);
  }
  HRESULT SetBackColor(int nColor) const {
    return _AfxHtmlEditExecInt(_GetDoc(), IDM_BACKCOLOR, (long)nColor);
  }
  HRESULT GetBlockFormat(CString& szFormat) const {
    return _AfxHtmlEditGetText(_GetDoc(), IDM_BLOCKFMT, szFormat);
  }
  HRESULT SetBlockFormat(LPCTSTR szFormat) const {
    return _AfxHtmlEditExecText(_GetDoc(), IDM_BLOCKFMT, szFormat);
  }
  HRESULT GetBlockFormatNames(CStringArray& sa) const {
    return _AfxHtmlEditGetBlockFormatNames(_GetDoc(), sa);
  }
  HRESULT JustifyLeft() const { return ExecHelperNN(IDM_JUSTIFYLEFT); }
  HRESULT JustifyCenter() const { return ExecHelperNN(IDM_JUSTIFYCENTER); }
  HRESULT JustifyRight() const { return ExecHelperNN(IDM_JUSTIFYRIGHT); }
  HRESULT Indent() const { return ExecHelperNN(IDM_INDENT); }
  HRESULT Outdent() const { return ExecHelperNN(IDM_OUTDENT); }
  HRESULT OrderList(LPCTSTR szId = NULL) const { return ExecHelperSetVal(IDM_ORDERLIST, szId); }
  HRESULT UnorderList(LPCTSTR szId = NULL) const {
    return ExecHelperSetVal(IDM_UNORDERLIST, szId);
  }
  HRESULT Paragraph(LPCTSTR szId = NULL) const { return ExecHelperSetVal(IDM_PARAGRAPH, szId); }
  HRESULT PreFormatted(LPCTSTR szId = NULL) const {
    return ExecHelperSetVal(IDM_PREFORMATTED, szId);
  }

  // Links and bookmarks.
  HRESULT SetHyperLink(LPCTSTR szUrl = NULL) const {
    return ExecHelperSetVal(IDM_HYPERLINK, szUrl);
  }
  HRESULT Unlink() const { return ExecHelperNN(IDM_UNLINK); }
  HRESULT Bookmark(LPCTSTR szName = NULL) const { return ExecHelperSetVal(IDM_BOOKMARK, szName); }
  HRESULT Unbookmark() const { return ExecHelperNN(IDM_UNBOOKMARK); }

  // Elements to insert; the string is the new element's id (or URL).
  HRESULT HorizontalLine(LPCTSTR szId = NULL) const {
    return ExecHelperSetVal(IDM_HORIZONTALLINE, szId);
  }
  HRESULT Image(LPCTSTR szUrl = NULL) const { return ExecHelperSetVal(IDM_IMAGE, szUrl); }
  HRESULT IFrame(LPCTSTR szUrl = NULL) const { return ExecHelperSetVal(IDM_IFRAME, szUrl); }
  HRESULT Marquee(LPCTSTR szId = NULL) const { return ExecHelperSetVal(IDM_MARQUEE, szId); }
  HRESULT Button(LPCTSTR szId = NULL) const { return ExecHelperSetVal(IDM_BUTTON, szId); }
  HRESULT CheckBox(LPCTSTR szId = NULL) const { return ExecHelperSetVal(IDM_CHECKBOX, szId); }
  HRESULT RadioButton(LPCTSTR szId = NULL) const {
    return ExecHelperSetVal(IDM_RADIOBUTTON, szId);
  }
  HRESULT DropDownBox(LPCTSTR szId = NULL) const {
    return ExecHelperSetVal(IDM_DROPDOWNBOX, szId);
  }
  HRESULT ListBox(LPCTSTR szId = NULL) const { return ExecHelperSetVal(IDM_LISTBOX, szId); }
  HRESULT TextBox(LPCTSTR szId = NULL) const { return ExecHelperSetVal(IDM_TEXTBOX, szId); }
  HRESULT TextArea(LPCTSTR szId = NULL) const { return ExecHelperSetVal(IDM_TEXTAREA, szId); }
  HRESULT InsertFieldSet(LPCTSTR szId = NULL) const {
    return ExecHelperSetVal(IDM_INSFIELDSET, szId);
  }
  HRESULT InsertInputButton(LPCTSTR szId = NULL) const {
    return ExecHelperSetVal(IDM_INSINPUTBUTTON, szId);
  }
  HRESULT InsertInputHidden(LPCTSTR szId = NULL) const {
    return ExecHelperSetVal(IDM_INSINPUTHIDDEN, szId);
  }
  HRESULT InsertInputImage(LPCTSTR szId = NULL) const {
    return ExecHelperSetVal(IDM_INSINPUTIMAGE, szId);
  }
  HRESULT InsertInputPassword(LPCTSTR szId = NULL) const {
    return ExecHelperSetVal(IDM_INSINPUTPASSWORD, szId);
  }
  HRESULT InsertInputReset(LPCTSTR szId = NULL) const {
    return ExecHelperSetVal(IDM_INSINPUTRESET, szId);
  }
  HRESULT InsertInputSubmit(LPCTSTR szId = NULL) const {
    return ExecHelperSetVal(IDM_INSINPUTSUBMIT, szId);
  }
  HRESULT InsertInputUpload(LPCTSTR szId = NULL) const {
    return ExecHelperSetVal(IDM_INSINPUTUPLOAD, szId);
  }

  // How the editor behaves.
  HRESULT SetAbsolutePosition(bool bNewValue) const {
    return _AfxHtmlEditExecBool(_GetDoc(), IDM_ABSOLUTE_POSITION, bNewValue);
  }
  HRESULT Set2DPosition(bool bNewValue) const {
    return _AfxHtmlEditExecBool(_GetDoc(), IDM_2D_POSITION, bNewValue);
  }
  HRESULT SetAtomicSelection(bool bNewValue) const {
    return _AfxHtmlEditExecBool(_GetDoc(), IDM_ATOMICSELECTION, bNewValue);
  }
  HRESULT SetMultipleSelection(bool bNewValue) const {
    return _AfxHtmlEditExecBool(_GetDoc(), IDM_MULTIPLESELECTION, bNewValue);
  }
  HRESULT SetLiveResize(bool bNewValue) const {
    return _AfxHtmlEditExecBool(_GetDoc(), IDM_LIVERESIZE, bNewValue);
  }
  HRESULT SetOverwriteMode(bool bNewValue) const {
    return _AfxHtmlEditExecBool(_GetDoc(), IDM_OVERWRITE, bNewValue);
  }
  HRESULT SetSnapToGrid(bool bNewValue) const {
    return _AfxHtmlEditExecBool(_GetDoc(), IDM_SNAPTOGRID, bNewValue);
  }
  HRESULT ShowDetails(bool bNewValue) const {
    return _AfxHtmlEditExecBool(_GetDoc(), IDM_SHOWALLTAGS, bNewValue);
  }
  HRESULT ShowBorders(bool bNewValue) const {
    return _AfxHtmlEditExecBool(_GetDoc(), IDM_SHOWZEROBORDERATDESIGNTIME, bNewValue);
  }
  HRESULT SetCSSEditingLevel(short nLevel) const {
    return _AfxHtmlEditExecInt(_GetDoc(), IDM_CSSEDITING_LEVEL, (long)nLevel);
  }
};

// ---------------------------------------------------------------------
// CHtmlEditCtrl: the WebBrowser control in design mode, as a child
// window.

class CHtmlEditCtrl : public CWnd, public CHtmlEditCtrlBase<CHtmlEditCtrl> {
public:
  CHtmlEditCtrl();
  virtual ~CHtmlEditCtrl();

  virtual BOOL Create(LPCTSTR lpszWindowName, DWORD dwStyle, const RECT& rect, CWnd* pParentWnd,
                      int nID, CCreateContext* pContext = NULL);
  BOOL GetDHtmlDocument(IHTMLDocument2** ppDocument) const;
  // The page it starts with.
  virtual LPCTSTR GetStartDocument();

  // Called when the browser has the start page, to put it in design mode.
  virtual void OnDocumentReady();

  virtual BOOL DestroyWindow();

protected:
  IDispatch* m_pBrowserSink;       // the browser's events, for design mode
  IConnectionPoint* m_pSinkPoint;  // where it is connected
  DWORD m_dwSinkCookie;
  void DisconnectBrowser();
  DECLARE_MESSAGE_MAP()
};

// ---------------------------------------------------------------------
// The editing command map of a CHtmlEditView: menu and toolbar commands
// that are MSHTML editing commands, enabled from their QueryStatus.

#define AFX_UI_ELEMTYPE_NORMAL 0
#define AFX_UI_ELEMTYPE_CHECBOX 1  // checked when the command is latched
#define AFX_UI_ELEMTYPE_CHECKBOX AFX_UI_ELEMTYPE_CHECBOX
#define AFX_UI_ELEMTYPE_RADIO 2

typedef void (*AFX_DHTMLEDITCMDFUNC)(CCmdTarget* pTarget, UINT nID);

struct DHtmlEditingCmdMapEntry {
  UINT nCmdID;            // the command (ID_EDIT_COPY...)
  long nDHtmlCmdID;       // MSHTML's (IDM_COPY...)
  UINT nElementType;      // AFX_UI_ELEMTYPE_*
  AFX_DHTMLEDITCMDFUNC pfnCmd;  // a handler to call instead of the command
};

// A handler takes the command's id, or nothing.
template <auto pfn, class M = decltype(pfn)>
struct _AfxDHtmlEditCmd;
template <auto pfn, class C, class R>
struct _AfxDHtmlEditCmd<pfn, R (C::*)()> {
  static void Call(CCmdTarget* p, UINT) { (static_cast<C*>(p)->*pfn)(); }
};
template <auto pfn, class C, class R>
struct _AfxDHtmlEditCmd<pfn, R (C::*)(UINT)> {
  static void Call(CCmdTarget* p, UINT nID) { (static_cast<C*>(p)->*pfn)(nID); }
};

#define DECLARE_DHTMLEDITING_CMDMAP(className) \
public:                                         \
  virtual const DHtmlEditingCmdMapEntry* GetDHtmlEditingCmdMap() const;
#define BEGIN_DHTMLEDITING_CMDMAP(className)                                 \
  const DHtmlEditingCmdMapEntry* className::GetDHtmlEditingCmdMap() const { \
    typedef className theClass;                                              \
    (void)sizeof(theClass*);                                                 \
    static const DHtmlEditingCmdMapEntry _dhtmlEditingEntries[] = {
#define DHTMLEDITING_CMD_ENTRY(cmdID, dhtmlcmdID) \
  {(UINT)(cmdID), (long)(dhtmlcmdID), AFX_UI_ELEMTYPE_NORMAL, (AFX_DHTMLEDITCMDFUNC)NULL},
#define DHTMLEDITING_CMD_ENTRY_TYPE(cmdID, dhtmlcmdID, elemType) \
  {(UINT)(cmdID), (long)(dhtmlcmdID), (UINT)(elemType), (AFX_DHTMLEDITCMDFUNC)NULL},
#define DHTMLEDITING_CMD_ENTRY_FUNC(cmdID, dhtmlcmdID, member_func) \
  {(UINT)(cmdID), (long)(dhtmlcmdID), AFX_UI_ELEMTYPE_NORMAL,         \
   &_AfxDHtmlEditCmd<_AFX_PMF(theClass, member_func)>::Call},
#define DHTMLEDITING_CMD_ENTRY_FUNC_TYPE(cmdID, dhtmlcmdID, member_func, elemType) \
  {(UINT)(cmdID), (long)(dhtmlcmdID), (UINT)(elemType),                            \
   &_AfxDHtmlEditCmd<_AFX_PMF(theClass, member_func)>::Call},
#define END_DHTMLEDITING_CMDMAP()                         \
  {0, 0, 0, (AFX_DHTMLEDITCMDFUNC)NULL}};                 \
  return _dhtmlEditingEntries;                            \
  }

// ---------------------------------------------------------------------
// CHtmlEditView: a CHtmlView whose page is edited.

class CHtmlEditDoc;

class CHtmlEditView : public CHtmlView, public CHtmlEditCtrlBase<CHtmlEditView> {
  DECLARE_DYNCREATE(CHtmlEditView)
public:
  CHtmlEditView();
  virtual ~CHtmlEditView();

  virtual BOOL Create(LPCTSTR lpszClassName, LPCTSTR lpszWindowName, DWORD dwStyle,
                      const RECT& rect, CWnd* pParentWnd, UINT nID,
                      CCreateContext* pContext = NULL);
  BOOL GetDHtmlDocument(IHTMLDocument2** ppDocument) const;
  virtual LPCTSTR GetStartDocument();
  CHtmlEditDoc* GetDocument() const;

  virtual void OnNavigateComplete2(LPCTSTR strURL);
  virtual BOOL OnCmdMsg(UINT nID, int nCode, void* pExtra, AFX_CMDHANDLERINFO* pHandlerInfo);

  DECLARE_DHTMLEDITING_CMDMAP(CHtmlEditView)

protected:
  // The entry for a command, in the class's map or CHtmlEditView's.
  const DHtmlEditingCmdMapEntry* FindEditingCmd(UINT nID) const;
};

// ---------------------------------------------------------------------
// CHtmlEditDoc: the document of a CHtmlEditView; the page is the data.

class CHtmlEditDoc : public CDocument {
  DECLARE_DYNCREATE(CHtmlEditDoc)
public:
  CHtmlEditDoc();
  virtual ~CHtmlEditDoc();

  virtual CHtmlEditView* GetView() const;
  virtual BOOL IsModified();
  virtual BOOL OpenURL(LPCTSTR lpszURL);
  virtual BOOL NewDocument();
  virtual BOOL OnNewDocument();
  virtual BOOL OnOpenDocument(LPCTSTR lpszPathName);
  virtual BOOL OnSaveDocument(LPCTSTR lpszPathName);
  virtual void DeleteContents();
};

#endif  // __AFXHTML_H__
