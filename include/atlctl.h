// atlctl.h - ATL's ActiveX controls: CComControl and the interfaces a
// control gives its container (IOleObject, in-place activation with or
// without a window, IViewObjectEx, IOleControl, IQuickActivate,
// IDataObject), stock properties (CStockPropImpl) and property pages
// (IPropertyPageImpl). The property maps and persistence are in
// atlcom.h.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#ifndef __ATLCTL_H__
#define __ATLCTL_H__

#include "atlwin.h"
#include "atlcom.h"
#include "atlhost.h"
#include <ocidl.h>
#include <olectl.h>

namespace ATL {

// What a control draws with, for OnDraw.
struct ATL_DRAWINFO {
  UINT cbSize;
  DWORD dwDrawAspect;
  LONG lindex;
  DVTARGETDEVICE* ptd;
  HDC hicTargetDev;
  HDC hdcDraw;
  LPCRECTL prcBounds;   // where to draw, in hdcDraw's units
  LPCRECTL prcWBounds;  // the metafile's bounds when drawing to one
  BOOL bOptimize;
  BOOL bZoomed;
  BOOL bRectInHimetric;
  SIZEL ZoomNum;
  SIZEL ZoomDen;
};

// The class's view status and misc status, for its container.
#define DECLARE_VIEW_STATUS(statusFlags) \
  static DWORD _GetViewStatus() { return statusFlags; }
#define DECLARE_OLEMISC_STATUS(statusFlags) \
  static DWORD _GetMiscStatus() { return statusFlags; }

// Pixels and HIMETRIC (0.01 mm) on the screen.
inline void AtlPixelToHiMetric(const SIZEL* lpSizeInPix, LPSIZEL lpSizeInHiMetric) {
  HDC hdc = ::GetDC(NULL);
  int nX = ::GetDeviceCaps(hdc, LOGPIXELSX), nY = ::GetDeviceCaps(hdc, LOGPIXELSY);
  ::ReleaseDC(NULL, hdc);
  lpSizeInHiMetric->cx = ::MulDiv(lpSizeInPix->cx, 2540, nX);
  lpSizeInHiMetric->cy = ::MulDiv(lpSizeInPix->cy, 2540, nY);
}
inline void AtlHiMetricToPixel(const SIZEL* lpSizeInHiMetric, LPSIZEL lpSizeInPix) {
  HDC hdc = ::GetDC(NULL);
  int nX = ::GetDeviceCaps(hdc, LOGPIXELSX), nY = ::GetDeviceCaps(hdc, LOGPIXELSY);
  ::ReleaseDC(NULL, hdc);
  lpSizeInPix->cx = ::MulDiv(lpSizeInHiMetric->cx, nX, 2540);
  lpSizeInPix->cy = ::MulDiv(lpSizeInHiMetric->cy, nY, 2540);
}

// Tells the object's IPropertyNotifySink connections that a property
// changed, or asks them whether it may.
class CFirePropNotifyEvent {
public:
  static HRESULT FireOnRequestEdit(IUnknown* pUnk, DISPID dispid) {
    return _Fire(pUnk, dispid, true);
  }
  static HRESULT FireOnChanged(IUnknown* pUnk, DISPID dispid) { return _Fire(pUnk, dispid, false); }

private:
  static HRESULT _Fire(IUnknown* pUnk, DISPID dispid, bool bRequest) {
    if (pUnk == NULL) return S_OK;
    CComPtr<IConnectionPointContainer> spCPC;
    if (FAILED(pUnk->QueryInterface(IID_IConnectionPointContainer, (void**)&spCPC))) return S_OK;
    CComPtr<IConnectionPoint> spCP;
    if (FAILED(spCPC->FindConnectionPoint(IID_IPropertyNotifySink, &spCP))) return S_OK;
    CComPtr<IEnumConnections> spEnum;
    if (FAILED(spCP->EnumConnections(&spEnum))) return S_OK;
    CONNECTDATA cd;
    HRESULT hr = S_OK;
    while (spEnum->Next(1, &cd, NULL) == S_OK) {
      if (cd.pUnk != NULL) {
        IPropertyNotifySink* pSink = NULL;
        if (SUCCEEDED(cd.pUnk->QueryInterface(IID_IPropertyNotifySink, (void**)&pSink))) {
          if (bRequest) {
            if (pSink->OnRequestEdit(dispid) == S_FALSE) hr = S_FALSE;
          } else {
            pSink->OnChanged(dispid);
          }
          pSink->Release();
        }
        cd.pUnk->Release();
      }
    }
    return hr;
  }
};

// For a class with no IPropertyNotifySink connection point.
class CFakeFirePropNotifyEvent {
public:
  static HRESULT FireOnRequestEdit(IUnknown*, DISPID) { return S_OK; }
  static HRESULT FireOnChanged(IUnknown*, DISPID) { return S_OK; }
};

// The IPropertyNotifySink connection point a control's container
// watches its properties through.
template <class T, class CDV = CComDynamicUnkArray>
class ATL_NO_VTABLE IPropertyNotifySinkCP
    : public IConnectionPointImpl<T, &IID_IPropertyNotifySink, CDV> {
public:
  typedef CFirePropNotifyEvent __ATL_PROP_NOTIFY_EVENT_CLASS;
};

// ---------------------------------------------------------------------
// CComControlBase: a control's state and what it does with its site.

class ATL_NO_VTABLE CComControlBase {
public:
  CComControlBase(HWND& h) : m_hWndCD(h) {
    m_nFreezeEvents = 0;
    m_bNegotiatedWnd = FALSE;
    m_bWndLess = FALSE;
    m_bInPlaceActive = FALSE;
    m_bUIActive = FALSE;
    m_bUsingWindowRgn = FALSE;
    m_bInPlaceSiteEx = FALSE;
    m_bWindowOnly = FALSE;
    m_bRequiresSave = FALSE;
    m_bWasOnceWindowless = FALSE;
    m_bAutoSize = FALSE;
    m_bRecomposeOnResize = FALSE;
    m_bResizeNatural = FALSE;
    m_bDrawFromNatural = FALSE;
    m_bDrawGetDataInHimetric = FALSE;
    m_dwViewAspects = 0;
    m_dwViewAdvf = 0;
    // Two inches square, in HIMETRIC.
    m_sizeExtent.cx = m_sizeExtent.cy = 2 * 2540;
    m_sizeNatural = m_sizeExtent;
    ::SetRectEmpty(&m_rcPos);
  }
  virtual ~CComControlBase() {
    if (m_hWndCD != NULL && ::IsWindow(m_hWndCD)) ::DestroyWindow(m_hWndCD);
  }

  // The control's window: the window class's m_hWnd.
  HWND& m_hWndCD;
  int m_nFreezeEvents;
  BOOL m_bNegotiatedWnd;
  BOOL m_bWndLess;
  BOOL m_bInPlaceActive;
  BOOL m_bUIActive;
  BOOL m_bUsingWindowRgn;
  BOOL m_bInPlaceSiteEx;
  BOOL m_bWindowOnly;  // never windowless
  BOOL m_bRequiresSave;
  BOOL m_bWasOnceWindowless;
  BOOL m_bAutoSize;  // takes no size but its natural one
  BOOL m_bRecomposeOnResize;
  BOOL m_bResizeNatural;
  BOOL m_bDrawFromNatural;
  BOOL m_bDrawGetDataInHimetric;

  SIZE m_sizeExtent;   // HIMETRIC
  SIZE m_sizeNatural;  // HIMETRIC
  RECT m_rcPos;        // in the container's window

  CComPtr<IOleInPlaceSiteWindowless> m_spInPlaceSite;  // the site's IOleInPlaceSite, at least
  CComPtr<IDataAdviseHolder> m_spDataAdviseHolder;
  CComPtr<IOleAdviseHolder> m_spOleAdviseHolder;
  CComPtr<IOleClientSite> m_spClientSite;
  CComPtr<IAdviseSink> m_spAdviseSink;
  CComPtr<IDispatch> m_spAmbientDispatch;
  DWORD m_dwViewAspects;
  DWORD m_dwViewAdvf;

  // What the class gives.
  virtual HRESULT ControlQueryInterface(const IID& iid, void** ppv) = 0;
  virtual HWND CreateControlWindow(HWND hWndParent, RECT& rcPos) = 0;

  // Drawing: OnDrawAdvanced gets the DC as the container gives it, and
  // calls OnDraw.
  virtual HRESULT OnDrawAdvanced(ATL_DRAWINFO& di) {
    int nSaved = ::SaveDC(di.hdcDraw);
    HRESULT hr = OnDraw(di);
    ::RestoreDC(di.hdcDraw, nSaved);
    return hr;
  }
  virtual HRESULT OnDraw(ATL_DRAWINFO& di) {
    RECT rc = {di.prcBounds->left, di.prcBounds->top, di.prcBounds->right, di.prcBounds->bottom};
    ::Rectangle(di.hdcDraw, rc.left, rc.top, rc.right, rc.bottom);
    ::SetTextAlign(di.hdcDraw, TA_CENTER | TA_BASELINE);
    static const TCHAR szText[] = _T("ATL control");
    ::TextOut(di.hdcDraw, (rc.left + rc.right) / 2, (rc.top + rc.bottom) / 2, szText,
              (int)(sizeof(szText) / sizeof(TCHAR) - 1));
    return S_OK;
  }

  static DWORD _GetViewStatus() { return 0; }
  static DWORD _GetMiscStatus() { return 0; }

  // The stock properties' change hooks; the class hides those it wants.
  void OnAppearanceChanged() {}
  void OnAutoSizeChanged() {}
  void OnBackColorChanged() {}
  void OnBackStyleChanged() {}
  void OnBorderColorChanged() {}
  void OnBorderStyleChanged() {}
  void OnBorderVisibleChanged() {}
  void OnBorderWidthChanged() {}
  void OnCaptionChanged() {}
  void OnDrawModeChanged() {}
  void OnDrawStyleChanged() {}
  void OnDrawWidthChanged() {}
  void OnEnabledChanged() {}
  void OnFillColorChanged() {}
  void OnFillStyleChanged() {}
  void OnFontChanged() {}
  void OnForeColorChanged() {}
  void OnMouseIconChanged() {}
  void OnMousePointerChanged() {}
  void OnPictureChanged() {}
  void OnReadyStateChanged() {}
  void OnTabStopChanged() {}
  void OnTextChanged() {}
  void OnValidChanged() {}

  // The verbs' hooks.
  HRESULT OnPreVerbInPlaceActivate() { return S_OK; }
  HRESULT OnPostVerbInPlaceActivate() { return S_OK; }
  HRESULT OnPreVerbUIActivate() { return S_OK; }
  HRESULT OnPostVerbUIActivate() { return S_OK; }
  HRESULT OnPreVerbShow() { return S_OK; }
  HRESULT OnPostVerbShow() { return S_OK; }
  HRESULT OnPreVerbHide() { return S_OK; }
  HRESULT OnPostVerbHide() { return S_OK; }
  HRESULT OnPreVerbOpen() { return S_OK; }
  HRESULT OnPostVerbOpen() { return S_OK; }
  HRESULT OnPreVerbDiscardUndo() { return S_OK; }
  HRESULT OnPostVerbDiscardUndo() { return S_OK; }
  BOOL PreTranslateAccelerator(LPMSG, HRESULT&) { return FALSE; }

