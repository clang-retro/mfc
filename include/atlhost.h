// atlhost.h - ATL's control host: the "AtlAxWin" window class, whose
// window holds an ActiveX control named by its text (a CLSID, a ProgID,
// a URL or "MSHTML:" and HTML), CAxWindow to make and reach one, dialogs
// whose templates name controls by CLSID (CAxDialogImpl, AtlAxDialogBox)
// and composite controls (CComCompositeControl).
//
// A dialog template's control item of class "{CLSID}" becomes an
// AtlAxWin window whose text is that CLSID; the control's saved state
// comes from the dialog's DLGINIT resource, as MFC reads it (occmgr).
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#ifndef __ATLHOST_H__
#define __ATLHOST_H__

#include "atlwin.h"
#include "atlcom.h"
#include "atliface.h"
#include <ocidl.h>
#include <olectl.h>
#include <exdisp.h>
#include <mshtmhst.h>

#define ATLAXWIN_CLASS "AtlAxWin"
#define ATLAXWINLIC_CLASS "AtlAxWinLic"

#ifndef WM_FORWARDMSG
#define WM_FORWARDMSG 0x037F
#endif

namespace ATL {

// The registered messages a host window answers: its host object and
// its control (neither with a reference added).
inline UINT _AtlAxGetHostMessage() {
  static UINT n = ::RegisterWindowMessage(_T("WM_ATLGETHOST"));
  return n;
}
inline UINT _AtlAxGetControlMessage() {
  static UINT n = ::RegisterWindowMessage(_T("WM_ATLGETCONTROL"));
  return n;
}

// ---------------------------------------------------------------------
// The frame and document windows a control's in-place activation sees.

class ATL_NO_VTABLE CAxUIWindow : public CComObjectRootEx<CComSingleThreadModel>,
                                  public IOleInPlaceFrame {
public:
  HWND m_hWnd;
  CComPtr<IOleInPlaceActiveObject> m_spActiveObject;
  CAxUIWindow() : m_hWnd(NULL) {}

  BEGIN_COM_MAP(CAxUIWindow)
    COM_INTERFACE_ENTRY(IOleInPlaceFrame)
    COM_INTERFACE_ENTRY(IOleInPlaceUIWindow)
    COM_INTERFACE_ENTRY(IOleWindow)
  END_COM_MAP()

  STDMETHOD(GetWindow)(HWND* phwnd) override {
    if (phwnd == NULL) return E_POINTER;
    *phwnd = m_hWnd;
    return S_OK;
  }
  STDMETHOD(ContextSensitiveHelp)(BOOL) override { return S_OK; }
  STDMETHOD(GetBorder)(LPRECT) override { return INPLACE_E_NOTOOLSPACE; }
  STDMETHOD(RequestBorderSpace)(LPCBORDERWIDTHS) override { return INPLACE_E_NOTOOLSPACE; }
  STDMETHOD(SetBorderSpace)(LPCBORDERWIDTHS) override { return S_OK; }
  STDMETHOD(SetActiveObject)(IOleInPlaceActiveObject* pActiveObject, LPCOLESTR) override {
    m_spActiveObject = pActiveObject;
    return S_OK;
  }
  STDMETHOD(InsertMenus)(HMENU, LPOLEMENUGROUPWIDTHS) override { return S_OK; }
  STDMETHOD(SetMenu)(HMENU, HOLEMENU, HWND) override { return S_OK; }
  STDMETHOD(RemoveMenus)(HMENU) override { return S_OK; }
  STDMETHOD(SetStatusText)(LPCOLESTR) override { return S_OK; }
  STDMETHOD(EnableModeless)(BOOL) override { return S_OK; }
  STDMETHOD(TranslateAccelerator)(LPMSG, WORD) override { return S_FALSE; }
};
typedef CAxUIWindow CAxFrameWindow;

// ---------------------------------------------------------------------
// The host: the site of one control in a window it subclasses.

class ATL_NO_VTABLE CAxHostWindow : public CComObjectRootEx<CComSingleThreadModel>,
                                    public CWindowImpl<CAxHostWindow>,
                                    public IAxWinHostWindowLic,
                                    public IOleClientSite,
                                    public IOleInPlaceSiteWindowless,
                                    public IOleControlSite,
                                    public IOleContainer,
                                    public IObjectWithSiteImpl<CAxHostWindow>,
                                    public IServiceProvider,
                                    public IAdviseSink,
                                    public IDocHostUIHandler,
                                    public IAxWinAmbientDispatchEx {
public:
  // The host's own dispatch ids for its properties that are not standard
  // ambients.
  enum {
    _DISPID_ALLOWWINDOWLESS = 1,
    _DISPID_DOCHOSTFLAGS,
    _DISPID_DOCHOSTDBLCLK,
    _DISPID_ALLOWCONTEXTMENU,
    _DISPID_ALLOWSHOWUI,
    _DISPID_OPTIONKEYPATH
  };

  CAxHostWindow()
      : m_dwOleObject(0), m_dwMiscStatus(0), m_dwAdviseSink(0), m_iidSink(IID_NULL),
        m_bInPlaceActive(FALSE), m_bUIActive(FALSE), m_bWindowless(FALSE), m_bCapture(FALSE),
        m_bHaveFocus(FALSE), m_bReleaseAll(FALSE), m_bOwnWindow(FALSE), m_bInFocus(FALSE),
        m_pFrame(NULL), m_pUIWindow(NULL), m_bCanWindowlessActivate(TRUE),
        m_clrBackground(0x80000000 | COLOR_BTNFACE), m_clrForeground(0x80000000 | COLOR_WINDOWTEXT),
        m_lcidLocaleID(LOCALE_USER_DEFAULT), m_bUserMode(TRUE), m_bDisplayAsDefault(FALSE),
        m_bMessageReflect(TRUE), m_dwDocHostFlags(DOCHOSTUIFLAG_NO3DBORDER),
        m_dwDocHostDoubleClickFlags(DOCHOSTUIDBLCLK_DEFAULT), m_bAllowContextMenu(TRUE),
        m_bAllowShowUI(FALSE) {
    ::SetRectEmpty(&m_rcPos);
    m_hmSize.cx = m_hmSize.cy = 0;
  }
  void FinalRelease() {
    ReleaseAll();
    if (m_pFrame != NULL) m_pFrame->Release();
    if (m_pUIWindow != NULL) m_pUIWindow->Release();
    m_pFrame = m_pUIWindow = NULL;
  }

  DECLARE_NO_REGISTRY()
  DECLARE_POLY_AGGREGATABLE(CAxHostWindow)
  DECLARE_GET_CONTROLLING_UNKNOWN()

  BEGIN_COM_MAP(CAxHostWindow)
    COM_INTERFACE_ENTRY2(IDispatch, IAxWinAmbientDispatchEx)
    COM_INTERFACE_ENTRY(IAxWinHostWindow)
    COM_INTERFACE_ENTRY(IAxWinHostWindowLic)
    COM_INTERFACE_ENTRY(IOleClientSite)
    COM_INTERFACE_ENTRY(IOleInPlaceSiteWindowless)
    COM_INTERFACE_ENTRY(IOleInPlaceSiteEx)
    COM_INTERFACE_ENTRY(IOleInPlaceSite)
    COM_INTERFACE_ENTRY2(IOleWindow, IOleInPlaceSiteWindowless)
    COM_INTERFACE_ENTRY(IOleControlSite)
    COM_INTERFACE_ENTRY(IOleContainer)
    COM_INTERFACE_ENTRY2(IParseDisplayName, IOleContainer)
    COM_INTERFACE_ENTRY(IObjectWithSite)
    COM_INTERFACE_ENTRY(IServiceProvider)
    COM_INTERFACE_ENTRY(IAdviseSink)
    COM_INTERFACE_ENTRY(IDocHostUIHandler)
    COM_INTERFACE_ENTRY(IAxWinAmbientDispatchEx)
    COM_INTERFACE_ENTRY(IAxWinAmbientDispatch)
  END_COM_MAP()

  // ---- the control
  CComPtr<IUnknown> m_spUnknown;
  CComPtr<IOleObject> m_spOleObject;
  CComPtr<IOleInPlaceObjectWindowless> m_spInPlaceObjectWindowless;  // IOleInPlaceObject at least
  CComPtr<IViewObject> m_spViewObject;
  DWORD m_dwOleObject;
  DWORD m_dwMiscStatus;
  DWORD m_dwAdviseSink;
  IID m_iidSink;
  CComPtr<IUnknown> m_spUnkSink;
  RECT m_rcPos;
  SIZEL m_hmSize;
  BOOL m_bInPlaceActive;
  BOOL m_bUIActive;
  BOOL m_bWindowless;
  BOOL m_bCapture;
  BOOL m_bHaveFocus;
  BOOL m_bReleaseAll;
  BOOL m_bOwnWindow;  // the window holds a reference to the host
  BOOL m_bInFocus;
  CComObject<CAxUIWindow>* m_pFrame;
  CComObject<CAxUIWindow>* m_pUIWindow;

  // ---- the ambient properties
  BOOL m_bCanWindowlessActivate;
  OLE_COLOR m_clrBackground;
  OLE_COLOR m_clrForeground;
  LCID m_lcidLocaleID;
  BOOL m_bUserMode;
  BOOL m_bDisplayAsDefault;
  CComPtr<IFontDisp> m_spFont;
  BOOL m_bMessageReflect;
  DWORD m_dwDocHostFlags;
  DWORD m_dwDocHostDoubleClickFlags;
  BOOL m_bAllowContextMenu;
  BOOL m_bAllowShowUI;
  CComBSTR m_bstrOptionKeyPath;
  CComPtr<IDispatch> m_spAmbientDispatch;  // the caller's, answering first
  CComPtr<IDispatch> m_spExternalDispatch;
  CComPtr<IDocHostUIHandlerDispatch> m_spIDocHostUIHandlerDispatch;

  // Lets go of the control: deactivated, closed and unadvised.
  void ReleaseAll() {
    if (m_bReleaseAll) return;
    m_bReleaseAll = TRUE;
    if (m_spViewObject != NULL) m_spViewObject->SetAdvise(DVASPECT_CONTENT, 0, NULL);
    if (m_spUnkSink != NULL && m_spUnknown != NULL) AtlUnadvise(m_spUnknown, m_iidSink, m_dwAdviseSink);
    m_spUnkSink.Release();
    if (m_spOleObject != NULL) {
      if (m_dwOleObject != 0) m_spOleObject->Unadvise(m_dwOleObject);
      m_spOleObject->Close(OLECLOSE_NOSAVE);
      m_spOleObject->SetClientSite(NULL);
    } else if (m_spUnknown != NULL) {
      CComPtr<IObjectWithSite> spSite;
      if (SUCCEEDED(m_spUnknown->QueryInterface(IID_IObjectWithSite, (void**)&spSite)))
        spSite->SetSite(NULL);
    }
    m_dwOleObject = 0;
    m_spViewObject.Release();
    m_spInPlaceObjectWindowless.Release();
    m_spOleObject.Release();
    m_spUnknown.Release();
    if (m_pFrame != NULL) m_pFrame->m_spActiveObject.Release();
    if (m_pUIWindow != NULL) m_pUIWindow->m_spActiveObject.Release();
    m_bInPlaceActive = m_bUIActive = m_bWindowless = FALSE;
    m_bReleaseAll = FALSE;
  }

  // ---- IAxWinHostWindow(Lic)
  STDMETHOD(CreateControl)(LPCOLESTR lpTricsData, HWND hWnd, IStream* pStream) override {
    return CreateControlLicEx(lpTricsData, hWnd, pStream, NULL, IID_NULL, NULL, NULL);
  }
  STDMETHOD(CreateControlEx)(LPCOLESTR lpTricsData, HWND hWnd, IStream* pStream, IUnknown** ppUnk,
                             REFIID riidAdvise, IUnknown* punkAdvise) override {
    return CreateControlLicEx(lpTricsData, hWnd, pStream, ppUnk, riidAdvise, punkAdvise, NULL);
  }
  STDMETHOD(CreateControlLic)(LPCOLESTR lpTricsData, HWND hWnd, IStream* pStream,
                              BSTR bstrLic) override {
    return CreateControlLicEx(lpTricsData, hWnd, pStream, NULL, IID_NULL, NULL, bstrLic);
  }
  STDMETHOD(CreateControlLicEx)(LPCOLESTR lpszTricsData, HWND hWnd, IStream* pStream,
                                IUnknown** ppUnk, REFIID riidAdvise, IUnknown* punkSink,
                                BSTR bstrLic) override {
    if (ppUnk != NULL) *ppUnk = NULL;
    HRESULT hr = _AttachWindow(hWnd);
    if (FAILED(hr)) return hr;
    ReleaseAll();
    if (lpszTricsData == NULL || lpszTricsData[0] == 0) return S_OK;  // a host only

    // What the name names: a CLSID, a ProgID, else a URL or HTML.
    CLSID clsid;
    bool bURL = false, bHTML = false;
    LPCOLESTR pszHTML = NULL;
    if (_StartsWith(lpszTricsData, L"MSHTML:")) {
      bHTML = true;
      pszHTML = lpszTricsData + 7;
      // CLSID_HTMLDocument
      static const CLSID clsidHTML = {0x25336920, 0x03f9, 0x11cf, {0x8f, 0xd0, 0x00, 0xaa, 0x00, 0x68, 0x6f, 0x13}};
      clsid = clsidHTML;
    } else if (lpszTricsData[0] == L'{' && SUCCEEDED(::CLSIDFromString((LPOLESTR)lpszTricsData, &clsid))) {
    } else if (SUCCEEDED(::CLSIDFromProgID(lpszTricsData, &clsid))) {
    } else {
      bURL = true;
      clsid = CLSID_WebBrowser;
    }
    CComPtr<IUnknown> spUnk;
    if (bstrLic != NULL && ::SysStringLen(bstrLic) > 0) {
      CComPtr<IClassFactory2> spCF2;
      hr = ::CoGetClassObject(clsid, CLSCTX_SERVER, NULL, IID_IClassFactory2, (void**)&spCF2);
      if (SUCCEEDED(hr)) hr = spCF2->CreateInstanceLic(NULL, NULL, IID_IUnknown, bstrLic, (void**)&spUnk);
    } else {
      hr = ::CoCreateInstance(clsid, NULL, CLSCTX_SERVER, IID_IUnknown, (void**)&spUnk);
    }
    if (FAILED(hr)) return hr;

    if (bHTML) {
      // The HTML, loaded as the document's stream.
      IStream* pHTML = NULL;
      int cb = ::WideCharToMultiByte(CP_ACP, 0, pszHTML, -1, NULL, 0, NULL, NULL);
      HGLOBAL h = ::GlobalAlloc(GMEM_MOVEABLE, cb > 0 ? cb : 1);
      if (h != NULL) {
        char* p = (char*)::GlobalLock(h);
        ::WideCharToMultiByte(CP_ACP, 0, pszHTML, -1, p, cb, NULL, NULL);
        ::GlobalUnlock(h);
        if (FAILED(::CreateStreamOnHGlobal(h, TRUE, &pHTML))) ::GlobalFree(h);
      }
      hr = ActivateAx(spUnk, false, pHTML);
      if (pHTML != NULL) pHTML->Release();
    } else {
      hr = ActivateAx(spUnk, false, pStream);
    }
    if (FAILED(hr)) return hr;
    if (bURL) {
      CComPtr<IWebBrowser2> spWB;
      if (SUCCEEDED(spUnk->QueryInterface(IID_IWebBrowser2, (void**)&spWB))) {
        CComVariant ve;
        CComBSTR url(lpszTricsData);
        spWB->Navigate(url, &ve, &ve, &ve, &ve);
      }
    }
    if (punkSink != NULL && !InlineIsEqualGUID(riidAdvise, IID_NULL)) {
      if (SUCCEEDED(AtlAdvise(spUnk, punkSink, riidAdvise, &m_dwAdviseSink))) {
        m_iidSink = riidAdvise;
        m_spUnkSink = punkSink;
      }
    }
    if (ppUnk != NULL) {
      *ppUnk = spUnk;
      spUnk->AddRef();
    }
    return S_OK;
  }
  STDMETHOD(AttachControl)(IUnknown* pUnkControl, HWND hWnd) override {
    if (pUnkControl == NULL) return E_POINTER;
    HRESULT hr = _AttachWindow(hWnd);
    if (FAILED(hr)) return hr;
    ReleaseAll();
    return ActivateAx(pUnkControl, true, NULL);
  }
  STDMETHOD(QueryControl)(REFIID riid, void** ppvObject) override {
    if (ppvObject == NULL) return E_POINTER;
    *ppvObject = NULL;
    if (m_spUnknown == NULL) return E_NOINTERFACE;
    return m_spUnknown->QueryInterface(riid, ppvObject);
  }
  STDMETHOD(SetExternalDispatch)(IDispatch* pDisp) override {
    m_spExternalDispatch = pDisp;
    return S_OK;
  }
  STDMETHOD(SetExternalUIHandler)(IDocHostUIHandlerDispatch* pDisp) override {
    m_spIDocHostUIHandlerDispatch = pDisp;
    return S_OK;
  }

  // Sets the control up in the window: its saved state (or a new one),
  // this site, the advises, its size, then in-place activation.
  HRESULT ActivateAx(IUnknown* pUnkControl, bool bInited, IStream* pStream) {
    if (pUnkControl == NULL) return S_OK;
    m_spUnknown = pUnkControl;
    m_dwMiscStatus = 0;
    m_spUnknown->QueryInterface(IID_IOleObject, (void**)&m_spOleObject);
    IOleClientSite* pSite = static_cast<IOleClientSite*>(this);
    if (m_spOleObject != NULL) {
      m_spOleObject->GetMiscStatus(DVASPECT_CONTENT, &m_dwMiscStatus);
      if (m_dwMiscStatus & OLEMISC_SETCLIENTSITEFIRST) m_spOleObject->SetClientSite(pSite);
    }
    HRESULT hr = S_OK;
    if (!bInited) hr = _InitState(pStream);
    if (FAILED(hr)) {
      if (m_spOleObject != NULL) m_spOleObject->SetClientSite(NULL);
      m_spOleObject.Release();
      m_spUnknown.Release();
      return hr;
    }
    if (m_spOleObject != NULL) {
      if (!(m_dwMiscStatus & OLEMISC_SETCLIENTSITEFIRST)) m_spOleObject->SetClientSite(pSite);
      m_spOleObject->Advise(static_cast<IAdviseSink*>(this), &m_dwOleObject);
      if (SUCCEEDED(m_spUnknown->QueryInterface(IID_IViewObjectEx, (void**)&m_spViewObject)) ||
          SUCCEEDED(m_spUnknown->QueryInterface(IID_IViewObject2, (void**)&m_spViewObject)) ||
          SUCCEEDED(m_spUnknown->QueryInterface(IID_IViewObject, (void**)&m_spViewObject)))
        m_spViewObject->SetAdvise(DVASPECT_CONTENT, 0, static_cast<IAdviseSink*>(this));
      // The window's size.
      if (m_hWnd != NULL) {
        ::GetClientRect(m_hWnd, &m_rcPos);
        SIZEL sizePix = {m_rcPos.right - m_rcPos.left, m_rcPos.bottom - m_rcPos.top};
        _PixelToHiMetric(&sizePix, &m_hmSize);
        m_spOleObject->SetExtent(DVASPECT_CONTENT, &m_hmSize);
        m_spOleObject->GetExtent(DVASPECT_CONTENT, &m_hmSize);
        if (!(m_dwMiscStatus & OLEMISC_INVISIBLEATRUNTIME) || !m_bUserMode)
          hr = m_spOleObject->DoVerb(OLEIVERB_INPLACEACTIVATE, NULL, pSite, 0, m_hWnd, &m_rcPos);
        ::InvalidateRect(m_hWnd, NULL, TRUE);
      }
    } else {
      CComPtr<IObjectWithSite> spSite;
      if (SUCCEEDED(m_spUnknown->QueryInterface(IID_IObjectWithSite, (void**)&spSite)))
        spSite->SetSite(GetControllingUnknown());
    }
    return S_OK;
  }

  // ---- IOleClientSite
  STDMETHOD(SaveObject)() override { return E_NOTIMPL; }
  STDMETHOD(GetMoniker)(DWORD, DWORD, IMoniker** ppmk) override {
    if (ppmk != NULL) *ppmk = NULL;
    return E_NOTIMPL;
  }
  STDMETHOD(GetContainer)(IOleContainer** ppContainer) override {
    if (ppContainer == NULL) return E_POINTER;
    return _InternalQueryInterface(IID_IOleContainer, (void**)ppContainer);
  }
  STDMETHOD(ShowObject)() override { return S_OK; }
  STDMETHOD(OnShowWindow)(BOOL) override { return S_OK; }
  STDMETHOD(RequestNewObjectLayout)() override { return E_NOTIMPL; }

  // ---- IOleInPlaceSite, Ex, Windowless
  STDMETHOD(GetWindow)(HWND* phwnd) override {
    if (phwnd == NULL) return E_POINTER;
    *phwnd = m_hWnd;
    return m_hWnd != NULL ? S_OK : E_FAIL;
  }
  STDMETHOD(ContextSensitiveHelp)(BOOL) override { return S_OK; }
  STDMETHOD(CanInPlaceActivate)() override { return S_OK; }
  STDMETHOD(OnInPlaceActivate)() override {
    m_bInPlaceActive = TRUE;
    m_spInPlaceObjectWindowless.Release();
    if (FAILED(m_spUnknown->QueryInterface(IID_IOleInPlaceObjectWindowless,
                                           (void**)&m_spInPlaceObjectWindowless))) {
      IOleInPlaceObject* p = NULL;
      if (SUCCEEDED(m_spUnknown->QueryInterface(IID_IOleInPlaceObject, (void**)&p)))
        m_spInPlaceObjectWindowless.p = (IOleInPlaceObjectWindowless*)p;
    }
    if (m_spInPlaceObjectWindowless != NULL)
      m_spInPlaceObjectWindowless->SetObjectRects(&m_rcPos, &m_rcPos);
    return S_OK;
  }
  STDMETHOD(OnUIActivate)() override {
    m_bUIActive = TRUE;
    return S_OK;
  }
  STDMETHOD(GetWindowContext)(IOleInPlaceFrame** ppFrame, IOleInPlaceUIWindow** ppDoc,
                              LPRECT lprcPosRect, LPRECT lprcClipRect,
                              LPOLEINPLACEFRAMEINFO lpFrameInfo) override {
    if (ppFrame == NULL || ppDoc == NULL || lprcPosRect == NULL || lprcClipRect == NULL)
      return E_POINTER;
    *ppFrame = NULL;
    *ppDoc = NULL;
    HWND hwndTop = m_hWnd;
    while (hwndTop != NULL && (::GetWindowLong(hwndTop, GWL_STYLE) & WS_CHILD) &&
           ::GetParent(hwndTop) != NULL)
      hwndTop = ::GetParent(hwndTop);
    if (m_pFrame == NULL) {
      if (FAILED(CComObject<CAxUIWindow>::CreateInstance(&m_pFrame))) return E_OUTOFMEMORY;
      m_pFrame->AddRef();
    }
    if (m_pUIWindow == NULL) {
      if (FAILED(CComObject<CAxUIWindow>::CreateInstance(&m_pUIWindow))) return E_OUTOFMEMORY;
      m_pUIWindow->AddRef();
    }
    m_pFrame->m_hWnd = hwndTop;
    m_pUIWindow->m_hWnd = m_hWnd != NULL ? ::GetParent(m_hWnd) : NULL;
    if (m_pUIWindow->m_hWnd == NULL) m_pUIWindow->m_hWnd = hwndTop;
    m_pFrame->QueryInterface(IID_IOleInPlaceFrame, (void**)ppFrame);
    m_pUIWindow->QueryInterface(IID_IOleInPlaceUIWindow, (void**)ppDoc);
    *lprcPosRect = m_rcPos;
    *lprcClipRect = m_rcPos;
    if (lpFrameInfo != NULL) {
      lpFrameInfo->cb = sizeof(OLEINPLACEFRAMEINFO);
      lpFrameInfo->fMDIApp = FALSE;
      lpFrameInfo->hwndFrame = hwndTop;
      lpFrameInfo->haccel = NULL;
      lpFrameInfo->cAccelEntries = 0;
    }
    return S_OK;
  }
  STDMETHOD(Scroll)(SIZE) override { return E_NOTIMPL; }
  STDMETHOD(OnUIDeactivate)(BOOL) override {
    m_bUIActive = FALSE;
    return S_OK;
  }
  STDMETHOD(OnInPlaceDeactivate)() override { return OnInPlaceDeactivateEx(TRUE); }
  STDMETHOD(DiscardUndoState)() override { return S_OK; }
  STDMETHOD(DeactivateAndUndo)() override {
    if (m_spInPlaceObjectWindowless != NULL) m_spInPlaceObjectWindowless->UIDeactivate();
    return S_OK;
  }
  STDMETHOD(OnPosRectChange)(LPCRECT lprcPosRect) override {
    if (lprcPosRect == NULL) return E_POINTER;
    m_rcPos = *lprcPosRect;
    if (m_spInPlaceObjectWindowless != NULL)
      m_spInPlaceObjectWindowless->SetObjectRects(&m_rcPos, &m_rcPos);
    return S_OK;
  }
  STDMETHOD(OnInPlaceActivateEx)(BOOL* pfNoRedraw, DWORD dwFlags) override {
    if (pfNoRedraw != NULL) *pfNoRedraw = FALSE;
    m_bWindowless = (dwFlags & ACTIVATE_WINDOWLESS) != 0;
    return OnInPlaceActivate();
  }
  STDMETHOD(OnInPlaceDeactivateEx)(BOOL) override {
    m_bInPlaceActive = FALSE;
    m_bWindowless = FALSE;
    m_spInPlaceObjectWindowless.Release();
    return S_OK;
  }
  STDMETHOD(RequestUIActivate)() override { return S_OK; }
  STDMETHOD(CanWindowlessActivate)() override { return m_bCanWindowlessActivate ? S_OK : S_FALSE; }
  STDMETHOD(GetCapture)() override { return m_bCapture ? S_OK : S_FALSE; }
  STDMETHOD(SetCapture)(BOOL fCapture) override {
    if (fCapture) {
      ::SetCapture(m_hWnd);
      m_bCapture = TRUE;
    } else {
      if (m_bCapture && ::GetCapture() == m_hWnd) ::ReleaseCapture();
      m_bCapture = FALSE;
    }
    return S_OK;
  }
  STDMETHOD(GetFocus)() override { return m_bHaveFocus ? S_OK : S_FALSE; }
  STDMETHOD(SetFocus)(BOOL fFocus) override {
    m_bHaveFocus = fFocus;
    if (fFocus && ::GetFocus() != m_hWnd) {
      m_bInFocus = TRUE;
      ::SetFocus(m_hWnd);
      m_bInFocus = FALSE;
    }
    return S_OK;
  }
  STDMETHOD(GetDC)(LPCRECT pRect, DWORD grfFlags, HDC* phDC) override {
    if (phDC == NULL) return E_POINTER;
    *phDC = ::GetDC(m_hWnd);
    if (*phDC == NULL) return E_FAIL;
    if (grfFlags & OLEDC_PAINTBKGND) {
      RECT rc = pRect != NULL ? *pRect : m_rcPos;
      _FillBackground(*phDC, &rc);
    }
    return S_OK;
  }
  STDMETHOD(ReleaseDC)(HDC hDC) override {
    ::ReleaseDC(m_hWnd, hDC);
    return S_OK;
  }
  STDMETHOD(InvalidateRect)(LPCRECT pRect, BOOL fErase) override {
    ::InvalidateRect(m_hWnd, pRect != NULL ? pRect : &m_rcPos, fErase);
    return S_OK;
  }
  STDMETHOD(InvalidateRgn)(HRGN hRGN, BOOL fErase) override {
    if (hRGN == NULL) return InvalidateRect(NULL, fErase);
    ::InvalidateRgn(m_hWnd, hRGN, fErase);
    return S_OK;
  }
  STDMETHOD(ScrollRect)(INT, INT, LPCRECT pRectScroll, LPCRECT) override {
    return InvalidateRect(pRectScroll, FALSE);
  }
  STDMETHOD(AdjustRect)(LPRECT) override { return S_OK; }
  STDMETHOD(OnDefWindowMessage)(UINT msg, WPARAM wParam, LPARAM lParam, LRESULT* plResult) override {
    LRESULT l = DefWindowProc(msg, wParam, lParam);
    if (plResult != NULL) *plResult = l;
    return S_OK;
  }

  // ---- IOleControlSite
  STDMETHOD(OnControlInfoChanged)() override { return S_OK; }
  STDMETHOD(LockInPlaceActive)(BOOL) override { return S_OK; }
  STDMETHOD(GetExtendedControl)(IDispatch** ppDisp) override {
    if (ppDisp == NULL) return E_POINTER;
    *ppDisp = NULL;
    return E_NOTIMPL;
  }
  STDMETHOD(TransformCoords)(POINTL* pPtlHimetric, POINTF* pPtfContainer, DWORD dwFlags) override {
    if (pPtlHimetric == NULL || pPtfContainer == NULL) return E_POINTER;
    HDC hdc = ::GetDC(m_hWnd);
    int nX = ::GetDeviceCaps(hdc, LOGPIXELSX), nY = ::GetDeviceCaps(hdc, LOGPIXELSY);
    ::ReleaseDC(m_hWnd, hdc);
    if (dwFlags & XFORMCOORDS_HIMETRICTOCONTAINER) {
      pPtfContainer->x = (float)pPtlHimetric->x * nX / 2540.0f;
      pPtfContainer->y = (float)pPtlHimetric->y * nY / 2540.0f;
    } else if (dwFlags & XFORMCOORDS_CONTAINERTOHIMETRIC) {
      pPtlHimetric->x = (LONG)(pPtfContainer->x * 2540.0f / nX + 0.5f);
      pPtlHimetric->y = (LONG)(pPtfContainer->y * 2540.0f / nY + 0.5f);
    } else {
      return E_INVALIDARG;
    }
    return S_OK;
  }
  STDMETHOD(TranslateAccelerator)(MSG*, DWORD) override { return S_FALSE; }
  STDMETHOD(OnFocus)(BOOL fGotFocus) override {
    m_bHaveFocus = fGotFocus;
    return S_OK;
  }
  STDMETHOD(ShowPropertyFrame)() override { return E_NOTIMPL; }

  // ---- IOleContainer
  STDMETHOD(ParseDisplayName)(IBindCtx*, LPOLESTR, ULONG*, IMoniker**) override { return E_NOTIMPL; }
  STDMETHOD(EnumObjects)(DWORD, IEnumUnknown** ppenum) override {
    if (ppenum == NULL) return E_POINTER;
    *ppenum = NULL;
    typedef CComObject<CComEnum<IEnumUnknown, &IID_IEnumUnknown, IUnknown*, _CopyInterface<IUnknown> > >
        enumunk;
    enumunk* p = NULL;
    HRESULT hr = enumunk::CreateInstance(&p);
    if (FAILED(hr)) return hr;
    IUnknown* pArray[1] = {m_spUnknown.p};
    int n = m_spUnknown != NULL ? 1 : 0;
    hr = p->Init(pArray, pArray + n, NULL, AtlFlagCopy);
    if (SUCCEEDED(hr)) hr = p->QueryInterface(IID_IEnumUnknown, (void**)ppenum);
    if (FAILED(hr)) delete p;
    return hr;
  }
  STDMETHOD(LockContainer)(BOOL) override { return S_OK; }

  // ---- IServiceProvider: the services of the host's own site
  STDMETHOD(QueryService)(REFGUID rsid, REFIID riid, void** ppvObj) override {
    if (ppvObj == NULL) return E_POINTER;
    *ppvObj = NULL;
    if (m_spUnkSite == NULL) return E_NOINTERFACE;
    CComPtr<IServiceProvider> spSP;
    if (FAILED(m_spUnkSite->QueryInterface(IID_IServiceProvider, (void**)&spSP))) return E_NOINTERFACE;
    return spSP->QueryService(rsid, riid, ppvObj);
  }

  // ---- IAdviseSink: a windowless control's view, redrawn
  virtual void STDMETHODCALLTYPE OnDataChange(FORMATETC*, STGMEDIUM*) override {}
  virtual void STDMETHODCALLTYPE OnViewChange(DWORD, LONG) override {
    if (m_hWnd != NULL && (m_bWindowless || !m_bInPlaceActive)) ::InvalidateRect(m_hWnd, &m_rcPos, FALSE);
  }
  virtual void STDMETHODCALLTYPE OnRename(IMoniker*) override {}
  virtual void STDMETHODCALLTYPE OnSave() override {}
  virtual void STDMETHODCALLTYPE OnClose() override {}

  // ---- IDocHostUIHandler, through the caller's handler if it gave one
  STDMETHOD(ShowContextMenu)(DWORD dwID, POINT* pptPosition, IUnknown* pCommandTarget,
                             IDispatch* pDispatchObjectHit) override {
    if (!m_bAllowContextMenu) return S_OK;
    if (m_spIDocHostUIHandlerDispatch != NULL && pptPosition != NULL) {
      HRESULT hr = S_FALSE;
      m_spIDocHostUIHandlerDispatch->ShowContextMenu(dwID, pptPosition->x, pptPosition->y,
                                                     pCommandTarget, pDispatchObjectHit, &hr);
      return hr;
    }
    return S_FALSE;
  }
  STDMETHOD(GetHostInfo)(DOCHOSTUIINFO* pInfo) override {
    if (pInfo == NULL) return E_POINTER;
    pInfo->dwFlags = m_dwDocHostFlags;
    pInfo->dwDoubleClick = m_dwDocHostDoubleClickFlags;
    if (m_spIDocHostUIHandlerDispatch != NULL)
      m_spIDocHostUIHandlerDispatch->GetHostInfo(&pInfo->dwFlags, &pInfo->dwDoubleClick);
    return S_OK;
  }
  STDMETHOD(ShowUI)(DWORD dwID, IOleInPlaceActiveObject* pActiveObject,
                    IOleCommandTarget* pCommandTarget, IOleInPlaceFrame* pFrame,
                    IOleInPlaceUIWindow* pDoc) override {
    HRESULT hr = m_bAllowShowUI ? S_FALSE : S_OK;
    if (m_spIDocHostUIHandlerDispatch != NULL)
      m_spIDocHostUIHandlerDispatch->ShowUI(dwID, pActiveObject, pCommandTarget, pFrame, pDoc, &hr);
    return hr;
  }
  STDMETHOD(HideUI)() override {
    return m_spIDocHostUIHandlerDispatch != NULL ? m_spIDocHostUIHandlerDispatch->HideUI() : S_OK;
  }
  STDMETHOD(UpdateUI)() override {
    return m_spIDocHostUIHandlerDispatch != NULL ? m_spIDocHostUIHandlerDispatch->UpdateUI() : S_OK;
  }
  STDMETHOD(EnableModeless)(BOOL fEnable) override {
    return m_spIDocHostUIHandlerDispatch != NULL
               ? m_spIDocHostUIHandlerDispatch->EnableModeless(fEnable ? VARIANT_TRUE : VARIANT_FALSE)
               : S_OK;
  }
  STDMETHOD(OnDocWindowActivate)(BOOL fActivate) override {
    return m_spIDocHostUIHandlerDispatch != NULL
               ? m_spIDocHostUIHandlerDispatch->OnDocWindowActivate(fActivate ? VARIANT_TRUE
                                                                              : VARIANT_FALSE)
               : S_OK;
  }
  STDMETHOD(OnFrameWindowActivate)(BOOL fActivate) override {
    return m_spIDocHostUIHandlerDispatch != NULL
               ? m_spIDocHostUIHandlerDispatch->OnFrameWindowActivate(fActivate ? VARIANT_TRUE
                                                                                : VARIANT_FALSE)
               : S_OK;
  }
  STDMETHOD(ResizeBorder)(LPCRECT prcBorder, IOleInPlaceUIWindow* pUIWindow,
                          BOOL fFrameWindow) override {
    if (m_spIDocHostUIHandlerDispatch == NULL || prcBorder == NULL) return S_OK;
    return m_spIDocHostUIHandlerDispatch->ResizeBorder(prcBorder->left, prcBorder->top,
                                                       prcBorder->right, prcBorder->bottom,
                                                       pUIWindow,
                                                       fFrameWindow ? VARIANT_TRUE : VARIANT_FALSE);
  }
  STDMETHOD(TranslateAccelerator)(LPMSG lpMsg, const GUID* pguidCmdGroup, DWORD nCmdID) override {
    if (m_spIDocHostUIHandlerDispatch == NULL || lpMsg == NULL) return S_FALSE;
    HRESULT hr = S_FALSE;
    CComBSTR bstrGuid;
    if (pguidCmdGroup != NULL) {
      OLECHAR sz[40];
      ::StringFromGUID2(*pguidCmdGroup, sz, 40);
      bstrGuid = sz;
    }
    m_spIDocHostUIHandlerDispatch->TranslateAccelerator(
        (DWORD_PTR)lpMsg->hwnd, lpMsg->message, (DWORD_PTR)lpMsg->wParam, (DWORD_PTR)lpMsg->lParam,
        bstrGuid, nCmdID, &hr);
    return hr;
  }
  STDMETHOD(GetOptionKeyPath)(LPOLESTR* pchKey, DWORD dw) override {
    if (pchKey == NULL) return E_POINTER;
    *pchKey = NULL;
    CComBSTR bstr = m_bstrOptionKeyPath;
    if (m_spIDocHostUIHandlerDispatch != NULL) {
      BSTR b = NULL;
      if (SUCCEEDED(m_spIDocHostUIHandlerDispatch->GetOptionKeyPath(&b, dw)) && b != NULL) {
        bstr.Attach(b);
      }
    }
    if (bstr.Length() == 0) return S_FALSE;
    UINT cb = (bstr.Length() + 1) * sizeof(OLECHAR);
    *pchKey = (LPOLESTR)::CoTaskMemAlloc(cb);
    if (*pchKey == NULL) return E_OUTOFMEMORY;
    memcpy(*pchKey, (BSTR)bstr, cb);
    return S_OK;
  }
  STDMETHOD(GetDropTarget)(IDropTarget* pDropTarget, IDropTarget** ppDropTarget) override {
    if (ppDropTarget == NULL) return E_POINTER;
    *ppDropTarget = NULL;
    if (m_spIDocHostUIHandlerDispatch == NULL) return E_NOTIMPL;
    IUnknown* p = NULL;
    HRESULT hr = m_spIDocHostUIHandlerDispatch->GetDropTarget(pDropTarget, &p);
    if (SUCCEEDED(hr) && p != NULL) {
      hr = p->QueryInterface(IID_IDropTarget, (void**)ppDropTarget);
      p->Release();
    }
    return hr;
  }
  STDMETHOD(GetExternal)(IDispatch** ppDispatch) override {
    if (ppDispatch == NULL) return E_POINTER;
    *ppDispatch = m_spExternalDispatch;
    if (*ppDispatch != NULL) {
      (*ppDispatch)->AddRef();
      return S_OK;
    }
    if (m_spIDocHostUIHandlerDispatch != NULL)
      return m_spIDocHostUIHandlerDispatch->GetExternal(ppDispatch);
    return S_FALSE;
  }
  STDMETHOD(TranslateUrl)(DWORD dwTranslate, OLECHAR* pchURLIn, OLECHAR** ppchURLOut) override {
    if (ppchURLOut == NULL) return E_POINTER;
    *ppchURLOut = NULL;
    if (m_spIDocHostUIHandlerDispatch == NULL) return S_FALSE;
    CComBSTR in(pchURLIn);
    BSTR out = NULL;
    HRESULT hr = m_spIDocHostUIHandlerDispatch->TranslateUrl(dwTranslate, in, &out);
    if (hr == S_OK && out != NULL) {
      UINT cb = (::SysStringLen(out) + 1) * sizeof(OLECHAR);
      *ppchURLOut = (OLECHAR*)::CoTaskMemAlloc(cb);
      if (*ppchURLOut != NULL) memcpy(*ppchURLOut, out, cb);
    } else {
      hr = S_FALSE;
    }
    ::SysFreeString(out);
    return hr;
  }
  STDMETHOD(FilterDataObject)(IDataObject* pDO, IDataObject** ppDORet) override {
    if (ppDORet == NULL) return E_POINTER;
    *ppDORet = NULL;
    if (m_spIDocHostUIHandlerDispatch == NULL) return S_FALSE;
    IUnknown* p = NULL;
    HRESULT hr = m_spIDocHostUIHandlerDispatch->FilterDataObject(pDO, &p);
    if (hr == S_OK && p != NULL) hr = p->QueryInterface(IID_IDataObject, (void**)ppDORet);
    if (p != NULL) p->Release();
    return hr;
  }

  // ---- the ambient properties, by vtable
  STDMETHOD(put_AllowWindowlessActivation)(VARIANT_BOOL b) override {
    m_bCanWindowlessActivate = b != VARIANT_FALSE;
    return S_OK;
  }
  STDMETHOD(get_AllowWindowlessActivation)(VARIANT_BOOL* pb) override { return _GetBool(m_bCanWindowlessActivate, pb); }
  STDMETHOD(put_BackColor)(OLE_COLOR clr) override {
    m_clrBackground = clr;
    _AmbientChanged(DISPID_AMBIENT_BACKCOLOR);
    return S_OK;
  }
  STDMETHOD(get_BackColor)(OLE_COLOR* pclr) override {
    if (pclr == NULL) return E_POINTER;
    *pclr = m_clrBackground;
    return S_OK;
  }
  STDMETHOD(put_ForeColor)(OLE_COLOR clr) override {
    m_clrForeground = clr;
    _AmbientChanged(DISPID_AMBIENT_FORECOLOR);
    return S_OK;
  }
  STDMETHOD(get_ForeColor)(OLE_COLOR* pclr) override {
    if (pclr == NULL) return E_POINTER;
    *pclr = m_clrForeground;
    return S_OK;
  }
  STDMETHOD(put_LocaleID)(LCID lcid) override {
    m_lcidLocaleID = lcid;
    _AmbientChanged(DISPID_AMBIENT_LOCALEID);
    return S_OK;
  }
  STDMETHOD(get_LocaleID)(LCID* plcid) override {
    if (plcid == NULL) return E_POINTER;
    *plcid = m_lcidLocaleID;
    return S_OK;
  }
  STDMETHOD(put_UserMode)(VARIANT_BOOL b) override {
    m_bUserMode = b != VARIANT_FALSE;
    _AmbientChanged(DISPID_AMBIENT_USERMODE);
    return S_OK;
  }
  STDMETHOD(get_UserMode)(VARIANT_BOOL* pb) override { return _GetBool(m_bUserMode, pb); }
  STDMETHOD(put_DisplayAsDefault)(VARIANT_BOOL b) override {
    m_bDisplayAsDefault = b != VARIANT_FALSE;
    _AmbientChanged(DISPID_AMBIENT_DISPLAYASDEFAULT);
    return S_OK;
  }
  STDMETHOD(get_DisplayAsDefault)(VARIANT_BOOL* pb) override { return _GetBool(m_bDisplayAsDefault, pb); }
  STDMETHOD(put_Font)(IFontDisp* pFont) override {
    m_spFont = pFont;
    _AmbientChanged(DISPID_AMBIENT_FONT);
    return S_OK;
  }
  STDMETHOD(get_Font)(IFontDisp** pFont) override {
    if (pFont == NULL) return E_POINTER;
    *pFont = NULL;
    if (m_spFont == NULL) {
      // The dialog font of the window's parent, else the GUI font.
      HFONT hFont = NULL;
      if (m_hWnd != NULL && ::GetParent(m_hWnd) != NULL)
        hFont = (HFONT)::SendMessage(::GetParent(m_hWnd), WM_GETFONT, 0, 0);
      if (hFont == NULL) hFont = (HFONT)::GetStockObject(DEFAULT_GUI_FONT);
      LOGFONTW lf;
      if (hFont == NULL || ::GetObjectW(hFont, sizeof(lf), &lf) == 0) return E_FAIL;
      HDC hdc = ::GetDC(NULL);
      int nPixY = ::GetDeviceCaps(hdc, LOGPIXELSY);
      ::ReleaseDC(NULL, hdc);
      FONTDESC fd;
      fd.cbSizeofstruct = sizeof(fd);
      fd.lpstrName = lf.lfFaceName;
      LONG lHeight = lf.lfHeight < 0 ? -lf.lfHeight : lf.lfHeight;
      fd.cySize.Lo = (ULONG)::MulDiv(lHeight, 720000, nPixY);
      fd.cySize.Hi = 0;
      fd.sWeight = (SHORT)lf.lfWeight;
      fd.sCharset = lf.lfCharSet;
      fd.fItalic = lf.lfItalic;
      fd.fUnderline = lf.lfUnderline;
      fd.fStrikethrough = lf.lfStrikeOut;
      ::OleCreateFontIndirect(&fd, IID_IFontDisp, (void**)&m_spFont);
    }
    *pFont = m_spFont;
    if (*pFont != NULL) (*pFont)->AddRef();
    return *pFont != NULL ? S_OK : E_FAIL;
  }
  STDMETHOD(put_MessageReflect)(VARIANT_BOOL b) override {
    m_bMessageReflect = b != VARIANT_FALSE;
    _AmbientChanged(DISPID_AMBIENT_MESSAGEREFLECT);
    return S_OK;
  }
  STDMETHOD(get_MessageReflect)(VARIANT_BOOL* pb) override { return _GetBool(m_bMessageReflect, pb); }
  STDMETHOD(get_ShowGrabHandles)(VARIANT_BOOL* pb) override { return _GetBool(FALSE, pb); }
  STDMETHOD(get_ShowHatching)(VARIANT_BOOL* pb) override { return _GetBool(FALSE, pb); }
  STDMETHOD(put_DocHostFlags)(DWORD dw) override {
    m_dwDocHostFlags = dw;
    return S_OK;
  }
  STDMETHOD(get_DocHostFlags)(DWORD* pdw) override {
    if (pdw == NULL) return E_POINTER;
    *pdw = m_dwDocHostFlags;
    return S_OK;
  }
  STDMETHOD(put_DocHostDoubleClickFlags)(DWORD dw) override {
    m_dwDocHostDoubleClickFlags = dw;
    return S_OK;
  }
  STDMETHOD(get_DocHostDoubleClickFlags)(DWORD* pdw) override {
    if (pdw == NULL) return E_POINTER;
    *pdw = m_dwDocHostDoubleClickFlags;
    return S_OK;
  }
  STDMETHOD(put_AllowContextMenu)(VARIANT_BOOL b) override {
    m_bAllowContextMenu = b != VARIANT_FALSE;
    return S_OK;
  }
  STDMETHOD(get_AllowContextMenu)(VARIANT_BOOL* pb) override { return _GetBool(m_bAllowContextMenu, pb); }
  STDMETHOD(put_AllowShowUI)(VARIANT_BOOL b) override {
    m_bAllowShowUI = b != VARIANT_FALSE;
    return S_OK;
  }
  STDMETHOD(get_AllowShowUI)(VARIANT_BOOL* pb) override { return _GetBool(m_bAllowShowUI, pb); }
  STDMETHOD(put_OptionKeyPath)(BSTR bstr) override {
    m_bstrOptionKeyPath = bstr;
    return S_OK;
  }
  STDMETHOD(get_OptionKeyPath)(BSTR* pbstr) override {
    if (pbstr == NULL) return E_POINTER;
    *pbstr = m_bstrOptionKeyPath.Copy();
    return S_OK;
  }
  STDMETHOD(SetAmbientDispatch)(IDispatch* pDisp) override {
    m_spAmbientDispatch = pDisp;
    return S_OK;
  }

  // ---- the ambient properties, by IDispatch (the control's view)
  STDMETHOD(GetTypeInfoCount)(UINT* pctinfo) override {
    if (pctinfo == NULL) return E_POINTER;
    *pctinfo = 0;
    return S_OK;
  }
  STDMETHOD(GetTypeInfo)(UINT, LCID, ITypeInfo** pptinfo) override {
    if (pptinfo != NULL) *pptinfo = NULL;
    return E_NOTIMPL;
  }
  STDMETHOD(GetIDsOfNames)(REFIID, LPOLESTR* rgszNames, UINT cNames, LCID,
                           DISPID* rgdispid) override {
    if (rgszNames == NULL || rgdispid == NULL) return E_POINTER;
    HRESULT hr = S_OK;
    for (UINT i = 0; i < cNames; i++) {
      rgdispid[i] = DISPID_UNKNOWN;
      for (const _AmbientName* p = _AmbientNames(); p->psz != NULL; p++) {
        if (::lstrcmpiW(rgszNames[i], p->psz) == 0) {
          rgdispid[i] = p->dispid;
          break;
        }
      }
      if (rgdispid[i] == DISPID_UNKNOWN) hr = DISP_E_UNKNOWNNAME;
    }
    return hr;
  }
  STDMETHOD(Invoke)(DISPID dispid, REFIID riid, LCID lcid, WORD wFlags, DISPPARAMS* pdp,
                    VARIANT* pvarResult, EXCEPINFO* pexcepinfo, UINT* puArgErr) override {
    if (m_spAmbientDispatch != NULL) {
      HRESULT hr = m_spAmbientDispatch->Invoke(dispid, riid, lcid, wFlags, pdp, pvarResult,
                                               pexcepinfo, puArgErr);
      if (hr != DISP_E_MEMBERNOTFOUND) return hr;
    }
    if (wFlags & DISPATCH_PROPERTYGET) {
      if (pvarResult == NULL) return E_POINTER;
      VariantInit(pvarResult);
      return _GetAmbient(dispid, pvarResult);
    }
    if (wFlags & (DISPATCH_PROPERTYPUT | DISPATCH_PROPERTYPUTREF)) {
      if (pdp == NULL || pdp->cArgs != 1) return DISP_E_BADPARAMCOUNT;
      return _PutAmbient(dispid, &pdp->rgvarg[0]);
    }
    return DISP_E_MEMBERNOTFOUND;
  }

  // ---- the window
  virtual BOOL ProcessWindowMessage(HWND, UINT uMsg, WPARAM wParam, LPARAM lParam,
                                    LRESULT& lResult, DWORD) override {
    if (uMsg == _AtlAxGetHostMessage()) {
      lResult = (LRESULT)GetControllingUnknown();
      return TRUE;
    }
    if (uMsg == _AtlAxGetControlMessage()) {
      lResult = (LRESULT)m_spUnknown.p;
      return TRUE;
    }
    BOOL bHandled = TRUE;
    switch (uMsg) {
    case WM_ERASEBKGND:
      // The control covers the window: windowed, it paints itself;
      // windowless, WM_PAINT paints the background first.
      if (m_spViewObject != NULL && m_bInPlaceActive) {
        lResult = 1;
        return TRUE;
      }
      return FALSE;
    case WM_PAINT:
      if (m_spViewObject != NULL && (m_bWindowless || !m_bInPlaceActive)) {
        PAINTSTRUCT ps;
        HDC hdc = ::BeginPaint(m_hWnd, &ps);
        RECT rcClient;
        ::GetClientRect(m_hWnd, &rcClient);
        _FillBackground(hdc, &rcClient);
        RECTL rcl = {m_rcPos.left, m_rcPos.top, m_rcPos.right, m_rcPos.bottom};
        m_spViewObject->Draw(DVASPECT_CONTENT, -1, NULL, NULL, NULL, hdc, &rcl, NULL, NULL, 0);
        ::EndPaint(m_hWnd, &ps);
        lResult = 0;
        return TRUE;
      }
      return FALSE;
    case WM_SIZE: {
      RECT rc = {0, 0, (short)LOWORD(lParam), (short)HIWORD(lParam)};
      m_rcPos = rc;
      SIZEL sizePix = {rc.right, rc.bottom};
      _PixelToHiMetric(&sizePix, &m_hmSize);
      if (m_spOleObject != NULL) m_spOleObject->SetExtent(DVASPECT_CONTENT, &m_hmSize);
      if (m_spInPlaceObjectWindowless != NULL)
        m_spInPlaceObjectWindowless->SetObjectRects(&m_rcPos, &m_rcPos);
      if (m_bWindowless) ::InvalidateRect(m_hWnd, NULL, TRUE);
      return FALSE;
    }
    case WM_SETFOCUS:
      if (m_bInFocus) return FALSE;
      m_bHaveFocus = TRUE;
      if (m_spOleObject != NULL && !m_bUIActive) {
        m_bInFocus = TRUE;
        m_spOleObject->DoVerb(OLEIVERB_UIACTIVATE, NULL, static_cast<IOleClientSite*>(this), 0,
                              m_hWnd, &m_rcPos);
        m_bInFocus = FALSE;
      }
      if (m_bWindowless && m_spInPlaceObjectWindowless != NULL) {
        m_spInPlaceObjectWindowless->OnWindowMessage(uMsg, wParam, lParam, &lResult);
      } else if (!m_bWindowless && m_spInPlaceObjectWindowless != NULL) {
        HWND hwndCtl = NULL;
        if (SUCCEEDED(m_spInPlaceObjectWindowless->GetWindow(&hwndCtl)) && hwndCtl != NULL &&
            ::GetFocus() == m_hWnd)
          ::SetFocus(hwndCtl);
      }
      return FALSE;
    case WM_KILLFOCUS:
      m_bHaveFocus = FALSE;
      if (m_bWindowless && m_spInPlaceObjectWindowless != NULL)
        m_spInPlaceObjectWindowless->OnWindowMessage(uMsg, wParam, lParam, &lResult);
      return FALSE;
    case WM_FORWARDMSG: {
      // A message of the program's loop, for the control's accelerators.
      lResult = 0;
      IOleInPlaceActiveObject* pAO = NULL;
      if (m_pFrame != NULL && m_pFrame->m_spActiveObject != NULL)
        pAO = m_pFrame->m_spActiveObject;
      else if (m_pUIWindow != NULL && m_pUIWindow->m_spActiveObject != NULL)
        pAO = m_pUIWindow->m_spActiveObject;
      if (pAO != NULL && lParam != 0 && pAO->TranslateAccelerator((LPMSG)lParam) == S_OK) lResult = 1;
      return TRUE;
    }
    case WM_DESTROY:
      ReleaseAll();
      return FALSE;
    default:
      break;
    }
    // A windowless control's input.
    if (m_bWindowless && m_spInPlaceObjectWindowless != NULL && _IsInputMessage(uMsg, lParam)) {
      LRESULT lr = 0;
      if (m_spInPlaceObjectWindowless->OnWindowMessage(uMsg, wParam, lParam, &lr) == S_OK) {
        lResult = lr;
        return TRUE;
      }
    }
    // Notifications from a windowed control's own children, back to it.
    if (m_bMessageReflect) {
      lResult = ReflectNotifications(uMsg, wParam, lParam, bHandled);
      if (bHandled) return TRUE;
    }
    return FALSE;
  }

  virtual void OnFinalMessage(HWND) override {
    if (m_bOwnWindow) {
      m_bOwnWindow = FALSE;
      GetControllingUnknown()->Release();
    }
  }

private:
  // Takes the window (subclassing it), unless it has it already.
  HRESULT _AttachWindow(HWND hWnd) {
    if (hWnd == NULL) return m_hWnd != NULL ? S_OK : E_INVALIDARG;
    if (m_hWnd == hWnd) return S_OK;
    if (m_hWnd != NULL) return E_FAIL;
    if (!SubclassWindow(hWnd)) return E_FAIL;
    // The window keeps the host until its last message.
    m_bOwnWindow = TRUE;
    GetControllingUnknown()->AddRef();
    return S_OK;
  }

  // The control's saved state, or a new one: a stream, or a compound
  // file in the stream (IPersistStorage).
  HRESULT _InitState(IStream* pStream) {
    HRESULT hr;
    if (pStream != NULL) {
      static const BYTE abSig[8] = {0xD0, 0xCF, 0x11, 0xE0, 0xA1, 0xB1, 0x1A, 0xE1};
      BYTE ab[8];
      ULONG cb = 0;
      LARGE_INTEGER li;
      li.QuadPart = 0;
      ULARGE_INTEGER uliStart;
      pStream->Seek(li, STREAM_SEEK_CUR, &uliStart);
      pStream->Read(ab, 8, &cb);
      li.QuadPart = (LONGLONG)uliStart.QuadPart;
      pStream->Seek(li, STREAM_SEEK_SET, NULL);
      if (cb == 8 && memcmp(ab, abSig, 8) == 0) {
        CComPtr<IPersistStorage> spPS;
        if (SUCCEEDED(m_spUnknown->QueryInterface(IID_IPersistStorage, (void**)&spPS))) {
          CComPtr<ILockBytes> spLB;
          hr = ::CreateILockBytesOnHGlobal(NULL, TRUE, &spLB);
          if (SUCCEEDED(hr)) {
            BYTE buf[4096];
            ULARGE_INTEGER off;
            off.QuadPart = 0;
            for (;;) {
              ULONG n = 0;
              if (FAILED(pStream->Read(buf, sizeof(buf), &n)) || n == 0) break;
              spLB->WriteAt(off, buf, n, NULL);
              off.QuadPart += n;
            }
            CComPtr<IStorage> spStg;
            hr = ::StgOpenStorageOnILockBytes(spLB, NULL, STGM_READWRITE | STGM_SHARE_EXCLUSIVE |
                                                              STGM_DIRECT, NULL, 0, &spStg);
            if (SUCCEEDED(hr)) hr = spPS->Load(spStg);
          }
          return hr;
        }
      }
      CComPtr<IPersistStreamInit> spPSI;
      if (SUCCEEDED(m_spUnknown->QueryInterface(IID_IPersistStreamInit, (void**)&spPSI)))
        return spPSI->Load(pStream);
      CComPtr<IPersistStream> spPStm;
      if (SUCCEEDED(m_spUnknown->QueryInterface(IID_IPersistStream, (void**)&spPStm)))
        return spPStm->Load(pStream);
      return S_OK;
    }
    CComPtr<IPersistStreamInit> spPSI;
    if (SUCCEEDED(m_spUnknown->QueryInterface(IID_IPersistStreamInit, (void**)&spPSI)))
      return spPSI->InitNew();
    CComPtr<IPersistStorage> spPS;
    if (SUCCEEDED(m_spUnknown->QueryInterface(IID_IPersistStorage, (void**)&spPS))) {
      CComPtr<ILockBytes> spLB;
      CComPtr<IStorage> spStg;
      hr = ::CreateILockBytesOnHGlobal(NULL, TRUE, &spLB);
      if (SUCCEEDED(hr))
        hr = ::StgCreateDocfileOnILockBytes(spLB, STGM_CREATE | STGM_READWRITE | STGM_SHARE_EXCLUSIVE,
                                            0, &spStg);
      if (SUCCEEDED(hr)) hr = spPS->InitNew(spStg);
      return hr;
    }
    return S_OK;
  }

  void _FillBackground(HDC hdc, const RECT* prc) {
    COLORREF clr = 0;
    if (FAILED(::OleTranslateColor(m_clrBackground, NULL, &clr))) clr = ::GetSysColor(COLOR_BTNFACE);
    HBRUSH hbr = ::CreateSolidBrush(clr);
    ::FillRect(hdc, prc, hbr);
    ::DeleteObject(hbr);
  }

  bool _IsInputMessage(UINT uMsg, LPARAM lParam) {
    if (uMsg >= WM_MOUSEFIRST && uMsg <= WM_MOUSELAST) {
      if (m_bCapture) return true;
      POINT pt = {(short)LOWORD(lParam), (short)HIWORD(lParam)};
      return ::PtInRect(&m_rcPos, pt) != FALSE;
    }
    if (uMsg >= WM_KEYFIRST && uMsg <= WM_KEYLAST) return m_bHaveFocus != FALSE;
    return uMsg == WM_SETCURSOR || uMsg == WM_CANCELMODE || uMsg == WM_HELP ||
           (uMsg >= WM_IME_SETCONTEXT && uMsg <= WM_IME_KEYUP);
  }

  void _AmbientChanged(DISPID dispid) {
    if (m_spUnknown == NULL) return;
    CComPtr<IOleControl> spCtl;
    if (SUCCEEDED(m_spUnknown->QueryInterface(IID_IOleControl, (void**)&spCtl)))
      spCtl->OnAmbientPropertyChange(dispid);
  }

  static HRESULT _GetBool(BOOL b, VARIANT_BOOL* pb) {
    if (pb == NULL) return E_POINTER;
    *pb = b ? VARIANT_TRUE : VARIANT_FALSE;
    return S_OK;
  }
  static bool _StartsWith(LPCOLESTR psz, LPCOLESTR pszPrefix) {
    int n = ::lstrlenW(pszPrefix);
    return ::lstrlenW(psz) >= n &&
           ::CompareStringW(LOCALE_INVARIANT, NORM_IGNORECASE, psz, n, pszPrefix, n) == CSTR_EQUAL;
  }
  static void _PixelToHiMetric(const SIZEL* pPix, SIZEL* pHM) {
    HDC hdc = ::GetDC(NULL);
    int nX = ::GetDeviceCaps(hdc, LOGPIXELSX), nY = ::GetDeviceCaps(hdc, LOGPIXELSY);
    ::ReleaseDC(NULL, hdc);
    pHM->cx = ::MulDiv(pPix->cx, 2540, nX);
    pHM->cy = ::MulDiv(pPix->cy, 2540, nY);
  }

  struct _AmbientName {
    LPCOLESTR psz;
    DISPID dispid;
  };
  static const _AmbientName* _AmbientNames() {
    static const _AmbientName a[] = {
        {L"AllowWindowlessActivation", _DISPID_ALLOWWINDOWLESS},
        {L"BackColor", DISPID_AMBIENT_BACKCOLOR},
        {L"ForeColor", DISPID_AMBIENT_FORECOLOR},
        {L"LocaleID", DISPID_AMBIENT_LOCALEID},
        {L"UserMode", DISPID_AMBIENT_USERMODE},
        {L"DisplayAsDefault", DISPID_AMBIENT_DISPLAYASDEFAULT},
        {L"Font", DISPID_AMBIENT_FONT},
        {L"MessageReflect", DISPID_AMBIENT_MESSAGEREFLECT},
        {L"ShowGrabHandles", DISPID_AMBIENT_SHOWGRABHANDLES},
        {L"ShowHatching", DISPID_AMBIENT_SHOWHATCHING},
        {L"DocHostFlags", _DISPID_DOCHOSTFLAGS},
        {L"DocHostDoubleClickFlags", _DISPID_DOCHOSTDBLCLK},
        {L"AllowContextMenu", _DISPID_ALLOWCONTEXTMENU},
        {L"AllowShowUI", _DISPID_ALLOWSHOWUI},
        {L"OptionKeyPath", _DISPID_OPTIONKEYPATH},
        {NULL, 0}};
    return a;
  }
  HRESULT _GetAmbient(DISPID dispid, VARIANT* pv) {
    switch (dispid) {
    case _DISPID_ALLOWWINDOWLESS: pv->vt = VT_BOOL; return get_AllowWindowlessActivation(&pv->boolVal);
    case DISPID_AMBIENT_BACKCOLOR: pv->vt = VT_I4; return get_BackColor((OLE_COLOR*)&pv->lVal);
    case DISPID_AMBIENT_FORECOLOR: pv->vt = VT_I4; return get_ForeColor((OLE_COLOR*)&pv->lVal);
    case DISPID_AMBIENT_LOCALEID: pv->vt = VT_I4; return get_LocaleID((LCID*)&pv->lVal);
    case DISPID_AMBIENT_USERMODE: pv->vt = VT_BOOL; return get_UserMode(&pv->boolVal);
    case DISPID_AMBIENT_DISPLAYASDEFAULT: pv->vt = VT_BOOL; return get_DisplayAsDefault(&pv->boolVal);
    case DISPID_AMBIENT_FONT: {
      IFontDisp* p = NULL;
      HRESULT hr = get_Font(&p);
      if (SUCCEEDED(hr)) {
        pv->vt = VT_DISPATCH;
        pv->pdispVal = p;
      }
      return hr;
    }
    case DISPID_AMBIENT_MESSAGEREFLECT: pv->vt = VT_BOOL; return get_MessageReflect(&pv->boolVal);
    case DISPID_AMBIENT_SHOWGRABHANDLES: pv->vt = VT_BOOL; return get_ShowGrabHandles(&pv->boolVal);
    case DISPID_AMBIENT_SHOWHATCHING: pv->vt = VT_BOOL; return get_ShowHatching(&pv->boolVal);
    case DISPID_AMBIENT_AUTOCLIP: pv->vt = VT_BOOL; pv->boolVal = VARIANT_TRUE; return S_OK;
    case DISPID_AMBIENT_UIDEAD: pv->vt = VT_BOOL; pv->boolVal = VARIANT_FALSE; return S_OK;
    case DISPID_AMBIENT_SUPPORTSMNEMONICS: pv->vt = VT_BOOL; pv->boolVal = VARIANT_TRUE; return S_OK;
    case _DISPID_DOCHOSTFLAGS: pv->vt = VT_UI4; return get_DocHostFlags(&pv->ulVal);
    case _DISPID_DOCHOSTDBLCLK: pv->vt = VT_UI4; return get_DocHostDoubleClickFlags(&pv->ulVal);
    case _DISPID_ALLOWCONTEXTMENU: pv->vt = VT_BOOL; return get_AllowContextMenu(&pv->boolVal);
    case _DISPID_ALLOWSHOWUI: pv->vt = VT_BOOL; return get_AllowShowUI(&pv->boolVal);
    case _DISPID_OPTIONKEYPATH: pv->vt = VT_BSTR; return get_OptionKeyPath(&pv->bstrVal);
    default: return DISP_E_MEMBERNOTFOUND;
    }
  }
  HRESULT _PutAmbient(DISPID dispid, VARIANT* pArg) {
    CComVariant v;
    VARTYPE vt;
    switch (dispid) {
    case DISPID_AMBIENT_FONT:
      if (pArg->vt != VT_DISPATCH && pArg->vt != VT_UNKNOWN) return DISP_E_TYPEMISMATCH;
      {
        CComPtr<IFontDisp> sp;
        if (pArg->punkVal != NULL) pArg->punkVal->QueryInterface(IID_IFontDisp, (void**)&sp);
        return put_Font(sp);
      }
    case _DISPID_OPTIONKEYPATH: vt = VT_BSTR; break;
    case DISPID_AMBIENT_BACKCOLOR:
    case DISPID_AMBIENT_FORECOLOR:
    case DISPID_AMBIENT_LOCALEID:
    case _DISPID_DOCHOSTFLAGS:
    case _DISPID_DOCHOSTDBLCLK: vt = VT_I4; break;
    case _DISPID_ALLOWWINDOWLESS:
    case DISPID_AMBIENT_USERMODE:
    case DISPID_AMBIENT_DISPLAYASDEFAULT:
    case DISPID_AMBIENT_MESSAGEREFLECT:
    case _DISPID_ALLOWCONTEXTMENU:
    case _DISPID_ALLOWSHOWUI: vt = VT_BOOL; break;
    default: return DISP_E_MEMBERNOTFOUND;
    }
    HRESULT hr = ::VariantChangeType(&v, pArg, 0, vt);
    if (FAILED(hr)) return DISP_E_TYPEMISMATCH;
    switch (dispid) {
    case _DISPID_OPTIONKEYPATH: return put_OptionKeyPath(v.bstrVal);
    case DISPID_AMBIENT_BACKCOLOR: return put_BackColor((OLE_COLOR)v.lVal);
    case DISPID_AMBIENT_FORECOLOR: return put_ForeColor((OLE_COLOR)v.lVal);
    case DISPID_AMBIENT_LOCALEID: return put_LocaleID((LCID)v.lVal);
    case _DISPID_DOCHOSTFLAGS: return put_DocHostFlags((DWORD)v.lVal);
    case _DISPID_DOCHOSTDBLCLK: return put_DocHostDoubleClickFlags((DWORD)v.lVal);
    case _DISPID_ALLOWWINDOWLESS: return put_AllowWindowlessActivation(v.boolVal);
    case DISPID_AMBIENT_USERMODE: return put_UserMode(v.boolVal);
    case DISPID_AMBIENT_DISPLAYASDEFAULT: return put_DisplayAsDefault(v.boolVal);
    case DISPID_AMBIENT_MESSAGEREFLECT: return put_MessageReflect(v.boolVal);
    case _DISPID_ALLOWCONTEXTMENU: return put_AllowContextMenu(v.boolVal);
    default: return put_AllowShowUI(v.boolVal);
    }
  }
};

// ---------------------------------------------------------------------
// The window classes, and the functions over them.

// The control's saved state, from the window's creation data: [WORD
// size][stream] for AtlAxWin; [WORD size][ULONG cchKey][key][stream] for
// AtlAxWinLic.
inline IStream* _AtlAxStreamFromCreateData(const BYTE* p, DWORD cb) {
  if (p == NULL || cb == 0) return NULL;
  HGLOBAL h = ::GlobalAlloc(GMEM_MOVEABLE, cb);
  if (h == NULL) return NULL;
  memcpy(::GlobalLock(h), p, cb);
  ::GlobalUnlock(h);
  IStream* pStream = NULL;
  if (FAILED(::CreateStreamOnHGlobal(h, TRUE, &pStream))) {
    ::GlobalFree(h);
    return NULL;
  }
  return pStream;
}

inline LRESULT _AtlAxWindowCreate(HWND hWnd, LPARAM lParam, bool bLic) {
  CREATESTRUCT* lpcs = (CREATESTRUCT*)lParam;
  int nLen = ::GetWindowTextLengthW(hWnd);
  LPOLESTR pszName = (LPOLESTR)__builtin_alloca((nLen + 1) * sizeof(OLECHAR));
  pszName[0] = 0;
  ::GetWindowTextW(hWnd, pszName, nLen + 1);
  // The saved state and the license key.
  const BYTE* pData = NULL;
  DWORD cbData = 0;
  BSTR bstrLic = NULL;
  if (lpcs != NULL && lpcs->lpCreateParams != NULL) {
    WORD cb;
    memcpy(&cb, lpcs->lpCreateParams, sizeof(WORD));
    pData = (const BYTE*)lpcs->lpCreateParams + sizeof(WORD);
    cbData = cb;
    if (bLic && cbData >= sizeof(ULONG)) {
      ULONG cch;
      memcpy(&cch, pData, sizeof(ULONG));
      pData += sizeof(ULONG);
      cbData -= sizeof(ULONG);
      if (cch * sizeof(OLECHAR) <= cbData) {
        if (cch > 0) bstrLic = ::SysAllocStringLen((const OLECHAR*)pData, cch);
        pData += cch * sizeof(OLECHAR);
        cbData -= cch * sizeof(OLECHAR);
      }
    }
  }
  IStream* pStream = _AtlAxStreamFromCreateData(pData, cbData);
  CComObject<CAxHostWindow>* pHost = NULL;
  HRESULT hr = CComObject<CAxHostWindow>::CreateInstance(&pHost);
  if (SUCCEEDED(hr)) {
    pHost->AddRef();
    hr = pHost->CreateControlLicEx(pszName, hWnd, pStream, NULL, IID_NULL, NULL, bstrLic);
    pHost->Release();  // the window keeps it
  }
  if (pStream != NULL) pStream->Release();
  ::SysFreeString(bstrLic);
  return FAILED(hr) ? -1 : 0;
}

inline LRESULT CALLBACK AtlAxWindowProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
  if (uMsg == WM_CREATE) return _AtlAxWindowCreate(hWnd, lParam, false);
  return ::DefWindowProc(hWnd, uMsg, wParam, lParam);
}
inline LRESULT CALLBACK AtlAxWindowProcLic(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
  if (uMsg == WM_CREATE) return _AtlAxWindowCreate(hWnd, lParam, true);
  return ::DefWindowProc(hWnd, uMsg, wParam, lParam);
}

