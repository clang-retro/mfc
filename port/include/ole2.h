// ole2.h - OLE: COM, automation and OLE documents; the OLE library
// functions for creating, loading, saving and running objects, the
// clipboard, drag and drop, and in-place menus.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#ifndef _WINPORT_OLE2_H
#define _WINPORT_OLE2_H

#include <objbase.h>
#include <oleauto.h>
#include <oleidl.h>

extern "C" {

#define E_DRAW VIEW_E_DRAW
#define DATA_E_FORMATETC DV_E_FORMATETC
#define OLEIVERB_PROPERTIES (-7L)

typedef struct tagOleStream* LPOLESTREAM;

HRESULT WINAPI OleInitialize(LPVOID pvReserved);
void WINAPI OleUninitialize(void);
DWORD WINAPI OleBuildVersion(void);

HRESULT WINAPI ReadClassStg(LPSTORAGE pStg, CLSID* pclsid);
HRESULT WINAPI WriteClassStg(LPSTORAGE pStg, REFCLSID rclsid);
HRESULT WINAPI ReadClassStm(LPSTREAM pStm, CLSID* pclsid);
HRESULT WINAPI WriteClassStm(LPSTREAM pStm, REFCLSID rclsid);
HRESULT WINAPI WriteFmtUserTypeStg(LPSTORAGE pstg, CLIPFORMAT cf, LPOLESTR lpszUserType);
HRESULT WINAPI ReadFmtUserTypeStg(LPSTORAGE pstg, CLIPFORMAT* pcf, LPOLESTR* lplpszUserType);

HRESULT WINAPI OleQueryLinkFromData(LPDATAOBJECT pSrcDataObject);
HRESULT WINAPI OleQueryCreateFromData(LPDATAOBJECT pSrcDataObject);
HRESULT WINAPI OleCreate(REFCLSID rclsid, REFIID riid, DWORD renderopt, LPFORMATETC pFormatEtc,
                         LPOLECLIENTSITE pClientSite, LPSTORAGE pStg, LPVOID* ppvObj);
HRESULT WINAPI OleCreateEx(REFCLSID rclsid, REFIID riid, DWORD dwFlags, DWORD renderopt, ULONG cFormats,
                           DWORD* rgAdvf, LPFORMATETC rgFormatEtc, IAdviseSink* lpAdviseSink,
                           DWORD* rgdwConnection, LPOLECLIENTSITE pClientSite, LPSTORAGE pStg, LPVOID* ppvObj);
HRESULT WINAPI OleCreateFromData(LPDATAOBJECT pSrcDataObj, REFIID riid, DWORD renderopt, LPFORMATETC pFormatEtc,
                                 LPOLECLIENTSITE pClientSite, LPSTORAGE pStg, LPVOID* ppvObj);
HRESULT WINAPI OleCreateLinkFromData(LPDATAOBJECT pSrcDataObj, REFIID riid, DWORD renderopt,
                                     LPFORMATETC pFormatEtc, LPOLECLIENTSITE pClientSite, LPSTORAGE pStg,
                                     LPVOID* ppvObj);
HRESULT WINAPI OleCreateStaticFromData(LPDATAOBJECT pSrcDataObj, REFIID iid, DWORD renderopt,
                                       LPFORMATETC pFormatEtc, LPOLECLIENTSITE pClientSite, LPSTORAGE pStg,
                                       LPVOID* ppvObj);
HRESULT WINAPI OleCreateLink(LPMONIKER pmkLinkSrc, REFIID riid, DWORD renderopt, LPFORMATETC lpFormatEtc,
                             LPOLECLIENTSITE pClientSite, LPSTORAGE pStg, LPVOID* ppvObj);
HRESULT WINAPI OleCreateLinkToFile(LPCOLESTR lpszFileName, REFIID riid, DWORD renderopt, LPFORMATETC lpFormatEtc,
                                   LPOLECLIENTSITE pClientSite, LPSTORAGE pStg, LPVOID* ppvObj);
HRESULT WINAPI OleCreateFromFile(REFCLSID rclsid, LPCOLESTR lpszFileName, REFIID riid, DWORD renderopt,
                                 LPFORMATETC lpFormatEtc, LPOLECLIENTSITE pClientSite, LPSTORAGE pStg,
                                 LPVOID* ppvObj);
HRESULT WINAPI OleLoad(LPSTORAGE pStg, REFIID riid, LPOLECLIENTSITE pClientSite, LPVOID* ppvObj);
HRESULT WINAPI OleSave(LPPERSISTSTORAGE pPS, LPSTORAGE pStg, BOOL fSameAsLoad);
HRESULT WINAPI OleLoadFromStream(LPSTREAM pStm, REFIID iidInterface, LPVOID* ppvObj);
HRESULT WINAPI OleSaveToStream(LPPERSISTSTREAM pPStm, LPSTREAM pStm);
HRESULT WINAPI OleSetContainedObject(LPUNKNOWN pUnknown, BOOL fContained);
HRESULT WINAPI OleNoteObjectVisible(LPUNKNOWN pUnknown, BOOL fVisible);

HRESULT WINAPI RegisterDragDrop(HWND hwnd, LPDROPTARGET pDropTarget);
HRESULT WINAPI RevokeDragDrop(HWND hwnd);
HRESULT WINAPI DoDragDrop(LPDATAOBJECT pDataObj, LPDROPSOURCE pDropSource, DWORD dwOKEffects, LPDWORD pdwEffect);

HRESULT WINAPI OleSetClipboard(LPDATAOBJECT pDataObj);
HRESULT WINAPI OleGetClipboard(LPDATAOBJECT* ppDataObj);
HRESULT WINAPI OleFlushClipboard(void);
HRESULT WINAPI OleIsCurrentClipboard(LPDATAOBJECT pDataObj);

HOLEMENU WINAPI OleCreateMenuDescriptor(HMENU hmenuCombined, LPOLEMENUGROUPWIDTHS lpMenuWidths);
HRESULT WINAPI OleSetMenuDescriptor(HOLEMENU holemenu, HWND hwndFrame, HWND hwndActiveObject,
                                    LPOLEINPLACEFRAME lpFrame, LPOLEINPLACEACTIVEOBJECT lpActiveObj);
HRESULT WINAPI OleDestroyMenuDescriptor(HOLEMENU holemenu);
HRESULT WINAPI OleTranslateAccelerator(LPOLEINPLACEFRAME lpFrame, LPOLEINPLACEFRAMEINFO lpFrameInfo, LPMSG lpmsg);
BOOL WINAPI IsAccelerator(HACCEL hAccel, int cAccelEntries, LPMSG lpMsg, WORD* lpwCmd);

HANDLE WINAPI OleDuplicateData(HANDLE hSrc, CLIPFORMAT cfFormat, UINT uiFlags);
HRESULT WINAPI OleDraw(LPUNKNOWN pUnknown, DWORD dwAspect, HDC hdcDraw, LPCRECT lprcBounds);
HRESULT WINAPI OleRun(LPUNKNOWN pUnknown);
BOOL WINAPI OleIsRunning(LPOLEOBJECT pObject);
HRESULT WINAPI OleLockRunning(LPUNKNOWN pUnknown, BOOL fLock, BOOL fLastUnlockCloses);
void WINAPI ReleaseStgMedium(LPSTGMEDIUM);
HRESULT WINAPI CreateOleAdviseHolder(LPOLEADVISEHOLDER* ppOAHolder);
HRESULT WINAPI CreateDataAdviseHolder(LPDATAADVISEHOLDER* ppDAHolder);
HRESULT WINAPI CreateDataCache(LPUNKNOWN pUnkOuter, REFCLSID rclsid, REFIID iid, LPVOID* ppv);
HRESULT WINAPI OleCreateDefaultHandler(REFCLSID clsid, LPUNKNOWN pUnkOuter, REFIID riid, LPVOID* lplpObj);
HRESULT WINAPI OleCreateEmbeddingHelper(REFCLSID clsid, LPUNKNOWN pUnkOuter, DWORD flags, LPCLASSFACTORY pCF,
                                        REFIID riid, LPVOID* lplpObj);

HRESULT WINAPI OleRegGetUserType(REFCLSID clsid, DWORD dwFormOfType, LPOLESTR* pszUserType);
HRESULT WINAPI OleRegGetMiscStatus(REFCLSID clsid, DWORD dwAspect, DWORD* pdwStatus);
HRESULT WINAPI OleRegEnumFormatEtc(REFCLSID clsid, DWORD dwDirection, LPENUMFORMATETC* ppenum);
HRESULT WINAPI OleRegEnumVerbs(REFCLSID clsid, LPENUMOLEVERB* ppenum);
HRESULT WINAPI OleDoAutoConvert(LPSTORAGE pStg, LPCLSID pClsidNew);
HRESULT WINAPI OleGetAutoConvert(REFCLSID clsidOld, LPCLSID pClsidNew);
HRESULT WINAPI OleSetAutoConvert(REFCLSID clsidOld, REFCLSID clsidNew);
HRESULT WINAPI GetConvertStg(LPSTORAGE pStg);
HRESULT WINAPI SetConvertStg(LPSTORAGE pStg, BOOL fConvert);

HGLOBAL WINAPI OleGetIconOfFile(LPOLESTR lpszPath, BOOL fUseFileAsLabel);
HGLOBAL WINAPI OleGetIconOfClass(REFCLSID rclsid, LPOLESTR lpszLabel, BOOL fUseTypeAsLabel);
HGLOBAL WINAPI OleMetafilePictFromIconAndLabel(HICON hIcon, LPOLESTR lpszLabel, LPOLESTR lpszSourceFile,
                                               UINT iIconIndex);

}  // extern "C"

#endif