  // ---- the container's ambient properties
  HRESULT GetAmbientProperty(DISPID dispid, VARIANT& var) {
    VariantClear(&var);
    if (m_spAmbientDispatch == NULL) return E_FAIL;
    return _AtlGetProperty(m_spAmbientDispatch, dispid, &var);
  }
#define _ATL_AMBIENT_BOOL(name, dispid)                         \
  HRESULT GetAmbient##name(BOOL& b) {                           \
    CComVariant var;                                            \
    HRESULT hr = GetAmbientProperty(dispid, var);               \
    if (SUCCEEDED(hr)) hr = var.ChangeType(VT_BOOL);            \
    if (SUCCEEDED(hr)) b = var.boolVal != VARIANT_FALSE;        \
    return hr;                                                  \
  }
#define _ATL_AMBIENT_VALUE(name, dispid, type, vtype, field)    \
  HRESULT GetAmbient##name(type& v) {                           \
    CComVariant var;                                            \
    HRESULT hr = GetAmbientProperty(dispid, var);               \
    if (SUCCEEDED(hr)) hr = var.ChangeType(vtype);              \
    if (SUCCEEDED(hr)) v = (type)(INT_PTR)var.field;            \
    return hr;                                                  \
  }
#define _ATL_AMBIENT_BSTR(name, dispid)                         \
  HRESULT GetAmbient##name(BSTR& bstr) {                        \
    CComVariant var;                                            \
    HRESULT hr = GetAmbientProperty(dispid, var);               \
    if (SUCCEEDED(hr)) hr = var.ChangeType(VT_BSTR);            \
    if (SUCCEEDED(hr)) {                                        \
      bstr = var.bstrVal;                                       \
      var.vt = VT_EMPTY;                                        \
    }                                                           \
    return hr;                                                  \
  }
  _ATL_AMBIENT_VALUE(Appearance, DISPID_AMBIENT_APPEARANCE, short, VT_I2, iVal)
  _ATL_AMBIENT_VALUE(BackColor, DISPID_AMBIENT_BACKCOLOR, OLE_COLOR, VT_I4, lVal)
  _ATL_AMBIENT_VALUE(ForeColor, DISPID_AMBIENT_FORECOLOR, OLE_COLOR, VT_I4, lVal)
  _ATL_AMBIENT_VALUE(LocaleID, DISPID_AMBIENT_LOCALEID, LCID, VT_I4, lVal)
  _ATL_AMBIENT_VALUE(TextAlign, DISPID_AMBIENT_TEXTALIGN, short, VT_I2, iVal)
  _ATL_AMBIENT_VALUE(CodePage, DISPID_AMBIENT_CODEPAGE, ULONG, VT_UI4, ulVal)
  _ATL_AMBIENT_VALUE(Palette, DISPID_AMBIENT_PALETTE, HPALETTE, VT_I4, lVal)
  _ATL_AMBIENT_BSTR(DisplayName, DISPID_AMBIENT_DISPLAYNAME)
  _ATL_AMBIENT_BSTR(ScaleUnits, DISPID_AMBIENT_SCALEUNITS)
  _ATL_AMBIENT_BSTR(CharSet, DISPID_AMBIENT_CHARSET)
  _ATL_AMBIENT_BOOL(UserMode, DISPID_AMBIENT_USERMODE)
  _ATL_AMBIENT_BOOL(UIDead, DISPID_AMBIENT_UIDEAD)
  _ATL_AMBIENT_BOOL(ShowGrabHandles, DISPID_AMBIENT_SHOWGRABHANDLES)
  _ATL_AMBIENT_BOOL(ShowHatching, DISPID_AMBIENT_SHOWHATCHING)
  _ATL_AMBIENT_BOOL(MessageReflect, DISPID_AMBIENT_MESSAGEREFLECT)
  _ATL_AMBIENT_BOOL(AutoClip, DISPID_AMBIENT_AUTOCLIP)
  _ATL_AMBIENT_BOOL(DisplayAsDefault, DISPID_AMBIENT_DISPLAYASDEFAULT)
  _ATL_AMBIENT_BOOL(SupportsMnemonics, DISPID_AMBIENT_SUPPORTSMNEMONICS)
  _ATL_AMBIENT_BOOL(RightToLeft, DISPID_AMBIENT_RIGHTTOLEFT)
  _ATL_AMBIENT_BOOL(TopToBottom, DISPID_AMBIENT_TOPTOBOTTOM)