// The class names: ATL 3's, and those later versions' resources name.
inline const LPCTSTR* _AtlAxWinClassNames(bool bLic) {
  static const LPCTSTR a[] = {_T("AtlAxWin"),   _T("AtlAxWin7"),   _T("AtlAxWin71"),
                              _T("AtlAxWin80"), _T("AtlAxWin90"),  _T("AtlAxWin100"),
                              _T("AtlAxWin110"), _T("AtlAxWin140"), NULL};
  static const LPCTSTR b[] = {_T("AtlAxWinLic"),   _T("AtlAxWinLic7"),   _T("AtlAxWinLic71"),
                              _T("AtlAxWinLic80"), _T("AtlAxWinLic90"),  _T("AtlAxWinLic100"),
                              _T("AtlAxWinLic110"), _T("AtlAxWinLic140"), NULL};
  return bLic ? b : a;
}

// Registers the host window classes in this module (once).
inline BOOL AtlAxWinInit() {
  static bool bDone = false;
  if (bDone) return TRUE;
  _AtlAxGetHostMessage();
  _AtlAxGetControlMessage();
  HINSTANCE hInst = _AtlBaseModule.GetModuleInstance();
  for (int nLic = 0; nLic < 2; nLic++) {
    for (const LPCTSTR* p = _AtlAxWinClassNames(nLic != 0); *p != NULL; p++) {
      WNDCLASSEX wc;
      memset(&wc, 0, sizeof(wc));
      wc.cbSize = sizeof(wc);
      if (::GetClassInfoEx(hInst, *p, &wc)) continue;
      wc.cbSize = sizeof(wc);
      wc.style = CS_DBLCLKS;
      wc.lpfnWndProc = nLic ? AtlAxWindowProcLic : AtlAxWindowProc;
      wc.hInstance = hInst;
      wc.hCursor = ::LoadCursor(NULL, IDC_ARROW);
      wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
      wc.lpszClassName = *p;
      if (::RegisterClassEx(&wc) == 0) return FALSE;
    }
  }
  bDone = true;
  return TRUE;
}
inline BOOL AtlAxWinTerm() {
  HINSTANCE hInst = _AtlBaseModule.GetModuleInstance();
  for (int nLic = 0; nLic < 2; nLic++)
    for (const LPCTSTR* p = _AtlAxWinClassNames(nLic != 0); *p != NULL; p++)
      ::UnregisterClass(*p, hInst);
  return TRUE;
}

