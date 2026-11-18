// atliface.h - the interfaces of ATL's control host (atlhost.h): the
// host window's own (IAxWinHostWindow), its ambient properties
// (IAxWinAmbientDispatch) and the dispatch form of a document host's UI
// handler (IDocHostUIHandlerDispatch).
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#ifndef __ATLIFACE_H__
#define __ATLIFACE_H__

#include <ole2.h>
#include <oleauto.h>
#include <ocidl.h>

// The UI handler a host's document (a WebBrowser control) calls, as an
// automation interface.
struct IDocHostUIHandlerDispatch : public IDispatch {
  STDMETHOD(ShowContextMenu)(DWORD dwID, DWORD x, DWORD y, IUnknown* pcmdtReserved,
                             IDispatch* pdispReserved, HRESULT* dwRetVal) = 0;
  STDMETHOD(GetHostInfo)(DWORD* pdwFlags, DWORD* pdwDoubleClick) = 0;
  STDMETHOD(ShowUI)(DWORD dwID, IUnknown* pActiveObject, IUnknown* pCommandTarget,
                    IUnknown* pFrame, IUnknown* pDoc, HRESULT* dwRetVal) = 0;
  STDMETHOD(HideUI)() = 0;
  STDMETHOD(UpdateUI)() = 0;
  STDMETHOD(EnableModeless)(VARIANT_BOOL fEnable) = 0;
  STDMETHOD(OnDocWindowActivate)(VARIANT_BOOL fActivate) = 0;
  STDMETHOD(OnFrameWindowActivate)(VARIANT_BOOL fActivate) = 0;
  STDMETHOD(ResizeBorder)(long left, long top, long right, long bottom, IUnknown* pUIWindow,
                          VARIANT_BOOL fFrameWindow) = 0;
  STDMETHOD(TranslateAccelerator)(DWORD_PTR hWnd, DWORD nMessage, DWORD_PTR wParam,
                                  DWORD_PTR lParam, BSTR bstrGuidCmdGroup, DWORD nCmdID,
                                  HRESULT* dwRetVal) = 0;
  STDMETHOD(GetOptionKeyPath)(BSTR* pbstrKey, DWORD dw) = 0;
  STDMETHOD(GetDropTarget)(IUnknown* pDropTarget, IUnknown** ppDropTarget) = 0;
  STDMETHOD(GetExternal)(IDispatch** ppDispatch) = 0;
  STDMETHOD(TranslateUrl)(DWORD dwTranslate, BSTR bstrURLIn, BSTR* pbstrURLOut) = 0;
  STDMETHOD(FilterDataObject)(IUnknown* pDO, IUnknown** ppDORet) = 0;
};
__CRT_UUID_DECL(IDocHostUIHandlerDispatch, 0x425b5af0, 0x65f1, 0x11d1, 0x96, 0x11, 0x00, 0x00,
                0xf8, 0x1e, 0x0d, 0x0d)
#define IID_IDocHostUIHandlerDispatch __uuidof(IDocHostUIHandlerDispatch)

// The host window: makes, or takes, the control it shows.
struct IAxWinHostWindow : public IUnknown {
  STDMETHOD(CreateControl)(LPCOLESTR lpTricsData, HWND hWnd, IStream* pStream) = 0;
  STDMETHOD(CreateControlEx)(LPCOLESTR lpTricsData, HWND hWnd, IStream* pStream, IUnknown** ppUnk,
                             REFIID riidAdvise, IUnknown* punkAdvise) = 0;
  STDMETHOD(AttachControl)(IUnknown* pUnkControl, HWND hWnd) = 0;
  STDMETHOD(QueryControl)(REFIID riid, void** ppvObject) = 0;
  STDMETHOD(SetExternalDispatch)(IDispatch* pDisp) = 0;
  STDMETHOD(SetExternalUIHandler)(IDocHostUIHandlerDispatch* pDisp) = 0;
};
__CRT_UUID_DECL(IAxWinHostWindow, 0xb6ea2050, 0x048a, 0x11d1, 0x82, 0xb9, 0x00, 0xc0, 0x4f, 0xb1,
                0x62, 0x5d)
#define IID_IAxWinHostWindow __uuidof(IAxWinHostWindow)