#undef _ATL_AMBIENT_BOOL
#undef _ATL_AMBIENT_VALUE
#undef _ATL_AMBIENT_BSTR
  HRESULT GetAmbientFontDisp(IFontDisp** ppFont) {
    if (ppFont == NULL) return E_POINTER;
    *ppFont = NULL;
    CComVariant var;
    HRESULT hr = GetAmbientProperty(DISPID_AMBIENT_FONT, var);
    if (FAILED(hr)) return hr;
    if ((var.vt != VT_DISPATCH && var.vt != VT_UNKNOWN) || var.punkVal == NULL) return E_FAIL;
    return var.punkVal->QueryInterface(IID_IFontDisp, (void**)ppFont);
  }
  HRESULT GetAmbientFont(IFont** ppFont) {
    if (ppFont == NULL) return E_POINTER;
    *ppFont = NULL;
    CComPtr<IFontDisp> sp;
    HRESULT hr = GetAmbientFontDisp(&sp);
    return SUCCEEDED(hr) ? sp->QueryInterface(IID_IFont, (void**)ppFont) : hr;
  }

  // ---- dirty, and the advise holders
  void SetDirty(BOOL bDirty) { m_bRequiresSave = bDirty; }
  BOOL GetDirty() { return m_bRequiresSave; }
  HRESULT SendOnDataChange(DWORD advf = 0) {
    if (m_spDataAdviseHolder == NULL) return S_OK;
    CComPtr<IDataObject> spData;
    if (FAILED(ControlQueryInterface(IID_IDataObject, (void**)&spData))) return S_OK;
    return m_spDataAdviseHolder->SendOnDataChange(spData, 0, advf);
  }
  HRESULT SendOnViewChange(DWORD dwAspect, LONG lindex = -1) {
    if (m_spAdviseSink != NULL && (m_dwViewAspects == 0 || (m_dwViewAspects & dwAspect)))
      m_spAdviseSink->OnViewChange(dwAspect, lindex);
    return S_OK;
  }
  HRESULT SendOnClose() {
    if (m_spOleAdviseHolder != NULL) m_spOleAdviseHolder->SendOnClose();
    return S_OK;
  }
  HRESULT SendOnRename(IMoniker* pmk) {
    if (m_spOleAdviseHolder != NULL) m_spOleAdviseHolder->SendOnRename(pmk);
    return S_OK;
  }
  HRESULT SendOnSave() {
    if (m_spOleAdviseHolder != NULL) m_spOleAdviseHolder->SendOnSave();
    return S_OK;
  }

  // Redraws: in place, the window or the windowless site's area; else
  // the view's advise sink.
  HRESULT FireViewChange() {
    if (m_bInPlaceActive) {
      if (!m_bWndLess && m_hWndCD != NULL) return ::InvalidateRect(m_hWndCD, NULL, TRUE) ? S_OK : E_FAIL;
      if (m_bWndLess && m_spInPlaceSite != NULL) return m_spInPlaceSite->InvalidateRect(NULL, TRUE);
    }
    return SendOnViewChange(DVASPECT_CONTENT);
  }
  HRESULT SetControlFocus(BOOL bGrab) {
    if (m_bWndLess) {
      if (m_spInPlaceSite == NULL) return E_FAIL;
      return m_spInPlaceSite->SetFocus(bGrab);
    }
    if (m_hWndCD == NULL) return E_FAIL;
    if (bGrab) ::SetFocus(m_hWndCD);
    return S_OK;
  }

  // ---- in-place activation
  HRESULT InPlaceActivate(LONG iVerb, const RECT* prcPosRect = NULL) {
    (void)prcPosRect;
    if (m_spClientSite == NULL) return E_UNEXPECTED;
    HRESULT hr;
    if (!m_bInPlaceActive) {
      // The site, the richest kind it is.
      CComPtr<IOleInPlaceSite> spSite;
      m_bWndLess = FALSE;
      m_bInPlaceSiteEx = FALSE;
      if (!m_bWindowOnly &&
          SUCCEEDED(m_spClientSite->QueryInterface(IID_IOleInPlaceSiteWindowless,
                                                   (void**)&m_spInPlaceSite))) {
        m_bInPlaceSiteEx = TRUE;
        m_bWndLess = m_spInPlaceSite->CanWindowlessActivate() == S_OK;
      } else {
        m_spInPlaceSite.Release();
        CComPtr<IOleInPlaceSiteEx> spEx;
        if (SUCCEEDED(m_spClientSite->QueryInterface(IID_IOleInPlaceSiteEx, (void**)&spEx))) {
          m_bInPlaceSiteEx = TRUE;
          m_spInPlaceSite.p = (IOleInPlaceSiteWindowless*)spEx.Detach();
        } else if (SUCCEEDED(m_spClientSite->QueryInterface(IID_IOleInPlaceSite, (void**)&spSite))) {
          m_spInPlaceSite.p = (IOleInPlaceSiteWindowless*)spSite.Detach();
        } else {
          return E_FAIL;
        }
      }
      if (m_spInPlaceSite->CanInPlaceActivate() != S_OK) {
        m_spInPlaceSite.Release();
        return E_FAIL;
      }
      m_bWasOnceWindowless = m_bWasOnceWindowless || m_bWndLess;
      BOOL bNoRedraw = FALSE;
      if (m_bInPlaceSiteEx)
        hr = ((IOleInPlaceSiteEx*)m_spInPlaceSite.p)
                 ->OnInPlaceActivateEx(&bNoRedraw, m_bWndLess ? ACTIVATE_WINDOWLESS : 0);
      else
        hr = m_spInPlaceSite->OnInPlaceActivate();
      if (FAILED(hr)) {
        m_spInPlaceSite.Release();
        return hr;
      }
      m_bInPlaceActive = TRUE;

      // Where, and in what window.
      CComPtr<IOleInPlaceFrame> spFrame;
      CComPtr<IOleInPlaceUIWindow> spDoc;
      RECT rcPos, rcClip;
      OLEINPLACEFRAMEINFO fi;
      fi.cb = sizeof(fi);
      HWND hwndParent = NULL;
      if (m_spInPlaceSite->GetWindow(&hwndParent) == S_OK &&
          SUCCEEDED(m_spInPlaceSite->GetWindowContext(&spFrame, &spDoc, &rcPos, &rcClip, &fi))) {
        if (!m_bWndLess) {
          if (m_hWndCD != NULL) {
            if (::GetParent(m_hWndCD) != hwndParent) ::SetParent(m_hWndCD, hwndParent);
          } else {
            CreateControlWindow(hwndParent, rcPos);
          }
        }
        IOleInPlaceObject* pIPO = NULL;
        if (SUCCEEDED(ControlQueryInterface(IID_IOleInPlaceObject, (void**)&pIPO))) {
          pIPO->SetObjectRects(&rcPos, &rcClip);
          pIPO->Release();
        } else {
          m_rcPos = rcPos;
        }
      }
      if (!m_bWndLess && m_hWndCD != NULL) ::ShowWindow(m_hWndCD, SW_SHOW);
    }

    // UI activation: the frame and document hear of its active object.
    if (iVerb == OLEIVERB_UIACTIVATE && !m_bUIActive) {
      if (m_bInPlaceSiteEx &&
          ((IOleInPlaceSiteEx*)m_spInPlaceSite.p)->RequestUIActivate() != S_OK)
        return S_OK;
      m_bUIActive = TRUE;
      hr = m_spInPlaceSite->OnUIActivate();
      if (FAILED(hr)) {
        m_bUIActive = FALSE;
        return hr;
      }
      SetControlFocus(TRUE);
      CComPtr<IOleInPlaceFrame> spFrame;
      CComPtr<IOleInPlaceUIWindow> spDoc;
      RECT rcPos, rcClip;
      OLEINPLACEFRAMEINFO fi;
      fi.cb = sizeof(fi);
      if (SUCCEEDED(m_spInPlaceSite->GetWindowContext(&spFrame, &spDoc, &rcPos, &rcClip, &fi))) {
        CComPtr<IOleInPlaceActiveObject> spAO;
        ControlQueryInterface(IID_IOleInPlaceActiveObject, (void**)&spAO);
        if (spDoc != NULL) {
          spDoc->SetActiveObject(spAO, NULL);
          spDoc->SetBorderSpace(NULL);
        }
        if (spFrame != NULL) {
          spFrame->SetActiveObject(spAO, NULL);
          spFrame->SetBorderSpace(NULL);
        }
      }
      if (m_spClientSite != NULL) m_spClientSite->ShowObject();
    }
    return S_OK;
  }

  // ---- the hooks the interface implementations call
  HRESULT IOleObject_SetClientSite(IOleClientSite* pClientSite) {
    m_spClientSite = pClientSite;
    m_spAmbientDispatch.Release();
    if (pClientSite != NULL) pClientSite->QueryInterface(IID_IDispatch, (void**)&m_spAmbientDispatch);
    return S_OK;
  }
  HRESULT IOleObject_Advise(IAdviseSink* pAdvSink, DWORD* pdwConnection) {
    HRESULT hr = S_OK;
    if (m_spOleAdviseHolder == NULL) hr = ::CreateOleAdviseHolder(&m_spOleAdviseHolder);
    if (SUCCEEDED(hr)) hr = m_spOleAdviseHolder->Advise(pAdvSink, pdwConnection);
    return hr;
  }
  HRESULT IOleObject_Close(DWORD dwSaveOption) {
    if (m_bInPlaceActive) IOleInPlaceObject_InPlaceDeactivate();
    if ((dwSaveOption == OLECLOSE_SAVEIFDIRTY || dwSaveOption == OLECLOSE_PROMPTSAVE) &&
        m_bRequiresSave) {
      if (m_spClientSite != NULL) m_spClientSite->SaveObject();
      SendOnSave();
    }
    m_spInPlaceSite.Release();
    m_bNegotiatedWnd = FALSE;
    m_bWndLess = FALSE;
    m_bInPlaceSiteEx = FALSE;
    m_spAdviseSink.Release();
    return S_OK;
  }
  HRESULT IOleObject_SetExtent(DWORD dwDrawAspect, SIZEL* psizel) {
    if (dwDrawAspect != DVASPECT_CONTENT) return DV_E_DVASPECT;
    if (psizel == NULL) return E_POINTER;
    BOOL bNatural = psizel->cx == m_sizeNatural.cx && psizel->cy == m_sizeNatural.cy;
    if (m_bAutoSize) return bNatural ? S_OK : E_FAIL;
    BOOL bChanged = psizel->cx != m_sizeExtent.cx || psizel->cy != m_sizeExtent.cy;
    m_sizeExtent = *psizel;
    if (m_bResizeNatural) m_sizeNatural = *psizel;
    if (bChanged) {
      SetDirty(TRUE);
      if (m_bRecomposeOnResize) {
        SendOnDataChange();
        FireViewChange();
      }
    }
    return S_OK;
  }
  HRESULT IOleInPlaceObject_InPlaceDeactivate() {
    if (!m_bInPlaceActive) return S_OK;
    if (m_bUIActive) IOleInPlaceObject_UIDeactivate();
    m_bInPlaceActive = FALSE;
    if (!m_bWndLess && m_hWndCD != NULL) {
      if (::IsWindow(m_hWndCD)) ::DestroyWindow(m_hWndCD);
      m_hWndCD = NULL;
    }
    if (m_spInPlaceSite != NULL) {
      if (m_bInPlaceSiteEx)
        ((IOleInPlaceSiteEx*)m_spInPlaceSite.p)->OnInPlaceDeactivateEx(TRUE);
      else
        m_spInPlaceSite->OnInPlaceDeactivate();
    }
    return S_OK;
  }
  HRESULT IOleInPlaceObject_UIDeactivate() {
    if (!m_bUIActive) return S_OK;
    m_bUIActive = FALSE;
    if (m_spInPlaceSite != NULL) {
      CComPtr<IOleInPlaceFrame> spFrame;
      CComPtr<IOleInPlaceUIWindow> spDoc;
      RECT rcPos, rcClip;
      OLEINPLACEFRAMEINFO fi;
      fi.cb = sizeof(fi);
      if (SUCCEEDED(m_spInPlaceSite->GetWindowContext(&spFrame, &spDoc, &rcPos, &rcClip, &fi))) {
        if (spDoc != NULL) spDoc->SetActiveObject(NULL, NULL);
        if (spFrame != NULL) spFrame->SetActiveObject(NULL, NULL);
      }
      m_spInPlaceSite->OnUIDeactivate(FALSE);
    }
    return S_OK;
  }
  HRESULT IOleInPlaceObject_SetObjectRects(LPCRECT prcPos, LPCRECT prcClip) {
    if (prcPos == NULL || prcClip == NULL) return E_POINTER;
    m_rcPos = *prcPos;
    if (!m_bWndLess && m_hWndCD != NULL) {
      // Clipped by a window region when the clip cuts it.
      RECT rcIXect;
      BOOL bIntersect = ::IntersectRect(&rcIXect, prcPos, prcClip);
      if (bIntersect && !::EqualRect(&rcIXect, prcPos)) {
        ::OffsetRect(&rcIXect, -prcPos->left, -prcPos->top);
        ::SetWindowRgn(m_hWndCD, ::CreateRectRgnIndirect(&rcIXect), TRUE);
        m_bUsingWindowRgn = TRUE;
      } else if (m_bUsingWindowRgn) {
        ::SetWindowRgn(m_hWndCD, NULL, TRUE);
        m_bUsingWindowRgn = FALSE;
      }
      ::SetWindowPos(m_hWndCD, NULL, prcPos->left, prcPos->top, prcPos->right - prcPos->left,
                     prcPos->bottom - prcPos->top, SWP_NOZORDER | SWP_NOACTIVATE);
    }
    return S_OK;
  }
  HRESULT IOleInPlaceObject_GetWindow(HWND* phwnd) {
    if (phwnd == NULL) return E_POINTER;
    *phwnd = m_bWndLess ? NULL : m_hWndCD;
    return *phwnd != NULL ? S_OK : E_FAIL;
  }

  HRESULT IViewObject_Draw(DWORD dwDrawAspect, LONG lindex, void*, DVTARGETDEVICE* ptd,
                           HDC hicTargetDev, HDC hdcDraw, LPCRECTL prcBounds, LPCRECTL prcWBounds) {
    if (hdcDraw == NULL) return E_INVALIDARG;
    RECTL rcPos = {m_rcPos.left, m_rcPos.top, m_rcPos.right, m_rcPos.bottom};
    if (prcBounds == NULL) {
      if (!m_bWndLess) return E_INVALIDARG;
      prcBounds = &rcPos;
    }
    ATL_DRAWINFO di;
    memset(&di, 0, sizeof(di));
    di.cbSize = sizeof(di);
    di.dwDrawAspect = dwDrawAspect;
    di.lindex = lindex;
    di.ptd = ptd;
    di.hicTargetDev = hicTargetDev;
    di.hdcDraw = hdcDraw;
    di.prcBounds = prcBounds;
    di.prcWBounds = prcWBounds;
    di.ZoomNum.cx = di.ZoomDen.cx = prcBounds->right - prcBounds->left;
    di.ZoomNum.cy = di.ZoomDen.cy = prcBounds->bottom - prcBounds->top;
    return OnDrawAdvanced(di);
  }

  // A metafile picture of the control, at its extent.
  HRESULT IDataObject_GetData(FORMATETC* pformatetcIn, STGMEDIUM* pmedium) {
    if (pformatetcIn == NULL || pmedium == NULL) return E_POINTER;
    memset(pmedium, 0, sizeof(*pmedium));
    if (pformatetcIn->cfFormat != CF_METAFILEPICT || !(pformatetcIn->tymed & TYMED_MFPICT))
      return DV_E_FORMATETC;
    SIZEL sizePix;
    AtlHiMetricToPixel(&m_sizeExtent, &sizePix);
    HDC hdc = ::CreateMetaFile(NULL);
    if (hdc == NULL) return E_OUTOFMEMORY;
    ::SetWindowOrgEx(hdc, 0, 0, NULL);
    ::SetWindowExtEx(hdc, sizePix.cx, sizePix.cy, NULL);
    RECTL rcBounds = {0, 0, sizePix.cx, sizePix.cy};
    IViewObject_Draw(DVASPECT_CONTENT, -1, NULL, NULL, NULL, hdc, &rcBounds, &rcBounds);
    HMETAFILE hMF = ::CloseMetaFile(hdc);
    if (hMF == NULL) return E_UNEXPECTED;
    HGLOBAL hMem = ::GlobalAlloc(GMEM_SHARE | GMEM_MOVEABLE, sizeof(METAFILEPICT));
    if (hMem == NULL) {
      ::DeleteMetaFile(hMF);
      return E_OUTOFMEMORY;
    }
    LPMETAFILEPICT pMFP = (LPMETAFILEPICT)::GlobalLock(hMem);
    pMFP->mm = MM_ANISOTROPIC;
    pMFP->xExt = m_sizeExtent.cx;
    pMFP->yExt = m_sizeExtent.cy;
    pMFP->hMF = hMF;
    ::GlobalUnlock(hMem);
    pmedium->tymed = TYMED_MFPICT;
    pmedium->hGlobal = hMem;
    pmedium->pUnkForRelease = NULL;
    return S_OK;
  }

  // ---- the verbs
  HRESULT DoVerbProperties(LPCRECT /*prcPosRect*/, HWND hwndParent) {
    // The container's own frame first, then one of ours.
    if (m_spClientSite != NULL) {
      CComPtr<IOleControlSite> spCS;
      if (SUCCEEDED(m_spClientSite->QueryInterface(IID_IOleControlSite, (void**)&spCS)) &&
          spCS->ShowPropertyFrame() == S_OK)
        return S_OK;
    }
    CComPtr<ISpecifyPropertyPages> spPages;
    HRESULT hr = ControlQueryInterface(IID_ISpecifyPropertyPages, (void**)&spPages);
    if (FAILED(hr)) return hr;
    CAUUID pages = {0, NULL};
    hr = spPages->GetPages(&pages);
    if (FAILED(hr)) return hr;
    CComPtr<IUnknown> spUnk;
    ControlQueryInterface(IID_IUnknown, (void**)&spUnk);
    IUnknown* pUnk = spUnk;
    LCID lcid = LOCALE_USER_DEFAULT;
    GetAmbientLocaleID(lcid);
    hr = ::OleCreatePropertyFrame(hwndParent, m_rcPos.top, m_rcPos.left, NULL, 1, &pUnk,
                                  pages.cElems, pages.pElems, lcid, 0, NULL);
    ::CoTaskMemFree(pages.pElems);
    return hr;
  }
  HRESULT DoVerbInPlaceActivate(LPCRECT prcPosRect, HWND) {
    HRESULT hr = OnPreVerbInPlaceActivate();
    if (SUCCEEDED(hr)) hr = InPlaceActivate(OLEIVERB_INPLACEACTIVATE, prcPosRect);
    if (SUCCEEDED(hr)) hr = OnPostVerbInPlaceActivate();
    if (SUCCEEDED(hr)) FireViewChange();
    return hr;
  }
  HRESULT DoVerbUIActivate(LPCRECT prcPosRect, HWND) {
    HRESULT hr = OnPreVerbUIActivate();
    if (SUCCEEDED(hr)) hr = InPlaceActivate(OLEIVERB_UIACTIVATE, prcPosRect);
    if (SUCCEEDED(hr)) hr = OnPostVerbUIActivate();
    return hr;
  }
  HRESULT DoVerbPrimary(LPCRECT prcPosRect, HWND hwndParent) {
    return DoVerbProperties(prcPosRect, hwndParent);
  }
  HRESULT DoVerbShow(LPCRECT prcPosRect, HWND) {
    HRESULT hr = OnPreVerbShow();
    if (SUCCEEDED(hr)) hr = InPlaceActivate(OLEIVERB_SHOW, prcPosRect);
    if (SUCCEEDED(hr)) hr = OnPostVerbShow();
    return hr;
  }
  HRESULT DoVerbHide(LPCRECT, HWND) {
    HRESULT hr = OnPreVerbHide();
    if (FAILED(hr)) return hr;
    IOleInPlaceObject_UIDeactivate();
    if (m_hWndCD != NULL) ::ShowWindow(m_hWndCD, SW_HIDE);
    return OnPostVerbHide();
  }
  HRESULT DoVerbOpen(LPCRECT, HWND) {
    HRESULT hr = OnPreVerbOpen();
    return SUCCEEDED(hr) ? OnPostVerbOpen() : hr;
  }
  HRESULT DoVerbDiscardUndo(LPCRECT, HWND) {
    HRESULT hr = OnPreVerbDiscardUndo();
    return SUCCEEDED(hr) ? OnPostVerbDiscardUndo() : hr;
  }

  // ---- the window's messages (CComControl's message map)
  LRESULT OnPaint(UINT, WPARAM wParam, LPARAM, BOOL&) {
    PAINTSTRUCT ps;
    HDC hdc = wParam != 0 ? (HDC)wParam : ::BeginPaint(m_hWndCD, &ps);
    if (hdc == NULL) return 0;
    RECT rc;
    ::GetClientRect(m_hWndCD, &rc);
    RECTL rcl = {rc.left, rc.top, rc.right, rc.bottom};
    ATL_DRAWINFO di;
    memset(&di, 0, sizeof(di));
    di.cbSize = sizeof(di);
    di.dwDrawAspect = DVASPECT_CONTENT;
    di.lindex = -1;
    di.hdcDraw = hdc;
    di.prcBounds = &rcl;
    di.ZoomNum.cx = di.ZoomDen.cx = rc.right - rc.left;
    di.ZoomNum.cy = di.ZoomDen.cy = rc.bottom - rc.top;
    OnDrawAdvanced(di);
    if (wParam == 0) ::EndPaint(m_hWndCD, &ps);
    return 0;
  }
  LRESULT OnSetFocus(UINT, WPARAM, LPARAM, BOOL& bHandled) {
    if (m_bInPlaceActive && !m_bUIActive) InPlaceActivate(OLEIVERB_UIACTIVATE);
    _ControlSiteOnFocus(TRUE);
    bHandled = FALSE;
    return 1;
  }
  LRESULT OnKillFocus(UINT, WPARAM, LPARAM, BOOL& bHandled) {
    _ControlSiteOnFocus(FALSE);
    bHandled = FALSE;
    return 1;
  }
  LRESULT OnMouseActivate(UINT, WPARAM, LPARAM, BOOL& bHandled) {
    if (m_bInPlaceActive && !m_bUIActive) InPlaceActivate(OLEIVERB_UIACTIVATE);
    bHandled = FALSE;
    return 1;
  }