inline HRESULT WINAPI AtlAxGetHost(HWND h, IUnknown** pp) {
  if (pp == NULL) return E_POINTER;
  *pp = NULL;
  IUnknown* p = (IUnknown*)::SendMessage(h, _AtlAxGetHostMessage(), 0, 0);
  return p != NULL ? p->QueryInterface(IID_IUnknown, (void**)pp) : E_FAIL;
}
inline HRESULT WINAPI AtlAxGetControl(HWND h, IUnknown** pp) {
  if (pp == NULL) return E_POINTER;
  *pp = NULL;
  IUnknown* p = (IUnknown*)::SendMessage(h, _AtlAxGetControlMessage(), 0, 0);
  return p != NULL ? p->QueryInterface(IID_IUnknown, (void**)pp) : E_FAIL;
}

// The host of hWnd: the one it has, else a new one that subclasses it.
inline HRESULT _AtlAxHostOf(HWND hWnd, IAxWinHostWindowLic** ppHost) {
  *ppHost = NULL;
  CComPtr<IUnknown> spUnk;
  if (SUCCEEDED(AtlAxGetHost(hWnd, &spUnk)))
    return spUnk->QueryInterface(IID_IAxWinHostWindowLic, (void**)ppHost);
  CComObject<CAxHostWindow>* pHost = NULL;
  HRESULT hr = CComObject<CAxHostWindow>::CreateInstance(&pHost);
  if (FAILED(hr)) return hr;
  pHost->AddRef();
  hr = pHost->QueryInterface(IID_IAxWinHostWindowLic, (void**)ppHost);
  pHost->Release();
  return hr;
}

