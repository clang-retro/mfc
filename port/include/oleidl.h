// oleidl.h - OLE documents: windows, in-place activation, objects and
// their sites, drag and drop, caches and views.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
//
// The interfaces follow with the OLE part of the port; until then this
// header declares their names, so that code passing them around builds.
#ifndef _WINPORT_OLEIDL_H
#define _WINPORT_OLEIDL_H

#include <objidl.h>

interface IOleWindow;
interface IOleInPlaceObject;
interface IOleInPlaceActiveObject;
interface IOleInPlaceUIWindow;
interface IOleInPlaceFrame;
interface IOleInPlaceSite;
interface IOleObject;
interface IOleClientSite;
interface IOleContainer;
interface IOleItemContainer;
interface IOleLink;
interface IOleAdviseHolder;
interface IOleCache;
interface IOleCache2;
interface IOleCacheControl;
interface IViewObject;
interface IViewObject2;
interface IEnumOLEVERB;
interface IRunnableObject;
interface IParseDisplayName;
interface IDropTarget;
interface IDropSource;
typedef IOleObject* LPOLEOBJECT;
typedef IOleClientSite* LPOLECLIENTSITE;
typedef IDropTarget* LPDROPTARGET;
typedef IDropSource* LPDROPSOURCE;
typedef IOleInPlaceFrame* LPOLEINPLACEFRAME;
typedef IOleInPlaceUIWindow* LPOLEINPLACEUIWINDOW;
typedef IOleInPlaceActiveObject* LPOLEINPLACEACTIVEOBJECT;
typedef IOleInPlaceObject* LPOLEINPLACEOBJECT;
typedef IOleInPlaceSite* LPOLEINPLACESITE;
typedef IOleWindow* LPOLEWINDOW;
typedef IOleContainer* LPOLECONTAINER;
typedef IOleItemContainer* LPOLEITEMCONTAINER;
typedef IOleLink* LPOLELINK;
typedef IOleAdviseHolder* LPOLEADVISEHOLDER;
typedef IOleCache* LPOLECACHE;
typedef IViewObject* LPVIEWOBJECT;
typedef IViewObject2* LPVIEWOBJECT2;
typedef IEnumOLEVERB* LPENUMOLEVERB;
typedef IRunnableObject* LPRUNNABLEOBJECT;

typedef struct tagOleMenuGroupWidths {
  LONG width[6];
} OLEMENUGROUPWIDTHS, *LPOLEMENUGROUPWIDTHS;
typedef HGLOBAL HOLEMENU;
typedef struct tagOIFI {
  UINT cb;
  BOOL fMDIApp;
  HWND hwndFrame;
  HACCEL haccel;
  UINT cAccelEntries;
} OLEINPLACEFRAMEINFO, *LPOLEINPLACEFRAMEINFO;
typedef RECT BORDERWIDTHS;
typedef LPRECT LPBORDERWIDTHS;
typedef LPCRECT LPCBORDERWIDTHS;
typedef struct tagOLEVERB {
  LONG lVerb;
  LPOLESTR lpszVerbName;
  DWORD fuFlags;
  DWORD grfAttribs;
} OLEVERB, *LPOLEVERB;

#endif