private:
  void _ControlSiteOnFocus(BOOL bGot) {
    CComPtr<IOleControlSite> spCS;
    if (m_spClientSite != NULL &&
        SUCCEEDED(m_spClientSite->QueryInterface(IID_IOleControlSite, (void**)&spCS)))
      spCS->OnFocus(bGot);
  }
};

// ---------------------------------------------------------------------
// CComControl: the base of a control class T, with its window.

template <class T, class WinBase = CWindowImpl<T> >
class ATL_NO_VTABLE CComControl : public CComControlBase, public WinBase {
public:
  CComControl() : CComControlBase(this->m_hWnd) {}

  HRESULT FireOnRequestEdit(DISPID dispid) {
    T* pT = static_cast<T*>(this);
    if (m_nFreezeEvents > 0) return S_OK;
    return CFirePropNotifyEvent::FireOnRequestEdit(pT->GetUnknown(), dispid);
  }
  HRESULT FireOnChanged(DISPID dispid) {
    T* pT = static_cast<T*>(this);
    if (m_nFreezeEvents > 0) return S_OK;
    return CFirePropNotifyEvent::FireOnChanged(pT->GetUnknown(), dispid);
  }
  virtual HRESULT ControlQueryInterface(const IID& iid, void** ppv) override {
    return static_cast<T*>(this)->GetUnknown()->QueryInterface(iid, ppv);
  }
  virtual HWND CreateControlWindow(HWND hWndParent, RECT& rcPos) override {
    T* pT = static_cast<T*>(this);
    return pT->Create(hWndParent, rcPos);
  }
  int MessageBox(LPCTSTR lpszText, LPCTSTR lpszCaption = _T(""), UINT nType = MB_OK) {
    HWND hwndParent = NULL;
    if (m_spInPlaceSite != NULL) m_spInPlaceSite->GetWindow(&hwndParent);
    if (hwndParent == NULL) hwndParent = this->m_hWnd;
    return ::MessageBox(hwndParent, lpszText, lpszCaption, nType);
  }

  typedef CComControl<T, WinBase> thisClass;
  BEGIN_MSG_MAP(thisClass)
    MESSAGE_HANDLER(WM_PAINT, CComControlBase::OnPaint)
    MESSAGE_HANDLER(WM_SETFOCUS, CComControlBase::OnSetFocus)
    MESSAGE_HANDLER(WM_KILLFOCUS, CComControlBase::OnKillFocus)
    MESSAGE_HANDLER(WM_MOUSEACTIVATE, CComControlBase::OnMouseActivate)
  END_MSG_MAP()
};

// ---------------------------------------------------------------------
// The interfaces a control gives its container.

template <class T>
class ATL_NO_VTABLE IOleObjectImpl : public IOleObject {
public:
  STDMETHOD(SetClientSite)(IOleClientSite* pClientSite) override {
    return static_cast<T*>(this)->IOleObject_SetClientSite(pClientSite);
  }
  STDMETHOD(GetClientSite)(IOleClientSite** ppClientSite) override {
    if (ppClientSite == NULL) return E_POINTER;
    *ppClientSite = static_cast<T*>(this)->m_spClientSite;
    if (*ppClientSite != NULL) (*ppClientSite)->AddRef();
    return S_OK;
  }
  STDMETHOD(SetHostNames)(LPCOLESTR, LPCOLESTR) override { return S_OK; }
  STDMETHOD(Close)(DWORD dwSaveOption) override {
    return static_cast<T*>(this)->IOleObject_Close(dwSaveOption);
  }
  STDMETHOD(SetMoniker)(DWORD, IMoniker*) override { return E_NOTIMPL; }
  STDMETHOD(GetMoniker)(DWORD dwAssign, DWORD dwWhichMoniker, IMoniker** ppmk) override {
    if (ppmk == NULL) return E_POINTER;
    *ppmk = NULL;
    T* pT = static_cast<T*>(this);
    if (pT->m_spClientSite == NULL) return E_UNEXPECTED;
    return pT->m_spClientSite->GetMoniker(dwAssign, dwWhichMoniker, ppmk);
  }
  STDMETHOD(InitFromData)(IDataObject*, BOOL, DWORD) override { return E_NOTIMPL; }
  STDMETHOD(GetClipboardData)(DWORD, IDataObject**) override { return E_NOTIMPL; }
  STDMETHOD(DoVerb)(LONG iVerb, LPMSG, IOleClientSite*, LONG, HWND hwndParent,
                    LPCRECT lprcPosRect) override {
    T* pT = static_cast<T*>(this);
    switch (iVerb) {
    case OLEIVERB_PRIMARY: return pT->DoVerbPrimary(lprcPosRect, hwndParent);
    case OLEIVERB_SHOW: return pT->DoVerbShow(lprcPosRect, hwndParent);
    case OLEIVERB_INPLACEACTIVATE: return pT->DoVerbInPlaceActivate(lprcPosRect, hwndParent);
    case OLEIVERB_UIACTIVATE: return pT->DoVerbUIActivate(lprcPosRect, hwndParent);
    case OLEIVERB_HIDE: return pT->DoVerbHide(lprcPosRect, hwndParent);
    case OLEIVERB_OPEN: return pT->DoVerbOpen(lprcPosRect, hwndParent);
    case OLEIVERB_DISCARDUNDOSTATE: return pT->DoVerbDiscardUndo(lprcPosRect, hwndParent);
    case OLEIVERB_PROPERTIES: return pT->DoVerbProperties(lprcPosRect, hwndParent);
    default:
      if (iVerb > 0) {
        // A verb it does not know: the primary one.
        HRESULT hr = pT->DoVerbPrimary(lprcPosRect, hwndParent);
        return SUCCEEDED(hr) ? OLEOBJ_S_INVALIDVERB : hr;
      }
      return E_NOTIMPL;
    }
  }
  STDMETHOD(EnumVerbs)(IEnumOLEVERB** ppEnumOleVerb) override {
    if (ppEnumOleVerb == NULL) return E_POINTER;
    return ::OleRegEnumVerbs(T::GetObjectCLSID(), ppEnumOleVerb);
  }
  STDMETHOD(Update)() override { return S_OK; }
  STDMETHOD(IsUpToDate)() override { return S_OK; }
  STDMETHOD(GetUserClassID)(CLSID* pClsid) override {
    if (pClsid == NULL) return E_POINTER;
    *pClsid = T::GetObjectCLSID();
    return S_OK;
  }
  STDMETHOD(GetUserType)(DWORD dwFormOfType, LPOLESTR* pszUserType) override {
    return ::OleRegGetUserType(T::GetObjectCLSID(), dwFormOfType, pszUserType);
  }
  STDMETHOD(SetExtent)(DWORD dwDrawAspect, SIZEL* psizel) override {
    return static_cast<T*>(this)->IOleObject_SetExtent(dwDrawAspect, psizel);
  }
  STDMETHOD(GetExtent)(DWORD dwDrawAspect, SIZEL* psizel) override {
    if (dwDrawAspect != DVASPECT_CONTENT) return E_FAIL;
    if (psizel == NULL) return E_POINTER;
    *psizel = static_cast<T*>(this)->m_sizeExtent;
    return S_OK;
  }
  STDMETHOD(Advise)(IAdviseSink* pAdvSink, DWORD* pdwConnection) override {
    return static_cast<T*>(this)->IOleObject_Advise(pAdvSink, pdwConnection);
  }
  STDMETHOD(Unadvise)(DWORD dwConnection) override {
    T* pT = static_cast<T*>(this);
    if (pT->m_spOleAdviseHolder == NULL) return OLE_E_NOCONNECTION;
    return pT->m_spOleAdviseHolder->Unadvise(dwConnection);
  }
  STDMETHOD(EnumAdvise)(IEnumSTATDATA** ppenumAdvise) override {
    if (ppenumAdvise == NULL) return E_POINTER;
    *ppenumAdvise = NULL;
    T* pT = static_cast<T*>(this);
    if (pT->m_spOleAdviseHolder == NULL) return S_OK;
    return pT->m_spOleAdviseHolder->EnumAdvise(ppenumAdvise);
  }
  STDMETHOD(GetMiscStatus)(DWORD dwAspect, DWORD* pdwStatus) override {
    if (pdwStatus == NULL) return E_POINTER;
    DWORD dw = T::_GetMiscStatus();
    if (dw != 0) {
      *pdwStatus = dw;
      return S_OK;
    }
    return ::OleRegGetMiscStatus(T::GetObjectCLSID(), dwAspect, pdwStatus);
  }
  STDMETHOD(SetColorScheme)(LOGPALETTE*) override { return E_NOTIMPL; }
};