inline HRESULT WINAPI AtlAxCreateControlLicEx(LPCOLESTR lpszName, HWND hWnd, IStream* pStream,
                                              IUnknown** ppUnkContainer, IUnknown** ppUnkControl,
                                              REFIID iidSink, IUnknown* punkSink, BSTR bstrLic) {
  AtlAxWinInit();
  if (ppUnkContainer != NULL) *ppUnkContainer = NULL;
  if (ppUnkControl != NULL) *ppUnkControl = NULL;
  CComPtr<IAxWinHostWindowLic> spHost;
  HRESULT hr = _AtlAxHostOf(hWnd, &spHost);
  if (FAILED(hr)) return hr;
  CComPtr<IUnknown> spControl;
  hr = spHost->CreateControlLicEx(lpszName, hWnd, pStream, &spControl, iidSink, punkSink, bstrLic);
  if (FAILED(hr)) return hr;
  if (ppUnkContainer != NULL) spHost->QueryInterface(IID_IUnknown, (void**)ppUnkContainer);
  if (ppUnkControl != NULL) *ppUnkControl = spControl.Detach();
  return hr;
}
inline HRESULT WINAPI AtlAxCreateControlEx(LPCOLESTR lpszName, HWND hWnd, IStream* pStream,
                                           IUnknown** ppUnkContainer, IUnknown** ppUnkControl,
                                           REFIID iidSink = IID_NULL, IUnknown* punkSink = NULL) {
  return AtlAxCreateControlLicEx(lpszName, hWnd, pStream, ppUnkContainer, ppUnkControl, iidSink,
                                 punkSink, NULL);
}
inline HRESULT WINAPI AtlAxCreateControl(LPCOLESTR lpszName, HWND hWnd, IStream* pStream,
                                         IUnknown** ppUnkContainer) {
  return AtlAxCreateControlEx(lpszName, hWnd, pStream, ppUnkContainer, NULL, IID_NULL, NULL);
}
inline HRESULT WINAPI AtlAxCreateControlLic(LPCOLESTR lpszName, HWND hWnd, IStream* pStream,
                                            IUnknown** ppUnkContainer, BSTR bstrLic) {
  return AtlAxCreateControlLicEx(lpszName, hWnd, pStream, ppUnkContainer, NULL, IID_NULL, NULL,
                                 bstrLic);
}
inline HRESULT WINAPI AtlAxAttachControl(IUnknown* pControl, HWND hWnd, IUnknown** ppUnkContainer) {
  AtlAxWinInit();
  if (ppUnkContainer != NULL) *ppUnkContainer = NULL;
  if (pControl == NULL) return E_INVALIDARG;
  CComPtr<IAxWinHostWindowLic> spHost;
  HRESULT hr = _AtlAxHostOf(hWnd, &spHost);
  if (FAILED(hr)) return hr;
  hr = spHost->AttachControl(pControl, hWnd);
  if (SUCCEEDED(hr) && ppUnkContainer != NULL) spHost->QueryInterface(IID_IUnknown, (void**)ppUnkContainer);
  return hr;
}

