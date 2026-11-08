// mshtmhst.h - what a program hosting the HTML document gives it:
// IDocHostUIHandler, for its own menus, borders, keys and drop targets.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#ifndef _WINPORT_MSHTMHST_H
#define _WINPORT_MSHTMHST_H

#include <windows.h>
#include <ole2.h>
#include <oleidl.h>
#include <docobj.h>

#define CONTEXT_MENU_DEFAULT 0
#define CONTEXT_MENU_IMAGE 1
#define CONTEXT_MENU_CONTROL 2
#define CONTEXT_MENU_TABLE 3
#define CONTEXT_MENU_TEXTSELECT 4
#define CONTEXT_MENU_ANCHOR 5
#define CONTEXT_MENU_UNKNOWN 6

typedef enum tagDOCHOSTUIDBLCLK {
  DOCHOSTUIDBLCLK_DEFAULT = 0,
  DOCHOSTUIDBLCLK_SHOWPROPERTIES = 1,
  DOCHOSTUIDBLCLK_SHOWCODE = 2
} DOCHOSTUIDBLCLK;

typedef enum tagDOCHOSTUIFLAG {
  DOCHOSTUIFLAG_DIALOG = 0x1,
  DOCHOSTUIFLAG_DISABLE_HELP_MENU = 0x2,
  DOCHOSTUIFLAG_NO3DBORDER = 0x4,
  DOCHOSTUIFLAG_SCROLL_NO = 0x8,
  DOCHOSTUIFLAG_DISABLE_SCRIPT_INACTIVE = 0x10,
  DOCHOSTUIFLAG_OPENNEWWIN = 0x20,
  DOCHOSTUIFLAG_DISABLE_OFFSCREEN = 0x40,
  DOCHOSTUIFLAG_FLAT_SCROLLBAR = 0x80,
  DOCHOSTUIFLAG_DIV_BLOCKDEFAULT = 0x100,
  DOCHOSTUIFLAG_ACTIVATE_CLIENTHIT_ONLY = 0x200,
  DOCHOSTUIFLAG_OVERRIDEBEHAVIORFACTORY = 0x400,
  DOCHOSTUIFLAG_CODEPAGELINKEDFONTS = 0x800,
  DOCHOSTUIFLAG_URL_ENCODING_DISABLE_UTF8 = 0x1000,
  DOCHOSTUIFLAG_URL_ENCODING_ENABLE_UTF8 = 0x2000,
  DOCHOSTUIFLAG_ENABLE_FORMS_AUTOCOMPLETE = 0x4000,
  DOCHOSTUIFLAG_ENABLE_INPLACE_NAVIGATION = 0x10000,
  DOCHOSTUIFLAG_IME_ENABLE_RECONVERSION = 0x20000,
  DOCHOSTUIFLAG_THEME = 0x40000,
  DOCHOSTUIFLAG_NOTHEME = 0x80000,
  DOCHOSTUIFLAG_NOPICS = 0x100000,
  DOCHOSTUIFLAG_NO3DOUTERBORDER = 0x200000
} DOCHOSTUIFLAG;

typedef struct _DOCHOSTUIINFO {
  ULONG cbSize;
  DWORD dwFlags;
  DWORD dwDoubleClick;
  OLECHAR* pchHostCss;
  OLECHAR* pchHostNS;
} DOCHOSTUIINFO;

extern "C" const IID IID_IDocHostUIHandler;

interface IDocHostUIHandler : public IUnknown {
  STDMETHOD(ShowContextMenu)(DWORD dwID, POINT* ppt, IUnknown* pcmdtReserved, IDispatch* pdispReserved) = 0;
  STDMETHOD(GetHostInfo)(DOCHOSTUIINFO* pInfo) = 0;
  STDMETHOD(ShowUI)(DWORD dwID, IOleInPlaceActiveObject* pActiveObject, IOleCommandTarget* pCommandTarget,
                    IOleInPlaceFrame* pFrame, IOleInPlaceUIWindow* pDoc) = 0;
  STDMETHOD(HideUI)() = 0;
  STDMETHOD(UpdateUI)() = 0;
  STDMETHOD(EnableModeless)(BOOL fEnable) = 0;
  STDMETHOD(OnDocWindowActivate)(BOOL fActivate) = 0;
  STDMETHOD(OnFrameWindowActivate)(BOOL fActivate) = 0;
  STDMETHOD(ResizeBorder)(LPCRECT prcBorder, IOleInPlaceUIWindow* pUIWindow, BOOL fRameWindow) = 0;
  STDMETHOD(TranslateAccelerator)(LPMSG lpMsg, const GUID* pguidCmdGroup, DWORD nCmdID) = 0;
  STDMETHOD(GetOptionKeyPath)(LPOLESTR* pchKey, DWORD dw) = 0;
  STDMETHOD(GetDropTarget)(IDropTarget* pDropTarget, IDropTarget** ppDropTarget) = 0;
  STDMETHOD(GetExternal)(IDispatch** ppDispatch) = 0;
  STDMETHOD(TranslateUrl)(DWORD dwTranslate, LPWSTR pchURLIn, LPWSTR* ppchURLOut) = 0;
  STDMETHOD(FilterDataObject)(IDataObject* pDO, IDataObject** ppDORet) = 0;
};

WINPORT_UUIDOF(IDocHostUIHandler)

#endif