template <class T>
class ATL_NO_VTABLE IOleInPlaceObjectWindowlessImpl : public IOleInPlaceObjectWindowless {
public:
  STDMETHOD(GetWindow)(HWND* phwnd) override {
    return static_cast<T*>(this)->IOleInPlaceObject_GetWindow(phwnd);
  }
  STDMETHOD(ContextSensitiveHelp)(BOOL) override { return E_NOTIMPL; }
  STDMETHOD(InPlaceDeactivate)() override {
    return static_cast<T*>(this)->IOleInPlaceObject_InPlaceDeactivate();
  }
  STDMETHOD(UIDeactivate)() override {
    return static_cast<T*>(this)->IOleInPlaceObject_UIDeactivate();
  }
  STDMETHOD(SetObjectRects)(LPCRECT prcPos, LPCRECT prcClip) override {
    return static_cast<T*>(this)->IOleInPlaceObject_SetObjectRects(prcPos, prcClip);
  }
  STDMETHOD(ReactivateAndUndo)() override { return E_NOTIMPL; }
  // A windowless control's messages, from its site, to its message map.
  STDMETHOD(OnWindowMessage)(UINT msg, WPARAM wParam, LPARAM lParam, LRESULT* plResult) override {
    T* pT = static_cast<T*>(this);
    LRESULT lRes = 0;
    BOOL b = pT->ProcessWindowMessage(pT->m_hWnd, msg, wParam, lParam, lRes, 0);
    if (plResult != NULL) *plResult = lRes;
    return b ? S_OK : S_FALSE;
  }
  STDMETHOD(GetDropTarget)(IDropTarget** ppDropTarget) override {
    if (ppDropTarget != NULL) *ppDropTarget = NULL;
    return E_NOTIMPL;
  }
};

template <class T>
class ATL_NO_VTABLE IOleInPlaceActiveObjectImpl : public IOleInPlaceActiveObject {
public:
  STDMETHOD(GetWindow)(HWND* phwnd) override {
    return static_cast<T*>(this)->IOleInPlaceObject_GetWindow(phwnd);
  }
  STDMETHOD(ContextSensitiveHelp)(BOOL) override { return E_NOTIMPL; }
  STDMETHOD(TranslateAccelerator)(LPMSG pMsg) override {
    T* pT = static_cast<T*>(this);
    HRESULT hRet = S_OK;
    if (pT->PreTranslateAccelerator(pMsg, hRet)) return hRet;
    // Its container's accelerators next.
    CComPtr<IOleControlSite> spCS;
    if (pT->m_spClientSite != NULL &&
        SUCCEEDED(pT->m_spClientSite->QueryInterface(IID_IOleControlSite, (void**)&spCS))) {
      DWORD dwKeyMod = 0;
      if (::GetKeyState(VK_SHIFT) < 0) dwKeyMod |= 1;
      if (::GetKeyState(VK_CONTROL) < 0) dwKeyMod |= 2;
      if (::GetKeyState(VK_MENU) < 0) dwKeyMod |= 4;
      return spCS->TranslateAccelerator(pMsg, dwKeyMod);
    }
    return S_FALSE;
  }
  STDMETHOD(OnFrameWindowActivate)(BOOL) override { return S_OK; }
  STDMETHOD(OnDocWindowActivate)(BOOL fActivate) override {
    if (!fActivate) static_cast<T*>(this)->IOleInPlaceObject_UIDeactivate();
    return S_OK;
  }
  STDMETHOD(ResizeBorder)(LPCRECT, IOleInPlaceUIWindow*, BOOL) override { return S_OK; }
  STDMETHOD(EnableModeless)(BOOL) override { return S_OK; }
};

template <class T>
class ATL_NO_VTABLE IOleControlImpl : public IOleControl {
public:
  STDMETHOD(GetControlInfo)(LPCONTROLINFO) override { return E_NOTIMPL; }
  STDMETHOD(OnMnemonic)(LPMSG) override { return E_NOTIMPL; }
  STDMETHOD(OnAmbientPropertyChange)(DISPID) override { return S_OK; }
  STDMETHOD(FreezeEvents)(BOOL bFreeze) override {
    T* pT = static_cast<T*>(this);
    if (bFreeze)
      pT->m_nFreezeEvents++;
    else if (pT->m_nFreezeEvents > 0)
      pT->m_nFreezeEvents--;
    return S_OK;
  }
};

template <class T>
class ATL_NO_VTABLE IViewObjectExImpl : public IViewObjectEx {
public:
  STDMETHOD(Draw)(DWORD dwDrawAspect, LONG lindex, void* pvAspect, DVTARGETDEVICE* ptd,
                  HDC hicTargetDev, HDC hdcDraw, LPCRECTL prcBounds, LPCRECTL prcWBounds,
                  BOOL(STDMETHODCALLTYPE*)(ULONG_PTR), ULONG_PTR) override {
    return static_cast<T*>(this)->IViewObject_Draw(dwDrawAspect, lindex, pvAspect, ptd,
                                                   hicTargetDev, hdcDraw, prcBounds, prcWBounds);
  }
  STDMETHOD(GetColorSet)(DWORD, LONG, void*, DVTARGETDEVICE*, HDC, LOGPALETTE**) override {
    return E_NOTIMPL;
  }
  STDMETHOD(Freeze)(DWORD, LONG, void*, DWORD*) override { return E_NOTIMPL; }
  STDMETHOD(Unfreeze)(DWORD) override { return E_NOTIMPL; }
  STDMETHOD(SetAdvise)(DWORD aspects, DWORD advf, IAdviseSink* pAdvSink) override {
    T* pT = static_cast<T*>(this);
    pT->m_spAdviseSink = pAdvSink;
    pT->m_dwViewAspects = aspects;
    pT->m_dwViewAdvf = advf;
    return S_OK;
  }
  STDMETHOD(GetAdvise)(DWORD* pAspects, DWORD* pAdvf, IAdviseSink** ppAdvSink) override {
    T* pT = static_cast<T*>(this);
    if (pAspects != NULL) *pAspects = pT->m_dwViewAspects;
    if (pAdvf != NULL) *pAdvf = pT->m_dwViewAdvf;
    if (ppAdvSink != NULL) {
      *ppAdvSink = pT->m_spAdviseSink;
      if (*ppAdvSink != NULL) (*ppAdvSink)->AddRef();
    }
    return S_OK;
  }
  // IViewObject2
  STDMETHOD(GetExtent)(DWORD dwDrawAspect, LONG, DVTARGETDEVICE*, LPSIZEL lpsizel) override {
    if (dwDrawAspect != DVASPECT_CONTENT) return E_FAIL;
    if (lpsizel == NULL) return E_POINTER;
    *lpsizel = static_cast<T*>(this)->m_sizeExtent;
    return S_OK;
  }
  // IViewObjectEx
  STDMETHOD(GetRect)(DWORD dwAspect, LPRECTL pRect) override {
    if (pRect == NULL) return E_POINTER;
    T* pT = static_cast<T*>(this);
    if (dwAspect == DVASPECT_CONTENT ||
        (dwAspect == DVASPECT_OPAQUE && (T::_GetViewStatus() & VIEWSTATUS_OPAQUE))) {
      pRect->left = pT->m_rcPos.left;
      pRect->top = pT->m_rcPos.top;
      pRect->right = pT->m_rcPos.right;
      pRect->bottom = pT->m_rcPos.bottom;
      return S_OK;
    }
    return DV_E_DVASPECT;
  }
  STDMETHOD(GetViewStatus)(DWORD* pdwStatus) override {
    if (pdwStatus == NULL) return E_POINTER;
    *pdwStatus = T::_GetViewStatus();
    return S_OK;
  }
  STDMETHOD(QueryHitPoint)(DWORD dwAspect, LPCRECT pRectBounds, POINT ptlLoc, LONG,
                           DWORD* pHitResult) override {
    if (pRectBounds == NULL || pHitResult == NULL) return E_POINTER;
    if (dwAspect != DVASPECT_CONTENT) return E_FAIL;
    *pHitResult = ::PtInRect(pRectBounds, ptlLoc) ? HITRESULT_HIT : HITRESULT_OUTSIDE;
    return S_OK;
  }
  STDMETHOD(QueryHitRect)(DWORD dwAspect, LPCRECT pRectBounds, LPCRECT prcLoc, LONG,
                          DWORD* pHitResult) override {
    if (pRectBounds == NULL || prcLoc == NULL || pHitResult == NULL) return E_POINTER;
    if (dwAspect != DVASPECT_CONTENT) return E_FAIL;
    RECT rc;
    *pHitResult = ::IntersectRect(&rc, pRectBounds, prcLoc) ? HITRESULT_HIT : HITRESULT_OUTSIDE;
    return S_OK;
  }
  STDMETHOD(GetNaturalExtent)(DWORD dwAspect, LONG, DVTARGETDEVICE*, HDC,
                              DVEXTENTINFO* pExtentInfo, LPSIZEL psizel) override {
    if (pExtentInfo == NULL || psizel == NULL) return E_POINTER;
    T* pT = static_cast<T*>(this);
    if (dwAspect == DVASPECT_CONTENT && pExtentInfo->dwExtentMode == DVEXTENT_CONTENT) {
      *psizel = pT->m_sizeNatural;
      return S_OK;
    }
    return E_NOTIMPL;
  }
};