// ---------------------------------------------------------------------
// CAxWindow: a CWindow over a host window.

template <class TBase = CWindow>
class CAxWindowT : public TBase {
public:
  CAxWindowT(HWND hWnd = NULL) : TBase(hWnd) { AtlAxWinInit(); }
  CAxWindowT<TBase>& operator=(HWND hWnd) {
    this->m_hWnd = hWnd;
    return *this;
  }
  static LPCTSTR GetWndClassName() { return _T(ATLAXWIN_CLASS); }

  HWND Create(HWND hWndParent, _U_RECT rect = NULL, LPCTSTR szWindowName = NULL, DWORD dwStyle = 0,
              DWORD dwExStyle = 0, _U_MENUorID MenuOrID = 0U, LPVOID lpCreateParam = NULL) {
    return CWindow::Create(GetWndClassName(), hWndParent, rect, szWindowName, dwStyle, dwExStyle,
                           MenuOrID, lpCreateParam);
  }

  HRESULT CreateControl(LPCOLESTR lpszName, IStream* pStream = NULL, IUnknown** ppUnkContainer = NULL) {
    return CreateControlEx(lpszName, pStream, ppUnkContainer);
  }
  HRESULT CreateControl(DWORD dwResID, IStream* pStream = NULL, IUnknown** ppUnkContainer = NULL) {
    return CreateControlEx(dwResID, pStream, ppUnkContainer);
  }
  HRESULT CreateControlEx(LPCOLESTR lpszName, IStream* pStream = NULL,
                          IUnknown** ppUnkContainer = NULL, IUnknown** ppUnkControl = NULL,
                          REFIID iidSink = IID_NULL, IUnknown* punkSink = NULL) {
    return AtlAxCreateControlEx(lpszName, this->m_hWnd, pStream, ppUnkContainer, ppUnkControl,
                                iidSink, punkSink);
  }
  // An HTML resource of this module, by its id.
  HRESULT CreateControlEx(DWORD dwResID, IStream* pStream = NULL, IUnknown** ppUnkContainer = NULL,
                          IUnknown** ppUnkControl = NULL, REFIID iidSink = IID_NULL,
                          IUnknown* punkSink = NULL) {
    WCHAR szModule[MAX_PATH];
    WCHAR szURL[MAX_PATH + 32];
    ::GetModuleFileNameW(_AtlBaseModule.GetModuleInstance(), szModule, MAX_PATH);
    ::wsprintfW(szURL, L"res://%s/%lu", szModule, (unsigned long)dwResID);
    return CreateControlEx(szURL, pStream, ppUnkContainer, ppUnkControl, iidSink, punkSink);
  }
  HRESULT AttachControl(IUnknown* pControl, IUnknown** ppUnkContainer) {
    return AtlAxAttachControl(pControl, this->m_hWnd, ppUnkContainer);
  }
  HRESULT QueryHost(REFIID iid, void** ppUnk) {
    if (ppUnk == NULL) return E_POINTER;
    *ppUnk = NULL;
    CComPtr<IUnknown> spUnk;
    HRESULT hr = AtlAxGetHost(this->m_hWnd, &spUnk);
    return SUCCEEDED(hr) ? spUnk->QueryInterface(iid, ppUnk) : hr;
  }
  template <class Q>
  HRESULT QueryHost(Q** ppUnk) {
    return QueryHost(__uuidof(Q), (void**)ppUnk);
  }
  HRESULT QueryControl(REFIID iid, void** ppUnk) {
    if (ppUnk == NULL) return E_POINTER;
    *ppUnk = NULL;
    CComPtr<IUnknown> spUnk;
    HRESULT hr = AtlAxGetControl(this->m_hWnd, &spUnk);
    return SUCCEEDED(hr) ? spUnk->QueryInterface(iid, ppUnk) : hr;
  }
  template <class Q>
  HRESULT QueryControl(Q** ppUnk) {
    return QueryControl(__uuidof(Q), (void**)ppUnk);
  }
  HRESULT SetExternalDispatch(IDispatch* pDisp) {
    CComPtr<IAxWinHostWindow> spHost;
    HRESULT hr = QueryHost(IID_IAxWinHostWindow, (void**)&spHost);
    return SUCCEEDED(hr) ? spHost->SetExternalDispatch(pDisp) : hr;
  }
  HRESULT SetExternalUIHandler(IDocHostUIHandlerDispatch* pUIHandler) {
    CComPtr<IAxWinHostWindow> spHost;
    HRESULT hr = QueryHost(IID_IAxWinHostWindow, (void**)&spHost);
    return SUCCEEDED(hr) ? spHost->SetExternalUIHandler(pUIHandler) : hr;
  }
};
typedef CAxWindowT<CWindow> CAxWindow;