// The same, for a licensed control.
struct IAxWinHostWindowLic : public IAxWinHostWindow {
  STDMETHOD(CreateControlLic)(LPCOLESTR lpTricsData, HWND hWnd, IStream* pStream, BSTR bstrLic) = 0;
  STDMETHOD(CreateControlLicEx)(LPCOLESTR lpTricsData, HWND hWnd, IStream* pStream,
                                IUnknown** ppUnk, REFIID riidAdvise, IUnknown* punkAdvise,
                                BSTR bstrLic) = 0;
};
__CRT_UUID_DECL(IAxWinHostWindowLic, 0x3935bda8, 0x4ed9, 0x495c, 0x86, 0x50, 0xe0, 0x1f, 0xc1,
                0xe3, 0x8a, 0x4b)
#define IID_IAxWinHostWindowLic __uuidof(IAxWinHostWindowLic)

// The host's ambient properties, which its control reads through the
// client site's IDispatch.
struct IAxWinAmbientDispatch : public IDispatch {
  STDMETHOD(put_AllowWindowlessActivation)(VARIANT_BOOL b) = 0;
  STDMETHOD(get_AllowWindowlessActivation)(VARIANT_BOOL* pb) = 0;
  STDMETHOD(put_BackColor)(OLE_COLOR clr) = 0;
  STDMETHOD(get_BackColor)(OLE_COLOR* pclr) = 0;
  STDMETHOD(put_ForeColor)(OLE_COLOR clr) = 0;
  STDMETHOD(get_ForeColor)(OLE_COLOR* pclr) = 0;
  STDMETHOD(put_LocaleID)(LCID lcid) = 0;
  STDMETHOD(get_LocaleID)(LCID* plcid) = 0;
  STDMETHOD(put_UserMode)(VARIANT_BOOL b) = 0;
  STDMETHOD(get_UserMode)(VARIANT_BOOL* pb) = 0;
  STDMETHOD(put_DisplayAsDefault)(VARIANT_BOOL b) = 0;
  STDMETHOD(get_DisplayAsDefault)(VARIANT_BOOL* pb) = 0;
  STDMETHOD(put_Font)(IFontDisp* pFont) = 0;
  STDMETHOD(get_Font)(IFontDisp** pFont) = 0;
  STDMETHOD(put_MessageReflect)(VARIANT_BOOL b) = 0;
  STDMETHOD(get_MessageReflect)(VARIANT_BOOL* pb) = 0;
  STDMETHOD(get_ShowGrabHandles)(VARIANT_BOOL* pb) = 0;
  STDMETHOD(get_ShowHatching)(VARIANT_BOOL* pb) = 0;
  STDMETHOD(put_DocHostFlags)(DWORD dw) = 0;
  STDMETHOD(get_DocHostFlags)(DWORD* pdw) = 0;
  STDMETHOD(put_DocHostDoubleClickFlags)(DWORD dw) = 0;
  STDMETHOD(get_DocHostDoubleClickFlags)(DWORD* pdw) = 0;
  STDMETHOD(put_AllowContextMenu)(VARIANT_BOOL b) = 0;
  STDMETHOD(get_AllowContextMenu)(VARIANT_BOOL* pb) = 0;
  STDMETHOD(put_AllowShowUI)(VARIANT_BOOL b) = 0;
  STDMETHOD(get_AllowShowUI)(VARIANT_BOOL* pb) = 0;
  STDMETHOD(put_OptionKeyPath)(BSTR bstr) = 0;
  STDMETHOD(get_OptionKeyPath)(BSTR* pbstr) = 0;
};
__CRT_UUID_DECL(IAxWinAmbientDispatch, 0xb6ea2051, 0x048a, 0x11d1, 0x82, 0xb9, 0x00, 0xc0, 0x4f,
                0xb1, 0x62, 0x5d)
#define IID_IAxWinAmbientDispatch __uuidof(IAxWinAmbientDispatch)

// The same, with a dispatch of the caller's that answers first.
struct IAxWinAmbientDispatchEx : public IAxWinAmbientDispatch {
  STDMETHOD(SetAmbientDispatch)(IDispatch* pDisp) = 0;
};
__CRT_UUID_DECL(IAxWinAmbientDispatchEx, 0xb2d0778b, 0xac99, 0x4c58, 0xa5, 0xc8, 0xe7, 0x72, 0x4e,
                0x53, 0x16, 0xb5)
#define IID_IAxWinAmbientDispatchEx __uuidof(IAxWinAmbientDispatchEx)

#endif  // __ATLIFACE_H__