// The container sets the control up in one call.
template <class T>
class ATL_NO_VTABLE IQuickActivateImpl : public IQuickActivate {
public:
  STDMETHOD(QuickActivate)(QACONTAINER* pQACont, QACONTROL* pQACtrl) override {
    if (pQACont == NULL || pQACtrl == NULL) return E_POINTER;
    if (pQACont->cbSize < sizeof(QACONTAINER) || pQACtrl->cbSize < sizeof(QACONTROL))
      return E_INVALIDARG;
    T* pT = static_cast<T*>(this);
    IUnknown* pUnk = pT->GetUnknown();
    ULONG cb = pQACtrl->cbSize;
    memset(pQACtrl, 0, cb);
    pQACtrl->cbSize = cb;
    pT->IOleObject_SetClientSite(pQACont->pClientSite);
    if (pQACont->pAdviseSink != NULL) {
      pT->m_spAdviseSink = pQACont->pAdviseSink;
      pT->m_dwViewAspects = DVASPECT_CONTENT;
    }
    CComPtr<IConnectionPointContainer> spCPC;
    if (SUCCEEDED(pUnk->QueryInterface(IID_IConnectionPointContainer, (void**)&spCPC))) {
      CComPtr<IConnectionPoint> spCP;
      if (pQACont->pPropertyNotifySink != NULL &&
          SUCCEEDED(spCPC->FindConnectionPoint(IID_IPropertyNotifySink, &spCP)))
        spCP->Advise(pQACont->pPropertyNotifySink, &pQACtrl->dwPropNotifyCookie);
      spCP.Release();
      CComPtr<IProvideClassInfo2> spPCI;
      IID iidEvents;
      if (pQACont->pUnkEventSink != NULL &&
          SUCCEEDED(pUnk->QueryInterface(IID_IProvideClassInfo2, (void**)&spPCI)) &&
          SUCCEEDED(spPCI->GetGUID(GUIDKIND_DEFAULT_SOURCE_DISP_IID, &iidEvents)) &&
          SUCCEEDED(spCPC->FindConnectionPoint(iidEvents, &spCP)))
        spCP->Advise(pQACont->pUnkEventSink, &pQACtrl->dwEventCookie);
    }
    CComPtr<IOleObject> spOO;
    if (SUCCEEDED(pUnk->QueryInterface(IID_IOleObject, (void**)&spOO)))
      spOO->GetMiscStatus(DVASPECT_CONTENT, &pQACtrl->dwMiscStatus);
    pQACtrl->dwViewStatus = T::_GetViewStatus();
    return S_OK;
  }
  STDMETHOD(SetContentExtent)(LPSIZEL pSizel) override {
    return static_cast<T*>(this)->IOleObject_SetExtent(DVASPECT_CONTENT, pSizel);
  }
  STDMETHOD(GetContentExtent)(LPSIZEL pSizel) override {
    if (pSizel == NULL) return E_POINTER;
    *pSizel = static_cast<T*>(this)->m_sizeExtent;
    return S_OK;
  }
};

template <class T>
class ATL_NO_VTABLE IDataObjectImpl : public IDataObject {
public:
  STDMETHOD(GetData)(FORMATETC* pformatetcIn, STGMEDIUM* pmedium) override {
    return static_cast<T*>(this)->IDataObject_GetData(pformatetcIn, pmedium);
  }
  STDMETHOD(GetDataHere)(FORMATETC*, STGMEDIUM*) override { return E_NOTIMPL; }
  STDMETHOD(QueryGetData)(FORMATETC* pformatetc) override {
    if (pformatetc == NULL) return E_POINTER;
    return pformatetc->cfFormat == CF_METAFILEPICT && (pformatetc->tymed & TYMED_MFPICT)
               ? S_OK
               : DV_E_FORMATETC;
  }
  STDMETHOD(GetCanonicalFormatEtc)(FORMATETC*, FORMATETC* pformatetcOut) override {
    if (pformatetcOut != NULL) pformatetcOut->ptd = NULL;
    return E_NOTIMPL;
  }
  STDMETHOD(SetData)(FORMATETC*, STGMEDIUM*, BOOL) override { return E_NOTIMPL; }
  STDMETHOD(EnumFormatEtc)(DWORD dwDirection, IEnumFORMATETC** ppenumFormatEtc) override {
    return ::OleRegEnumFormatEtc(T::GetObjectCLSID(), dwDirection, ppenumFormatEtc);
  }
  STDMETHOD(DAdvise)(FORMATETC* pformatetc, DWORD advf, IAdviseSink* pAdvSink,
                     DWORD* pdwConnection) override {
    T* pT = static_cast<T*>(this);
    HRESULT hr = S_OK;
    if (pT->m_spDataAdviseHolder == NULL) hr = ::CreateDataAdviseHolder(&pT->m_spDataAdviseHolder);
    if (SUCCEEDED(hr))
      hr = pT->m_spDataAdviseHolder->Advise(this, pformatetc, advf, pAdvSink, pdwConnection);
    return hr;
  }
  STDMETHOD(DUnadvise)(DWORD dwConnection) override {
    T* pT = static_cast<T*>(this);
    if (pT->m_spDataAdviseHolder == NULL) return OLE_E_NOCONNECTION;
    return pT->m_spDataAdviseHolder->Unadvise(dwConnection);
  }
  STDMETHOD(EnumDAdvise)(IEnumSTATDATA** ppenumAdvise) override {
    if (ppenumAdvise == NULL) return E_POINTER;
    *ppenumAdvise = NULL;
    T* pT = static_cast<T*>(this);
    if (pT->m_spDataAdviseHolder == NULL) return S_OK;
    return pT->m_spDataAdviseHolder->EnumAdvise(ppenumAdvise);
  }
};

template <class T>
class ATL_NO_VTABLE IRunnableObjectImpl : public IRunnableObject {
public:
  STDMETHOD(GetRunningClass)(LPCLSID lpClsid) override {
    if (lpClsid == NULL) return E_POINTER;
    *lpClsid = GUID_NULL;
    return E_UNEXPECTED;
  }
  STDMETHOD(Run)(LPBINDCTX) override { return S_OK; }
  virtual BOOL STDMETHODCALLTYPE IsRunning() override { return TRUE; }
  STDMETHOD(LockRunning)(BOOL, BOOL) override { return S_OK; }
  STDMETHOD(SetContainedObject)(BOOL) override { return S_OK; }
};

template <class T>
class ATL_NO_VTABLE IPointerInactiveImpl : public IPointerInactive {
public:
  STDMETHOD(GetActivationPolicy)(DWORD* pdwPolicy) override {
    if (pdwPolicy == NULL) return E_POINTER;
    *pdwPolicy = 0;
    return S_OK;
  }
  STDMETHOD(OnInactiveMouseMove)(LPCRECT, LONG, LONG, DWORD) override { return S_OK; }
  STDMETHOD(OnInactiveSetCursor)(LPCRECT, LONG, LONG, DWORD, BOOL) override { return S_OK; }
};

template <class T>
class ATL_NO_VTABLE IPerPropertyBrowsingImpl : public IPerPropertyBrowsing {
public:
  STDMETHOD(GetDisplayString)(DISPID dispID, BSTR* pBstr) override {
    if (pBstr == NULL) return E_POINTER;
    *pBstr = NULL;
    T* pT = static_cast<T*>(this);
    CComPtr<IDispatch> spDisp;
    if (FAILED(pT->GetUnknown()->QueryInterface(IID_IDispatch, (void**)&spDisp))) return E_FAIL;
    CComVariant var;
    if (FAILED(_AtlGetProperty(spDisp, dispID, &var))) return S_FALSE;
    if (var.vt == VT_DISPATCH || var.vt == VT_UNKNOWN) return S_FALSE;
    if (FAILED(var.ChangeType(VT_BSTR))) return S_FALSE;
    *pBstr = var.bstrVal;
    var.vt = VT_EMPTY;
    return S_OK;
  }
  STDMETHOD(MapPropertyToPage)(DISPID dispID, CLSID* pClsid) override {
    if (pClsid == NULL) return E_POINTER;
    *pClsid = CLSID_NULL;
    for (const ATL_PROPMAP_ENTRY* p = T::GetPropertyMap(); !_AtlPropMapEnd(p); p++) {
      if (p->szDesc != NULL && p->dwSizeData == 0 && p->dispid == dispID) {
        if (p->pclsidPropPage != NULL) *pClsid = *p->pclsidPropPage;
        return S_OK;
      }
    }
    return PERPROP_E_NOPAGEAVAILABLE;
  }
  STDMETHOD(GetPredefinedStrings)(DISPID, CALPOLESTR* pCaStringsOut,
                                  CADWORD* pCaCookiesOut) override {
    if (pCaStringsOut != NULL) {
      pCaStringsOut->cElems = 0;
      pCaStringsOut->pElems = NULL;
    }
    if (pCaCookiesOut != NULL) {
      pCaCookiesOut->cElems = 0;
      pCaCookiesOut->pElems = NULL;
    }
    return S_OK;
  }
  STDMETHOD(GetPredefinedValue)(DISPID, DWORD, VARIANT*) override { return E_NOTIMPL; }
};

// ---------------------------------------------------------------------
// Stock properties: CStockPropImpl gives the get_ and put_ methods of the
// stock properties the interface names, each on the class's own member
// of the usual name (m_clrBackColor and so on). A property whose member
// the class does not have answers E_NOTIMPL.

template <class X>
struct _AtlIsVoidPtr {
  enum { value = 0 };
};
template <>
struct _AtlIsVoidPtr<void*> {
  enum { value = 1 };
};

#define _ATL_STOCK_MEMBER(name, member)                                               \
  template <class X>                                                                 \
  static auto _StockPtr_##name(X* p, int) -> decltype(&p->member) { return &p->member; } \
  template <class X>                                                                 \
  static void* _StockPtr_##name(X*, long) { return NULL; }

// A property kept as it is passed.
#define _ATL_STOCK_VALUE(name, type, member, dispid)                     \
  _ATL_STOCK_MEMBER(name, member)                                        \
  STDMETHOD(put_##name)(type v) {                                        \
    T* pT = static_cast<T*>(this);                                       \
    auto pm = _StockPtr_##name(pT, 0);                                   \
    if constexpr (_AtlIsVoidPtr<decltype(pm)>::value) {                  \
      (void)v;                                                           \
      return E_NOTIMPL;                                                  \
    } else {                                                             \
      if (pT->FireOnRequestEdit(dispid) == S_FALSE) return S_FALSE;      \
      *pm = v;                                                           \
      pT->m_bRequiresSave = TRUE;                                        \
      pT->FireOnChanged(dispid);                                         \
      pT->On##name##Changed();                                           \
      pT->FireViewChange();                                              \
      return S_OK;                                                       \
    }                                                                    \
  }                                                                      \
  STDMETHOD(get_##name)(type * p) {                                      \
    if (p == NULL) return E_POINTER;                                     \
    T* pT = static_cast<T*>(this);                                       \
    auto pm = _StockPtr_##name(pT, 0);                                   \
    if constexpr (_AtlIsVoidPtr<decltype(pm)>::value) {                  \
      return E_NOTIMPL;                                                  \
    } else {                                                             \
      *p = (type)*pm;                                                    \
      return S_OK;                                                       \
    }                                                                    \
  }

