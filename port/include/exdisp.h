// exdisp.h - the WebBrowser control's interface, IWebBrowser2, and its
// event interface. There is no browser control outside Windows yet, so
// creating one fails.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#ifndef _WINPORT_EXDISP_H
#define _WINPORT_EXDISP_H

#include <windows.h>
#include <ole2.h>
#include <oleauto.h>
#include <ocidl.h>
#include <docobj.h>
#include <shtypes.h>

extern "C" const IID IID_IWebBrowser, IID_IWebBrowserApp, IID_IWebBrowser2, DIID_DWebBrowserEvents,
    DIID_DWebBrowserEvents2;
extern "C" const CLSID CLSID_WebBrowser, CLSID_InternetExplorer;

typedef enum BrowserNavConstants {
  navOpenInNewWindow = 0x1,
  navNoHistory = 0x2,
  navNoReadFromCache = 0x4,
  navNoWriteToCache = 0x8,
  navAllowAutosearch = 0x10,
  navBrowserBar = 0x20,
  navHyperlink = 0x40,
  navEnforceRestricted = 0x80
} BrowserNavConstants;

typedef enum RefreshConstants {
  REFRESH_NORMAL = 0,
  REFRESH_IFEXPIRED = 1,
  REFRESH_COMPLETELY = 3
} RefreshConstants;

typedef LONG_PTR SHANDLE_PTR;

interface IWebBrowser : public IDispatch {
  STDMETHOD(GoBack)() = 0;
  STDMETHOD(GoForward)() = 0;
  STDMETHOD(GoHome)() = 0;
  STDMETHOD(GoSearch)() = 0;
  STDMETHOD(Navigate)(BSTR URL, VARIANT* Flags, VARIANT* TargetFrameName, VARIANT* PostData, VARIANT* Headers) = 0;
  STDMETHOD(Refresh)() = 0;
  STDMETHOD(Refresh2)(VARIANT* Level) = 0;
  STDMETHOD(Stop)() = 0;
  STDMETHOD(get_Application)(IDispatch** ppDisp) = 0;
  STDMETHOD(get_Parent)(IDispatch** ppDisp) = 0;
  STDMETHOD(get_Container)(IDispatch** ppDisp) = 0;
  STDMETHOD(get_Document)(IDispatch** ppDisp) = 0;
  STDMETHOD(get_TopLevelContainer)(VARIANT_BOOL* pBool) = 0;
  STDMETHOD(get_Type)(BSTR* Type) = 0;
  STDMETHOD(get_Left)(LONG* pl) = 0;
  STDMETHOD(put_Left)(LONG Left) = 0;
  STDMETHOD(get_Top)(LONG* pl) = 0;
  STDMETHOD(put_Top)(LONG Top) = 0;
  STDMETHOD(get_Width)(LONG* pl) = 0;
  STDMETHOD(put_Width)(LONG Width) = 0;
  STDMETHOD(get_Height)(LONG* pl) = 0;
  STDMETHOD(put_Height)(LONG Height) = 0;
  STDMETHOD(get_LocationName)(BSTR* LocationName) = 0;
  STDMETHOD(get_LocationURL)(BSTR* LocationURL) = 0;
  STDMETHOD(get_Busy)(VARIANT_BOOL* pBool) = 0;
};

interface IWebBrowserApp : public IWebBrowser {
  STDMETHOD(Quit)() = 0;
  STDMETHOD(ClientToWindow)(int* pcx, int* pcy) = 0;
  STDMETHOD(PutProperty)(BSTR Property, VARIANT vtValue) = 0;
  STDMETHOD(GetProperty)(BSTR Property, VARIANT* pvtValue) = 0;
  STDMETHOD(get_Name)(BSTR* Name) = 0;
  STDMETHOD(get_HWND)(SHANDLE_PTR* pHWND) = 0;
  STDMETHOD(get_FullName)(BSTR* FullName) = 0;
  STDMETHOD(get_Path)(BSTR* Path) = 0;
  STDMETHOD(get_Visible)(VARIANT_BOOL* pBool) = 0;
  STDMETHOD(put_Visible)(VARIANT_BOOL Value) = 0;
  STDMETHOD(get_StatusBar)(VARIANT_BOOL* pBool) = 0;
  STDMETHOD(put_StatusBar)(VARIANT_BOOL Value) = 0;
  STDMETHOD(get_StatusText)(BSTR* StatusText) = 0;
  STDMETHOD(put_StatusText)(BSTR StatusText) = 0;
  STDMETHOD(get_ToolBar)(int* Value) = 0;
  STDMETHOD(put_ToolBar)(int Value) = 0;
  STDMETHOD(get_MenuBar)(VARIANT_BOOL* Value) = 0;
  STDMETHOD(put_MenuBar)(VARIANT_BOOL Value) = 0;
  STDMETHOD(get_FullScreen)(VARIANT_BOOL* pbFullScreen) = 0;
  STDMETHOD(put_FullScreen)(VARIANT_BOOL bFullScreen) = 0;
};

interface IWebBrowser2 : public IWebBrowserApp {
  STDMETHOD(Navigate2)(VARIANT* URL, VARIANT* Flags, VARIANT* TargetFrameName, VARIANT* PostData,
                       VARIANT* Headers) = 0;
  STDMETHOD(QueryStatusWB)(OLECMDID cmdID, OLECMDF* pcmdf) = 0;
  STDMETHOD(ExecWB)(OLECMDID cmdID, OLECMDEXECOPT cmdexecopt, VARIANT* pvaIn, VARIANT* pvaOut) = 0;
  STDMETHOD(ShowBrowserBar)(VARIANT* pvaClsid, VARIANT* pvarShow, VARIANT* pvarSize) = 0;
  STDMETHOD(get_ReadyState)(READYSTATE* plReadyState) = 0;
  STDMETHOD(get_Offline)(VARIANT_BOOL* pbOffline) = 0;
  STDMETHOD(put_Offline)(VARIANT_BOOL bOffline) = 0;
  STDMETHOD(get_Silent)(VARIANT_BOOL* pbSilent) = 0;
  STDMETHOD(put_Silent)(VARIANT_BOOL bSilent) = 0;
  STDMETHOD(get_RegisterAsBrowser)(VARIANT_BOOL* pbRegister) = 0;
  STDMETHOD(put_RegisterAsBrowser)(VARIANT_BOOL bRegister) = 0;
  STDMETHOD(get_RegisterAsDropTarget)(VARIANT_BOOL* pbRegister) = 0;
  STDMETHOD(put_RegisterAsDropTarget)(VARIANT_BOOL bRegister) = 0;
  STDMETHOD(get_TheaterMode)(VARIANT_BOOL* pbRegister) = 0;
  STDMETHOD(put_TheaterMode)(VARIANT_BOOL bRegister) = 0;
  STDMETHOD(get_AddressBar)(VARIANT_BOOL* Value) = 0;
  STDMETHOD(put_AddressBar)(VARIANT_BOOL Value) = 0;
  STDMETHOD(get_Resizable)(VARIANT_BOOL* Value) = 0;
  STDMETHOD(put_Resizable)(VARIANT_BOOL Value) = 0;
};

// The events are a dispinterface: they arrive through IDispatch::Invoke.
typedef IDispatch DWebBrowserEvents2;

WINPORT_UUIDOF(IWebBrowser2)

#endif