// The same, for licensed controls.
template <class TBase = CWindow>
class CAxWindow2T : public CAxWindowT<TBase> {
public:
  CAxWindow2T(HWND hWnd = NULL) : CAxWindowT<TBase>(hWnd) {}
  CAxWindow2T<TBase>& operator=(HWND hWnd) {
    this->m_hWnd = hWnd;
    return *this;
  }
  static LPCTSTR GetWndClassName() { return _T(ATLAXWINLIC_CLASS); }
  HWND Create(HWND hWndParent, _U_RECT rect = NULL, LPCTSTR szWindowName = NULL, DWORD dwStyle = 0,
              DWORD dwExStyle = 0, _U_MENUorID MenuOrID = 0U, LPVOID lpCreateParam = NULL) {
    return CWindow::Create(GetWndClassName(), hWndParent, rect, szWindowName, dwStyle, dwExStyle,
                           MenuOrID, lpCreateParam);
  }
  HRESULT CreateControlLic(LPCOLESTR lpszName, IStream* pStream = NULL,
                           IUnknown** ppUnkContainer = NULL, BSTR bstrLicKey = NULL) {
    return AtlAxCreateControlLic(lpszName, this->m_hWnd, pStream, ppUnkContainer, bstrLicKey);
  }
  HRESULT CreateControlLicEx(LPCOLESTR lpszName, IStream* pStream = NULL,
                             IUnknown** ppUnkContainer = NULL, IUnknown** ppUnkControl = NULL,
                             REFIID iidSink = IID_NULL, IUnknown* punkSink = NULL,
                             BSTR bstrLicKey = NULL) {
    return AtlAxCreateControlLicEx(lpszName, this->m_hWnd, pStream, ppUnkContainer, ppUnkControl,
                                   iidSink, punkSink, bstrLicKey);
  }
};
typedef CAxWindow2T<CWindow> CAxWindow2;