// A VARIANT_BOOL property kept as a BOOL.
#define _ATL_STOCK_BOOL(name, member, dispid)                            \
  _ATL_STOCK_MEMBER(name, member)                                        \
  STDMETHOD(put_##name)(VARIANT_BOOL v) {                                \
    T* pT = static_cast<T*>(this);                                       \
    auto pm = _StockPtr_##name(pT, 0);                                   \
    if constexpr (_AtlIsVoidPtr<decltype(pm)>::value) {                  \
      (void)v;                                                           \
      return E_NOTIMPL;                                                  \
    } else {                                                             \
      if (pT->FireOnRequestEdit(dispid) == S_FALSE) return S_FALSE;      \
      *pm = v != VARIANT_FALSE;                                          \
      pT->m_bRequiresSave = TRUE;                                        \
      pT->FireOnChanged(dispid);                                         \
      pT->On##name##Changed();                                           \
      pT->FireViewChange();                                              \
      return S_OK;                                                       \
    }                                                                    \
  }                                                                      \
  STDMETHOD(get_##name)(VARIANT_BOOL * p) {                              \
    if (p == NULL) return E_POINTER;                                     \
    T* pT = static_cast<T*>(this);                                       \
    auto pm = _StockPtr_##name(pT, 0);                                   \
    if constexpr (_AtlIsVoidPtr<decltype(pm)>::value) {                  \
      return E_NOTIMPL;                                                  \
    } else {                                                             \
      *p = *pm ? VARIANT_TRUE : VARIANT_FALSE;                           \
      return S_OK;                                                       \
    }                                                                    \
  }

// A string property kept as a CComBSTR (or BSTR).
#define _ATL_STOCK_BSTR(name, member, dispid)                            \
  _ATL_STOCK_MEMBER(name, member)                                        \
  STDMETHOD(put_##name)(BSTR v) {                                        \
    T* pT = static_cast<T*>(this);                                       \
    auto pm = _StockPtr_##name(pT, 0);                                   \
    if constexpr (_AtlIsVoidPtr<decltype(pm)>::value) {                  \
      (void)v;                                                           \
      return E_NOTIMPL;                                                  \
    } else {                                                             \
      if (pT->FireOnRequestEdit(dispid) == S_FALSE) return S_FALSE;      \
      _AtlStockSetBSTR(*pm, v);                                          \
      pT->m_bRequiresSave = TRUE;                                        \
      pT->FireOnChanged(dispid);                                         \
      pT->On##name##Changed();                                           \
      pT->FireViewChange();                                              \
      return S_OK;                                                       \
    }                                                                    \
  }                                                                      \
  STDMETHOD(get_##name)(BSTR * p) {                                      \
    if (p == NULL) return E_POINTER;                                     \
    T* pT = static_cast<T*>(this);                                       \
    auto pm = _StockPtr_##name(pT, 0);                                   \
    if constexpr (_AtlIsVoidPtr<decltype(pm)>::value) {                  \
      *p = NULL;                                                         \
      return E_NOTIMPL;                                                  \
    } else {                                                             \
      BSTR b = *pm;                                                      \
      *p = ::SysAllocStringLen(b, ::SysStringLen(b));                    \
      return S_OK;                                                       \
    }                                                                    \
  }