// ---------------------------------------------------------------------
// Dialogs with controls: the template rewritten, each "{CLSID}" item an
// AtlAxWin window with its DLGINIT data as creation data.

#ifndef _ATL_RT_DLGINIT
#define _ATL_RT_DLGINIT 240
#endif

// A growing byte buffer.
struct _AtlAxBuf {
  BYTE* p;
  size_t n;
  size_t cap;
  bool bFailed;
  _AtlAxBuf() : p(NULL), n(0), cap(0), bFailed(false) {}
  ~_AtlAxBuf() { delete[] p; }
  void Put(const void* pv, size_t cb) {
    if (bFailed) return;
    if (n + cb > cap) {
      size_t c = cap ? cap * 2 : 1024;
      while (c < n + cb) c *= 2;
      BYTE* q = new (std::nothrow) BYTE[c];
      if (q == NULL) {
        bFailed = true;
        return;
      }
      if (p != NULL) memcpy(q, p, n);
      delete[] p;
      p = q;
      cap = c;
    }
    memcpy(p + n, pv, cb);
    n += cb;
  }
  void PutWord(WORD w) { Put(&w, sizeof(w)); }
  void PutDword(DWORD dw) { Put(&dw, sizeof(dw)); }
  void Align4() {
    static const BYTE z[3] = {0, 0, 0};
    if (n & 3) Put(z, 4 - (n & 3));
  }
  BYTE* Detach() {
    BYTE* q = p;
    p = NULL;
    n = cap = 0;
    return q;
  }
};

// The words after a name or ordinal (0, 0xFFFF and an ordinal, or a
// string).
inline const WORD* _AtlAxSkipSzOrOrd(const WORD* p) {
  if (*p == 0) return p + 1;
  if (*p == 0xFFFF) return p + 2;
  while (*p != 0) p++;
  return p + 1;
}

// A control's record in the DLGINIT resource: [WORD id][WORD msg][DWORD
// size][data], until an id of 0.
inline const BYTE* _AtlAxFindDlgInit(const BYTE* pInit, WORD nID, WORD* pnMsg, DWORD* pcb) {
  if (pInit == NULL) return NULL;
  for (;;) {
    WORD nIDC, nMsg;
    DWORD cb;
    memcpy(&nIDC, pInit, sizeof(WORD));
    if (nIDC == 0) return NULL;
    memcpy(&nMsg, pInit + 2, sizeof(WORD));
    memcpy(&cb, pInit + 4, sizeof(DWORD));
    pInit += 8;
    if (nIDC == nID && nMsg >= 0x0376 && nMsg <= 0x037B && nMsg != 0x0379) {
      *pnMsg = nMsg;
      *pcb = cb;
      return pInit;
    }
    pInit += cb;
  }
}

// A control item's creation data, from its DLGINIT record: the license
// key, then (for an _EX record) the bindings to skip, then the state.
inline void _AtlAxDlgInitData(const BYTE* pInit, WORD nID, _AtlAxBuf* pData, bool* pbLic) {
  *pbLic = false;
  WORD nMsg = 0;
  DWORD cb = 0;
  const BYTE* p = _AtlAxFindDlgInit(pInit, nID, &nMsg, &cb);
  if (p == NULL || nMsg == 0x0378) return;  // a new control
  ULONG cchKey = 0;
  const BYTE* pKey = NULL;
  if (cb >= sizeof(ULONG)) {
    memcpy(&cchKey, p, sizeof(ULONG));
    p += sizeof(ULONG);
    cb -= sizeof(ULONG);
    if (cchKey * sizeof(WCHAR) > cb) cchKey = 0;
    pKey = p;
    p += cchKey * sizeof(WCHAR);
    cb -= cchKey * sizeof(WCHAR);
  }
  if ((nMsg == 0x037A || nMsg == 0x037B) && cb >= 3 * sizeof(DWORD)) {
    DWORD cbSection;
    memcpy(&cbSection, p, sizeof(DWORD));
    if (cbSection > cb || cbSection < 3 * sizeof(DWORD)) cbSection = cb;
    p += cbSection;
    cb -= cbSection;
  }
  if (cchKey > 0) {
    *pbLic = true;
    pData->Put(&cchKey, sizeof(ULONG));
    pData->Put(pKey, cchKey * sizeof(WCHAR));
  }
  pData->Put(p, cb);
}