// An object property (a font or a picture) kept as a CComPtr; put_
// copies a font, putref_ keeps the one given.
#define _ATL_STOCK_OBJECT(name, itf, member, dispid, bClone)             \
  _ATL_STOCK_MEMBER(name, member)                                        \
  HRESULT _StockSet##name(itf* v, bool bCopy) {                          \
    T* pT = static_cast<T*>(this);                                       \
    auto pm = _StockPtr_##name(pT, 0);                                   \
    if constexpr (_AtlIsVoidPtr<decltype(pm)>::value) {                  \
      (void)v;                                                           \
      (void)bCopy;                                                       \
      return E_NOTIMPL;                                                  \
    } else {                                                             \
      if (pT->FireOnRequestEdit(dispid) == S_FALSE) return S_FALSE;      \
      CComPtr<itf> sp(v);                                                \
      if (bCopy && v != NULL) _AtlStockCloneFont(v, &sp.p);              \
      *pm = sp;                                                          \
      pT->m_bRequiresSave = TRUE;                                        \
      pT->FireOnChanged(dispid);                                         \
      pT->On##name##Changed();                                           \
      pT->FireViewChange();                                              \
      return S_OK;                                                       \
    }                                                                    \
  }                                                                      \
  STDMETHOD(put_##name)(itf * v) { return _StockSet##name(v, bClone); }  \
  STDMETHOD(putref_##name)(itf * v) { return _StockSet##name(v, false); } \
  STDMETHOD(get_##name)(itf * *pp) {                                     \
    if (pp == NULL) return E_POINTER;                                    \
    *pp = NULL;                                                          \
    T* pT = static_cast<T*>(this);                                       \
    auto pm = _StockPtr_##name(pT, 0);                                   \
    if constexpr (_AtlIsVoidPtr<decltype(pm)>::value) {                  \
      return E_NOTIMPL;                                                  \
    } else {                                                             \
      itf* p = *pm;                                                      \
      if (p != NULL) p->AddRef();                                        \
      *pp = p;                                                           \
      return S_OK;                                                       \
    }                                                                    \
  }

inline void _AtlStockSetBSTR(CComBSTR& s, BSTR v) { s = v; }
inline void _AtlStockSetBSTR(BSTR& s, BSTR v) {
  ::SysFreeString(s);
  s = ::SysAllocStringLen(v, ::SysStringLen(v));
}
// A copy of a font, so that the control's changes stay its own; other
// objects are kept as given.
template <class I>
inline void _AtlStockCloneFont(I*, I**) {}
inline void _AtlStockCloneFont(IFontDisp* v, IFontDisp** pp) {
  CComPtr<IFont> spFont, spClone;
  if (SUCCEEDED(v->QueryInterface(IID_IFont, (void**)&spFont)) &&
      SUCCEEDED(spFont->Clone(&spClone))) {
    IFontDisp* pClone = NULL;
    if (SUCCEEDED(spClone->QueryInterface(IID_IFontDisp, (void**)&pClone))) {
      if (*pp != NULL) (*pp)->Release();
      *pp = pClone;
    }
  }
}

template <class T, class InterfaceName, const IID* piid = &__uuidof(InterfaceName),
          const GUID* plibid = &CAtlModule::m_libid>
class ATL_NO_VTABLE CStockPropImpl : public IDispatchImpl<InterfaceName, piid, plibid> {
public:
  _ATL_STOCK_VALUE(Appearance, short, m_nAppearance, DISPID_APPEARANCE)
  _ATL_STOCK_BOOL(AutoSize, m_bAutoSize, DISPID_AUTOSIZE)
  _ATL_STOCK_VALUE(BackColor, OLE_COLOR, m_clrBackColor, DISPID_BACKCOLOR)
  _ATL_STOCK_VALUE(BackStyle, long, m_nBackStyle, DISPID_BACKSTYLE)
  _ATL_STOCK_VALUE(BorderColor, OLE_COLOR, m_clrBorderColor, DISPID_BORDERCOLOR)
  _ATL_STOCK_VALUE(BorderStyle, long, m_nBorderStyle, DISPID_BORDERSTYLE)
  _ATL_STOCK_BOOL(BorderVisible, m_bBorderVisible, DISPID_BORDERVISIBLE)
  _ATL_STOCK_VALUE(BorderWidth, long, m_nBorderWidth, DISPID_BORDERWIDTH)
  _ATL_STOCK_BSTR(Caption, m_bstrCaption, DISPID_CAPTION)
  _ATL_STOCK_VALUE(DrawMode, long, m_nDrawMode, DISPID_DRAWMODE)
  _ATL_STOCK_VALUE(DrawStyle, long, m_nDrawStyle, DISPID_DRAWSTYLE)
  _ATL_STOCK_VALUE(DrawWidth, long, m_nDrawWidth, DISPID_DRAWWIDTH)
  _ATL_STOCK_BOOL(Enabled, m_bEnabled, DISPID_ENABLED)
  _ATL_STOCK_VALUE(FillColor, OLE_COLOR, m_clrFillColor, DISPID_FILLCOLOR)
  _ATL_STOCK_VALUE(FillStyle, long, m_nFillStyle, DISPID_FILLSTYLE)
  _ATL_STOCK_OBJECT(Font, IFontDisp, m_pFont, DISPID_FONT, true)
  _ATL_STOCK_VALUE(ForeColor, OLE_COLOR, m_clrForeColor, DISPID_FORECOLOR)
  _ATL_STOCK_OBJECT(MouseIcon, IPictureDisp, m_pMouseIcon, DISPID_MOUSEICON, false)
  _ATL_STOCK_VALUE(MousePointer, long, m_nMousePointer, DISPID_MOUSEPOINTER)
  _ATL_STOCK_OBJECT(Picture, IPictureDisp, m_pPicture, DISPID_PICTURE, false)
  _ATL_STOCK_VALUE(ReadyState, long, m_nReadyState, DISPID_READYSTATE)
  _ATL_STOCK_BOOL(TabStop, m_bTabStop, DISPID_TABSTOP)
  _ATL_STOCK_BSTR(Text, m_bstrText, DISPID_TEXT)
  _ATL_STOCK_BOOL(Valid, m_bValid, DISPID_VALID)

  STDMETHOD(get_HWND)(LONG_PTR* phWnd) {
    if (phWnd == NULL) return E_POINTER;
    *phWnd = (LONG_PTR)static_cast<T*>(this)->m_hWnd;
    return S_OK;
  }
  STDMETHOD(get_Window)(LONG_PTR* phWnd) { return get_HWND(phWnd); }
  STDMETHOD(put_Window)(LONG_PTR) { return E_FAIL; }
};

#undef _ATL_STOCK_MEMBER
#undef _ATL_STOCK_VALUE
#undef _ATL_STOCK_BOOL
#undef _ATL_STOCK_BSTR
#undef _ATL_STOCK_OBJECT

// ---------------------------------------------------------------------
// Property pages: a dialog (T::IDD) that edits the objects the frame
// gives it, through IPropertyPage.

template <class T, class _PageBase = IPropertyPage>
class ATL_NO_VTABLE IPropertyPageImpl : public _PageBase {
public:
  IPropertyPageImpl()
      : m_pPageSite(NULL), m_ppUnk(NULL), m_nObjects(0), m_bDirty(FALSE), m_dwTitleID(0),
        m_dwHelpFileID(0), m_dwDocStringID(0), m_dwHelpContext(0) {
    m_size.cx = m_size.cy = 0;
  }
  ~IPropertyPageImpl() {
    if (m_pPageSite != NULL) m_pPageSite->Release();
    _ReleaseObjects();
  }

  void SetDirty(BOOL bDirty) {
    if (m_bDirty == bDirty) return;
    m_bDirty = bDirty;
    if (m_pPageSite != NULL)
      m_pPageSite->OnStatusChange(bDirty ? PROPPAGESTATUS_DIRTY | PROPPAGESTATUS_VALIDATE : 0);
  }

  STDMETHOD(SetPageSite)(IPropertyPageSite* pPageSite) override {
    if (pPageSite != NULL && m_pPageSite != NULL) return E_UNEXPECTED;
    if (m_pPageSite != NULL) m_pPageSite->Release();
    m_pPageSite = pPageSite;
    if (m_pPageSite != NULL) m_pPageSite->AddRef();
    return S_OK;
  }
  STDMETHOD(Activate)(HWND hWndParent, LPCRECT pRect, BOOL) override {
    T* pT = static_cast<T*>(this);
    if (pRect == NULL) return E_POINTER;
    HWND h = pT->Create(hWndParent);
    if (h == NULL) return E_FAIL;
    ::SetWindowLong(h, GWL_STYLE, (::GetWindowLong(h, GWL_STYLE) & ~(WS_POPUP | WS_CAPTION)) | WS_CHILD);
    ::SetParent(h, hWndParent);
    Move(pRect);
    m_size.cx = pRect->right - pRect->left;
    m_size.cy = pRect->bottom - pRect->top;
    return S_OK;
  }
  STDMETHOD(Deactivate)() override {
    T* pT = static_cast<T*>(this);
    if (pT->m_hWnd != NULL) pT->DestroyWindow();
    return S_OK;
  }
  STDMETHOD(GetPageInfo)(PROPPAGEINFO* pPageInfo) override {
    if (pPageInfo == NULL) return E_POINTER;
    T* pT = static_cast<T*>(this);
    memset(pPageInfo, 0, sizeof(*pPageInfo));
    pPageInfo->cb = sizeof(PROPPAGEINFO);
    pPageInfo->pszTitle = _LoadString(m_dwTitleID);
    pPageInfo->pszDocString = _LoadString(m_dwDocStringID);
    pPageInfo->pszHelpFile = _LoadString(m_dwHelpFileID);
    pPageInfo->dwHelpContext = m_dwHelpContext;
    // The dialog's size, from its template.
    SIZE size = m_size;
    if (size.cx == 0) {
      HWND h = ::CreateDialogParam(_AtlBaseModule.GetResourceInstance(), MAKEINTRESOURCE(T::IDD),
                                   ::GetDesktopWindow(), _EmptyDlgProc, 0);
      if (h != NULL) {
        RECT rc;
        ::GetWindowRect(h, &rc);
        size.cx = rc.right - rc.left;
        size.cy = rc.bottom - rc.top;
        ::DestroyWindow(h);
      }
    }
    pPageInfo->size = size;
    (void)pT;
    return S_OK;
  }
  STDMETHOD(SetObjects)(ULONG nObjects, IUnknown** ppUnk) override {
    _ReleaseObjects();
    if (nObjects == 0) return S_OK;
    if (ppUnk == NULL) return E_POINTER;
    m_ppUnk = new (std::nothrow) IUnknown*[nObjects];
    if (m_ppUnk == NULL) return E_OUTOFMEMORY;
    for (ULONG i = 0; i < nObjects; i++) {
      m_ppUnk[i] = ppUnk[i];
      if (m_ppUnk[i] != NULL) m_ppUnk[i]->AddRef();
    }
    m_nObjects = nObjects;
    return S_OK;
  }
  STDMETHOD(Show)(UINT nCmdShow) override {
    T* pT = static_cast<T*>(this);
    if (pT->m_hWnd == NULL) return E_UNEXPECTED;
    ::ShowWindow(pT->m_hWnd, nCmdShow);
    return S_OK;
  }
  STDMETHOD(Move)(LPCRECT pRect) override {
    T* pT = static_cast<T*>(this);
    if (pRect == NULL) return E_POINTER;
    if (pT->m_hWnd == NULL) return E_UNEXPECTED;
    ::MoveWindow(pT->m_hWnd, pRect->left, pRect->top, pRect->right - pRect->left,
                 pRect->bottom - pRect->top, TRUE);
    return S_OK;
  }
  STDMETHOD(IsPageDirty)() override { return m_bDirty ? S_OK : S_FALSE; }

  // For the page's map to chain to.
  typedef IPropertyPageImpl<T, _PageBase> _PropertyPageClass;
  BEGIN_MSG_MAP(_PropertyPageClass)
  END_MSG_MAP()
  STDMETHOD(Apply)() override { return S_OK; }
  STDMETHOD(Help)(LPCOLESTR) override { return E_NOTIMPL; }
  STDMETHOD(TranslateAccelerator)(LPMSG pMsg) override {
    T* pT = static_cast<T*>(this);
    if (pMsg == NULL || pT->m_hWnd == NULL) return E_POINTER;
    if ((pMsg->message < WM_KEYFIRST || pMsg->message > WM_KEYLAST) &&
        (pMsg->message < WM_MOUSEFIRST || pMsg->message > WM_MOUSELAST))
      return S_FALSE;
    return ::IsDialogMessage(pT->m_hWnd, pMsg) ? S_OK : S_FALSE;
  }

  IPropertyPageSite* m_pPageSite;
  IUnknown** m_ppUnk;
  ULONG m_nObjects;
  SIZE m_size;
  BOOL m_bDirty;
  UINT m_dwTitleID;
  UINT m_dwHelpFileID;
  UINT m_dwDocStringID;
  DWORD m_dwHelpContext;

private:
  void _ReleaseObjects() {
    for (ULONG i = 0; i < m_nObjects; i++)
      if (m_ppUnk[i] != NULL) m_ppUnk[i]->Release();
    delete[] m_ppUnk;
    m_ppUnk = NULL;
    m_nObjects = 0;
  }
  static LPOLESTR _LoadString(UINT nID) {
    if (nID == 0) return NULL;
    WCHAR sz[256];
    int n = ::LoadStringW(_AtlBaseModule.GetResourceInstance(), nID, sz, 256);
    if (n <= 0) return NULL;
    LPOLESTR p = (LPOLESTR)::CoTaskMemAlloc((n + 1) * sizeof(WCHAR));
    if (p != NULL) memcpy(p, sz, (n + 1) * sizeof(WCHAR));
    return p;
  }
  static INT_PTR CALLBACK _EmptyDlgProc(HWND, UINT, WPARAM, LPARAM) { return FALSE; }
};

// IPropertyPage2: a page that can be asked to show one property. The
// page does nothing with it unless T has EditProperty of its own.
template <class T>
class ATL_NO_VTABLE IPropertyPage2Impl : public IPropertyPageImpl<T, IPropertyPage2> {
public:
  STDMETHOD(EditProperty)(DISPID /*dispID*/) override { return E_NOTIMPL; }
};

}  // namespace ATL

// ---------------------------------------------------------------------
// Composite controls: CComControl over a dialog of controls (T::IDD).

namespace ATL {

template <class T>
class CComCompositeControl : public CComControl<T, CAxDialogImpl<T> > {
public:
  HBRUSH m_hbrBackground;

  CComCompositeControl() : m_hbrBackground(NULL) { this->m_bWindowOnly = TRUE; }
  ~CComCompositeControl() {
    if (m_hbrBackground != NULL) ::DeleteObject(m_hbrBackground);
  }

  // The dialog's size in HIMETRIC, from its template.
  BOOL CalcExtent(SIZE& size) {
    const DLGTEMPLATE* pOriginal = NULL;
    HINSTANCE hInst = _AtlBaseModule.GetResourceInstance();
    DLGTEMPLATE* pEmpty = _AtlAxLoadDialog(hInst, MAKEINTRESOURCEW(T::IDD), &pOriginal, true);
    if (pEmpty == NULL) return FALSE;
    HWND h = ::CreateDialogIndirectParamW(hInst, pEmpty, ::GetDesktopWindow(), _EmptyDlgProc, 0);
    delete[] (BYTE*)pEmpty;
    if (h == NULL) return FALSE;
    RECT rc;
    ::GetWindowRect(h, &rc);
    ::DestroyWindow(h);
    SIZEL sizePix = {rc.right - rc.left, rc.bottom - rc.top};
    SIZEL sizeHM;
    AtlPixelToHiMetric(&sizePix, &sizeHM);
    size.cx = sizeHM.cx;
    size.cy = sizeHM.cy;
    return TRUE;
  }

  virtual HWND CreateControlWindow(HWND hWndParent, RECT& rcPos) override {
    T* pT = static_cast<T*>(this);
    HWND h = pT->Create(hWndParent);
    if (h != NULL) {
      ::SetWindowLong(h, GWL_STYLE, (::GetWindowLong(h, GWL_STYLE) & ~(WS_POPUP | WS_CAPTION)) |
                                        WS_CHILD | WS_CLIPCHILDREN);
      ::SetParent(h, hWndParent);
      ::SetWindowPos(h, NULL, rcPos.left, rcPos.top, rcPos.right - rcPos.left,
                     rcPos.bottom - rcPos.top, SWP_NOZORDER | SWP_NOACTIVATE);
      SetBackgroundColorFromAmbient();
      pT->AdviseSinkMap(true);
    }
    return h;
  }
  HRESULT SetBackgroundColorFromAmbient() {
    if (m_hbrBackground != NULL) {
      ::DeleteObject(m_hbrBackground);
      m_hbrBackground = NULL;
    }
    OLE_COLOR clr;
    HRESULT hr = this->GetAmbientBackColor(clr);
    if (SUCCEEDED(hr)) {
      COLORREF rgb;
      ::OleTranslateColor(clr, NULL, &rgb);
      m_hbrBackground = ::CreateSolidBrush(rgb);
    }
    return hr;
  }
  BOOL PreTranslateAccelerator(LPMSG pMsg, HRESULT& hRet) {
    if (this->m_hWnd != NULL && pMsg->message >= WM_KEYFIRST && pMsg->message <= WM_KEYLAST &&
        ::IsDialogMessage(this->m_hWnd, pMsg)) {
      hRet = S_OK;
      return TRUE;
    }
    return FALSE;
  }

  typedef CComCompositeControl<T> thisClass;
  BEGIN_MSG_MAP(thisClass)
    MESSAGE_HANDLER(WM_CTLCOLORDLG, OnCtlColorDlg)
    MESSAGE_HANDLER(WM_CTLCOLORSTATIC, OnCtlColorDlg)
    MESSAGE_HANDLER(WM_SETFOCUS, CComControlBase::OnSetFocus)
    MESSAGE_HANDLER(WM_KILLFOCUS, CComControlBase::OnKillFocus)
    MESSAGE_HANDLER(WM_MOUSEACTIVATE, CComControlBase::OnMouseActivate)
    MESSAGE_HANDLER(WM_DESTROY, OnDestroySinksComposite)
  END_MSG_MAP()

  LRESULT OnCtlColorDlg(UINT, WPARAM wParam, LPARAM, BOOL& bHandled) {
    if (m_hbrBackground == NULL) {
      bHandled = FALSE;
      return 0;
    }
    COLORREF clr = 0;
    LOGBRUSH lb;
    if (::GetObject(m_hbrBackground, sizeof(lb), &lb)) clr = lb.lbColor;
    ::SetBkColor((HDC)wParam, clr);
    return (LRESULT)m_hbrBackground;
  }
  LRESULT OnDestroySinksComposite(UINT, WPARAM, LPARAM, BOOL& bHandled) {
    static_cast<T*>(this)->AdviseSinkMap(false);
    bHandled = FALSE;
    return 0;
  }

private:
  static INT_PTR CALLBACK _EmptyDlgProc(HWND, UINT, WPARAM, LPARAM) { return FALSE; }
};

}  // namespace ATL

#endif  // __ATLCTL_H__