// The template rewritten (as a DLGTEMPLATEEX), or NULL when it names no
// control by CLSID; delete[] it. With bNoItems, the dialog alone.
inline DLGTEMPLATE* _AtlAxConvertDialog(const DLGTEMPLATE* pTemplate, const BYTE* pInit,
                                        bool bNoItems = false) {
  if (pTemplate == NULL) return NULL;
  const WORD* pw = (const WORD*)pTemplate;
  bool bEx = pw[1] == 0xFFFF;
  DWORD dwHelpID = 0, dwExStyle, dwStyle;
  WORD cItems;
  short x, y, cx, cy;
  if (bEx) {
    memcpy(&dwHelpID, pw + 2, 4);
    memcpy(&dwExStyle, pw + 4, 4);
    memcpy(&dwStyle, pw + 6, 4);
    cItems = pw[8];
    x = (short)pw[9];
    y = (short)pw[10];
    cx = (short)pw[11];
    cy = (short)pw[12];
    pw += 13;
  } else {
    memcpy(&dwStyle, pw, 4);
    memcpy(&dwExStyle, pw + 2, 4);
    cItems = pw[4];
    x = (short)pw[5];
    y = (short)pw[6];
    cx = (short)pw[7];
    cy = (short)pw[8];
    pw += 9;
  }
  const WORD* pMenu = pw;
  pw = _AtlAxSkipSzOrOrd(pw);
  pw = _AtlAxSkipSzOrOrd(pw);  // the class
  pw = _AtlAxSkipSzOrOrd(pw);  // the title
  const WORD* pTitleEnd = pw;
  WORD wPoint = 0, wWeight = FW_NORMAL;
  BYTE bItalic = 0, bCharset = DEFAULT_CHARSET;
  const WORD* pFace = NULL;
  const WORD* pFaceEnd = NULL;
  if (dwStyle & DS_SETFONT) {
    wPoint = *pw++;
    if (bEx) {
      wWeight = *pw++;
      bItalic = LOBYTE(*pw);
      bCharset = HIBYTE(*pw);
      pw++;
    }
    pFace = pw;
    pw = _AtlAxSkipSzOrOrd(pw);
    pFaceEnd = pw;
  }

  // The items, and whether any names a control.
  bool bAny = bNoItems;
  {
    const BYTE* pb = (const BYTE*)pw;
    for (WORD i = 0; i < cItems && !bAny; i++) {
      pb = (const BYTE*)pTemplate + (((pb - (const BYTE*)pTemplate) + 3) & ~(size_t)3);
      const WORD* q = (const WORD*)pb + (bEx ? 12 : 9);
      if (q[0] == L'{') bAny = true;
      q = _AtlAxSkipSzOrOrd(q);
      q = _AtlAxSkipSzOrOrd(q);
      WORD cbExtra = *q++;
      pb = (const BYTE*)q + cbExtra;
    }
  }
  if (!bAny) return NULL;

  _AtlAxBuf buf;
  buf.PutWord(1);
  buf.PutWord(0xFFFF);
  buf.PutDword(dwHelpID);
  buf.PutDword(dwExStyle);
  buf.PutDword(dwStyle);
  buf.PutWord(bNoItems ? 0 : cItems);
  buf.PutWord((WORD)x);
  buf.PutWord((WORD)y);
  buf.PutWord((WORD)cx);
  buf.PutWord((WORD)cy);
  buf.Put(pMenu, (const BYTE*)pTitleEnd - (const BYTE*)pMenu);
  if (dwStyle & DS_SETFONT) {
    buf.PutWord(wPoint);
    buf.PutWord(wWeight);
    BYTE ab[2] = {bItalic, bCharset};
    buf.Put(ab, 2);
    buf.Put(pFace, (const BYTE*)pFaceEnd - (const BYTE*)pFace);
  }
  const BYTE* pb = (const BYTE*)pw;
  for (WORD i = 0; i < cItems && !bNoItems; i++) {
    pb = (const BYTE*)pTemplate + (((pb - (const BYTE*)pTemplate) + 3) & ~(size_t)3);
    const WORD* q = (const WORD*)pb;
    DWORD dwItemHelp = 0, dwItemEx, dwItemStyle, dwID;
    short ix, iy, icx, icy;
    if (bEx) {
      memcpy(&dwItemHelp, q, 4);
      memcpy(&dwItemEx, q + 2, 4);
      memcpy(&dwItemStyle, q + 4, 4);
      ix = (short)q[6];
      iy = (short)q[7];
      icx = (short)q[8];
      icy = (short)q[9];
      memcpy(&dwID, q + 10, 4);
      q += 12;
    } else {
      memcpy(&dwItemStyle, q, 4);
      memcpy(&dwItemEx, q + 2, 4);
      ix = (short)q[4];
      iy = (short)q[5];
      icx = (short)q[6];
      icy = (short)q[7];
      dwID = q[8];
      q += 9;
    }
    const WORD* pItemClass = q;
    q = _AtlAxSkipSzOrOrd(q);
    const WORD* pItemTitle = q;
    q = _AtlAxSkipSzOrOrd(q);
    const WORD* pItemTitleEnd = q;
    WORD cbExtra = *q++;
    const BYTE* pExtra = (const BYTE*)q;
    pb = pExtra + cbExtra;

    buf.Align4();
    buf.PutDword(dwItemHelp);
    buf.PutDword(dwItemEx);
    buf.PutDword(dwItemStyle);
    buf.PutWord((WORD)ix);
    buf.PutWord((WORD)iy);
    buf.PutWord((WORD)icx);
    buf.PutWord((WORD)icy);
    buf.PutDword(dwID);
    if (pItemClass[0] == L'{') {
      _AtlAxBuf data;
      bool bLic = false;
      _AtlAxDlgInitData(pInit, (WORD)dwID, &data, &bLic);
      if (data.n > 0xFFF0) {  // more than creation data holds
        data.n = 0;
        bLic = false;
      }
      static const WCHAR szWin[] = L"AtlAxWin";
      static const WCHAR szWinLic[] = L"AtlAxWinLic";
      if (bLic)
        buf.Put(szWinLic, sizeof(szWinLic));
      else
        buf.Put(szWin, sizeof(szWin));
      buf.Put(pItemClass, (const BYTE*)pItemTitle - (const BYTE*)pItemClass);  // the CLSID
      buf.PutWord((WORD)data.n);
      if (data.n > 0) buf.Put(data.p, data.n);
    } else {
      buf.Put(pItemClass, (const BYTE*)pItemTitleEnd - (const BYTE*)pItemClass);
      buf.PutWord(cbExtra);
      if (cbExtra > 0) buf.Put(pExtra, cbExtra);
    }
  }
  if (buf.bFailed) return NULL;
  return (DLGTEMPLATE*)buf.Detach();
}

// A dialog resource and its DLGINIT, rewritten.
inline DLGTEMPLATE* _AtlAxLoadDialog(HINSTANCE hInst, LPCWSTR lpTemplateName,
                                     const DLGTEMPLATE** ppOriginal, bool bNoItems = false) {
  *ppOriginal = NULL;
  HRSRC hDlg = ::FindResourceW(hInst, lpTemplateName, (LPCWSTR)RT_DIALOG);
  if (hDlg == NULL) return NULL;
  const DLGTEMPLATE* pTemplate = (const DLGTEMPLATE*)::LockResource(::LoadResource(hInst, hDlg));
  *ppOriginal = pTemplate;
  const BYTE* pInit = NULL;
  HRSRC hInit = ::FindResourceW(hInst, lpTemplateName, MAKEINTRESOURCEW(_ATL_RT_DLGINIT));
  if (hInit != NULL) pInit = (const BYTE*)::LockResource(::LoadResource(hInst, hInit));
  return _AtlAxConvertDialog(pTemplate, pInit, bNoItems);
}
inline LPCWSTR _AtlAxTemplateNameW(LPCSTR lpTemplateName, WCHAR* pszBuf, int cch) {
  if (IS_INTRESOURCE(lpTemplateName)) return (LPCWSTR)lpTemplateName;
  ::MultiByteToWideChar(CP_ACP, 0, lpTemplateName, -1, pszBuf, cch);
  return pszBuf;
}

inline INT_PTR WINAPI AtlAxDialogBoxW(HINSTANCE hInstance, LPCWSTR lpTemplateName,
                                      HWND hWndParent, DLGPROC lpDialogProc, LPARAM dwInitParam) {
  AtlAxWinInit();
  const DLGTEMPLATE* pOriginal = NULL;
  DLGTEMPLATE* pNew = _AtlAxLoadDialog(hInstance, lpTemplateName, &pOriginal);
  if (pOriginal == NULL) return -1;
  INT_PTR n = ::DialogBoxIndirectParamW(hInstance, pNew != NULL ? pNew : pOriginal, hWndParent,
                                        lpDialogProc, dwInitParam);
  delete[] (BYTE*)pNew;
  return n;
}
inline INT_PTR WINAPI AtlAxDialogBoxA(HINSTANCE hInstance, LPCSTR lpTemplateName, HWND hWndParent,
                                      DLGPROC lpDialogProc, LPARAM dwInitParam) {
  AtlAxWinInit();
  WCHAR sz[256];
  const DLGTEMPLATE* pOriginal = NULL;
  DLGTEMPLATE* pNew =
      _AtlAxLoadDialog(hInstance, _AtlAxTemplateNameW(lpTemplateName, sz, 256), &pOriginal);
  if (pOriginal == NULL) return -1;
  INT_PTR n = ::DialogBoxIndirectParamA(hInstance, pNew != NULL ? pNew : pOriginal, hWndParent,
                                        lpDialogProc, dwInitParam);
  delete[] (BYTE*)pNew;
  return n;
}
inline HWND WINAPI AtlAxCreateDialogW(HINSTANCE hInstance, LPCWSTR lpTemplateName,
                                      HWND hWndParent, DLGPROC lpDialogProc, LPARAM dwInitParam) {
  AtlAxWinInit();
  const DLGTEMPLATE* pOriginal = NULL;
  DLGTEMPLATE* pNew = _AtlAxLoadDialog(hInstance, lpTemplateName, &pOriginal);
  if (pOriginal == NULL) return NULL;
  HWND h = ::CreateDialogIndirectParamW(hInstance, pNew != NULL ? pNew : pOriginal, hWndParent,
                                        lpDialogProc, dwInitParam);
  delete[] (BYTE*)pNew;
  return h;
}
inline HWND WINAPI AtlAxCreateDialogA(HINSTANCE hInstance, LPCSTR lpTemplateName, HWND hWndParent,
                                      DLGPROC lpDialogProc, LPARAM dwInitParam) {
  AtlAxWinInit();
  WCHAR sz[256];
  const DLGTEMPLATE* pOriginal = NULL;
  DLGTEMPLATE* pNew =
      _AtlAxLoadDialog(hInstance, _AtlAxTemplateNameW(lpTemplateName, sz, 256), &pOriginal);
  if (pOriginal == NULL) return NULL;
  HWND h = ::CreateDialogIndirectParamA(hInstance, pNew != NULL ? pNew : pOriginal, hWndParent,
                                        lpDialogProc, dwInitParam);
  delete[] (BYTE*)pNew;
  return h;
}
#ifdef UNICODE
#define AtlAxDialogBox AtlAxDialogBoxW
#define AtlAxCreateDialog AtlAxCreateDialogW
#else
#define AtlAxDialogBox AtlAxDialogBoxA
#define AtlAxCreateDialog AtlAxCreateDialogA
#endif

// Advises T's sink map, when it has one.
template <class T>
inline auto _AtlAxAdviseSinkMap(T* pT, bool bAdvise, int) -> decltype(T::_GetSinkMap(), HRESULT()) {
  return AtlAdviseSinkMap(pT, bAdvise);
}
template <class T>
inline HRESULT _AtlAxAdviseSinkMap(T*, bool, long) {
  return S_OK;
}

// A dialog (T::IDD) with controls. Chaining its message map advises the
// class's sink map on the controls when the dialog starts, and
// unadvises it when it goes.
template <class T, class TBase = CWindow>
class ATL_NO_VTABLE CAxDialogImpl : public CDialogImplBaseT<TBase> {
public:
  INT_PTR DoModal(HWND hWndParent = ::GetActiveWindow(), LPARAM dwInitParam = 0) {
    _AtlWinModule.AddCreateWndData(&this->m_thunk.cd, (CDialogImplBaseT<TBase>*)this);
    return AtlAxDialogBox(_AtlBaseModule.GetResourceInstance(),
                          MAKEINTRESOURCE(static_cast<T*>(this)->IDD), hWndParent, T::StartDialogProc,
                          dwInitParam);
  }
  BOOL EndDialog(int nRetCode) { return ::EndDialog(this->m_hWnd, nRetCode); }
  HWND Create(HWND hWndParent, LPARAM dwInitParam = 0) {
    _AtlWinModule.AddCreateWndData(&this->m_thunk.cd, (CDialogImplBaseT<TBase>*)this);
    HWND hWnd = AtlAxCreateDialog(_AtlBaseModule.GetResourceInstance(),
                                  MAKEINTRESOURCE(static_cast<T*>(this)->IDD), hWndParent,
                                  T::StartDialogProc, dwInitParam);
    if (hWnd == NULL) _AtlWinModule.ExtractCreateWndData();
    return hWnd;
  }
  HWND Create(HWND hWndParent, RECT&, LPARAM dwInitParam = 0) { return Create(hWndParent, dwInitParam); }
  BOOL DestroyWindow() { return ::DestroyWindow(this->m_hWnd); }

  HRESULT AdviseSinkMap(bool bAdvise) {
    return _AtlAxAdviseSinkMap(static_cast<T*>(this), bAdvise, 0);
  }

  typedef CAxDialogImpl<T, TBase> thisClass;
  BEGIN_MSG_MAP(thisClass)
    MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialogSinks)
    MESSAGE_HANDLER(WM_DESTROY, OnDestroySinks)
  END_MSG_MAP()

  LRESULT OnInitDialogSinks(UINT, WPARAM, LPARAM, BOOL& bHandled) {
    AdviseSinkMap(true);
    bHandled = FALSE;
    return 1;
  }
  LRESULT OnDestroySinks(UINT, WPARAM, LPARAM, BOOL& bHandled) {
    AdviseSinkMap(false);
    bHandled = FALSE;
    return 0;
  }
};

}  // namespace ATL


#endif  // __ATLHOST_H__
