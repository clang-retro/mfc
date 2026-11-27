// atlsnap.h - MMC snap-ins with ATL: the nodes (CSnapInItemImpl), the
// data objects MMC passes about, the component data and components, the
// context menus, property pages and toolbars of the nodes, and extensions
// of other snap-ins' nodes.
//
//   class CMyNode : public CSnapInItemImpl<CMyNode> {
//     static const GUID* m_NODETYPE;           // and m_SZNODETYPE,
//     ...                                      // m_SZDISPLAY_NAME,
//     SNAPINMENUID(IDR_MYMENU)                 // m_SNAPIN_CLASSID
//     BEGIN_SNAPINTOOLBARID_MAP(CMyNode)
//       SNAPINTOOLBARID_ENTRY(IDR_MYTOOLBAR)
//     END_SNAPINTOOLBARID_MAP()
//     BEGIN_SNAPINCOMMAND_MAP(CMyNode, FALSE)
//       SNAPINCOMMAND_ENTRY(ID_MYCOMMAND, OnMyCommand)
//     END_SNAPINCOMMAND_MAP()
//     STDMETHOD(Notify)(MMC_NOTIFY_TYPE, LPARAM, LPARAM, IComponentData*,
//                       IComponent*, DATA_OBJECT_TYPES);
//   };
//   class CMySnapin : public CComObjectRootEx<...>,
//                     public CSnapInObjectRoot<1, CMySnapin>,
//                     public IComponentDataImpl<CMySnapin, CMyComponent>, ...
//   class CMyComponent : public CComObjectRootEx<...>,
//                        public CSnapInObjectRoot<2, CMySnapin>,
//                        public IComponentImpl<CMyComponent>, ...
//
// A menu resource has up to four popups, for MMC's top, new, task and view
// insertion points; each item's text is "name\nstatus bar text". A toolbar
// resource and its bitmap share an ID; each button's string is "text\ntip".
//
// The IIDs of the MMC interfaces come from the SDK's MMC library (mmc.lib),
// as with Visual C++; with MinGW's headers, __uuidof of each gives it.
//
// Part of clang-retro MFC, an independent implementation of the MFC API.
// SPDX-License-Identifier: AGPL-3.0-only
#ifndef __ATLSNAP_H__
#define __ATLSNAP_H__

#include <atlbase.h>
#include <atlcom.h>
#include <atlwin.h>
#include <atlsimpcoll.h>
#include <atlstr.h>
#include <commctrl.h>
#include <mmc.h>

#ifndef RT_TOOLBAR
#define RT_TOOLBAR MAKEINTRESOURCE(241)
#endif

#if !defined(_ATL_NO_DEFAULT_LIBS) && defined(_WIN32)
__asm__(".section .drectve,\"yn\"\n"
        ".ascii \" -defaultlib:ole32 -defaultlib:comctl32 -defaultlib:uuid\"\n"
        ".text");
#endif

// MinGW's mmc.h declares the interfaces' IIDs but not __uuidof, which COM
// maps of snap-ins use: __uuidof gives the IID the MMC library defines.
#if defined(__MINGW32__) && defined(__cplusplus) && defined(USE___UUIDOF) && (USE___UUIDOF == 0) && \
    !defined(_ATL_NO_SNAPIN_UUIDOF)
#define _ATL_SNAPIN_UUIDOF(i)                                                   \
  extern "C++" {                                                                \
  template <> inline const GUID& __mingw_uuidof<i>() { return IID_##i; }        \
  template <> inline const GUID& __mingw_uuidof<i*>() { return IID_##i; }       \
  }
_ATL_SNAPIN_UUIDOF(IComponentData)
_ATL_SNAPIN_UUIDOF(IComponent)
_ATL_SNAPIN_UUIDOF(IConsole)
_ATL_SNAPIN_UUIDOF(IConsole2)
_ATL_SNAPIN_UUIDOF(IConsoleNameSpace)
_ATL_SNAPIN_UUIDOF(IConsoleNameSpace2)
_ATL_SNAPIN_UUIDOF(IConsoleVerb)
_ATL_SNAPIN_UUIDOF(IContextMenuCallback)
_ATL_SNAPIN_UUIDOF(IContextMenuProvider)
_ATL_SNAPIN_UUIDOF(IControlbar)
_ATL_SNAPIN_UUIDOF(IExtendContextMenu)
_ATL_SNAPIN_UUIDOF(IExtendControlbar)
_ATL_SNAPIN_UUIDOF(IExtendPropertySheet)
_ATL_SNAPIN_UUIDOF(IExtendPropertySheet2)
_ATL_SNAPIN_UUIDOF(IHeaderCtrl)
_ATL_SNAPIN_UUIDOF(IImageList)
_ATL_SNAPIN_UUIDOF(IPropertySheetCallback)
_ATL_SNAPIN_UUIDOF(IPropertySheetProvider)
_ATL_SNAPIN_UUIDOF(IResultData)
_ATL_SNAPIN_UUIDOF(IResultDataCompare)
_ATL_SNAPIN_UUIDOF(ISnapinAbout)
_ATL_SNAPIN_UUIDOF(ISnapinHelp)
_ATL_SNAPIN_UUIDOF(IToolbar)
#undef _ATL_SNAPIN_UUIDOF
#endif

namespace ATL {

class CSnapInObjectRootBase;

// What CCF_SNAPIN_GETOBJECTDATA gives: the node behind a data object.
struct CObjectData {
  class CSnapInItem* m_pItem;
  DATA_OBJECT_TYPES m_type;
};

class ATL_NO_VTABLE CSnapInItem {
public:
  virtual ~CSnapInItem() {}

  STDMETHOD(Notify)(MMC_NOTIFY_TYPE event, LPARAM arg, LPARAM param, IComponentData* pComponentData,
                    IComponent* pComponent, DATA_OBJECT_TYPES type) = 0;
  STDMETHOD(GetScopePaneInfo)(SCOPEDATAITEM* pScopeDataItem) = 0;
  STDMETHOD(GetResultViewType)(LPOLESTR* ppViewType, long* pViewOptions) = 0;
  STDMETHOD(GetResultPaneInfo)(RESULTDATAITEM* pResultDataItem) = 0;
  STDMETHOD(AddMenuItems)(LPCONTEXTMENUCALLBACK piCallback, long* pInsertionAllowed,
                          DATA_OBJECT_TYPES type) = 0;
  STDMETHOD(Command)(long lCommandID, CSnapInObjectRootBase* pObj, DATA_OBJECT_TYPES type) = 0;
  STDMETHOD(CreatePropertyPages)(LPPROPERTYSHEETCALLBACK lpProvider, LONG_PTR handle, IUnknown* pUnk,
                                 DATA_OBJECT_TYPES type) = 0;
  STDMETHOD(QueryPagesFor)(DATA_OBJECT_TYPES type) = 0;
  STDMETHOD(SetControlbar)(IControlbar* pControlbar, IExtendControlbar* pExtendControlbar,
                           CSimpleMap<UINT, IUnknown*>* pToolbarMap) = 0;
  STDMETHOD(ControlbarNotify)(IControlbar* pControlbar, IExtendControlbar* pExtendControlbar,
                              CSimpleMap<UINT, IUnknown*>* pToolbarMap, MMC_NOTIFY_TYPE event, LPARAM arg,
                              LPARAM param, CSnapInObjectRootBase* pObj, DATA_OBJECT_TYPES type) = 0;
  STDMETHOD(GetScopeData)(SCOPEDATAITEM** pScopeDataItem) = 0;
  STDMETHOD(GetResultData)(RESULTDATAITEM** pResultDataItem) = 0;
  STDMETHOD(FillData)(CLIPFORMAT cf, LPSTREAM pStream) = 0;
  virtual HRESULT STDMETHODCALLTYPE GetDataObject(IDataObject** pDataObj, DATA_OBJECT_TYPES type) = 0;
  // An extension's node, given the data object of the node it extends.
  virtual void InitDataClass(IDataObject* /*pDataObject*/, CSnapInItem* /*pDefault*/) {}

  // The clipboard formats MMC asks data objects for.
  static inline CLIPFORMAT m_CCF_NODETYPE = 0;
  static inline CLIPFORMAT m_CCF_SZNODETYPE = 0;
  static inline CLIPFORMAT m_CCF_DISPLAY_NAME = 0;
  static inline CLIPFORMAT m_CCF_SNAPIN_CLASSID = 0;
  static inline CLIPFORMAT m_CCF_SNAPIN_GETOBJECTDATA = 0;
  static inline CLIPFORMAT m_CCF_MMC_MULTISELECT_DATAOBJECT = 0;

  static void Init() {
    if (m_CCF_NODETYPE != 0) return;
    m_CCF_NODETYPE = (CLIPFORMAT)::RegisterClipboardFormatW(L"CCF_NODETYPE");
    m_CCF_SZNODETYPE = (CLIPFORMAT)::RegisterClipboardFormatW(L"CCF_SZNODETYPE");
    m_CCF_DISPLAY_NAME = (CLIPFORMAT)::RegisterClipboardFormatW(L"CCF_DISPLAY_NAME");
    m_CCF_SNAPIN_CLASSID = (CLIPFORMAT)::RegisterClipboardFormatW(L"CCF_SNAPIN_CLASSID");
    m_CCF_SNAPIN_GETOBJECTDATA = (CLIPFORMAT)::RegisterClipboardFormatW(L"CCF_GETOBJECTDATA");
    m_CCF_MMC_MULTISELECT_DATAOBJECT = (CLIPFORMAT)::RegisterClipboardFormatW(L"CCF_MMC_MULTISELECT_DATAOBJECT");
  }

  // The node and the kind of view behind a data object of a snap-in made
  // with these classes.
  static HRESULT GetDataClass(IDataObject* pDataObject, CSnapInItem** ppItem, DATA_OBJECT_TYPES* pType) {
    if (ppItem == NULL) return E_POINTER;
    *ppItem = NULL;
    if (pDataObject == NULL) return E_POINTER;
    Init();
    STGMEDIUM stgmedium = {TYMED_HGLOBAL, {NULL}, NULL};
    FORMATETC formatetc = {m_CCF_SNAPIN_GETOBJECTDATA, NULL, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
    stgmedium.hGlobal = ::GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, sizeof(CObjectData));
    if (stgmedium.hGlobal == NULL) return E_OUTOFMEMORY;
    HRESULT hr = pDataObject->GetDataHere(&formatetc, &stgmedium);
    if (SUCCEEDED(hr)) {
      CObjectData* p = (CObjectData*)::GlobalLock(stgmedium.hGlobal);
      if (p != NULL) {
        *ppItem = p->m_pItem;
        if (pType != NULL) *pType = p->m_type;
        ::GlobalUnlock(stgmedium.hGlobal);
      } else {
        hr = E_UNEXPECTED;
      }
    }
    ::GlobalFree(stgmedium.hGlobal);
    return hr;
  }
};

// The data object of a node: the node and its view for the snap-in, and
// the clipboard formats MMC reads, which FillData writes.
class ATL_NO_VTABLE CSnapInDataObjectImpl : public CComObjectRootEx<CComSingleThreadModel>,
                                            public IDataObject {
public:
  BEGIN_COM_MAP(CSnapInDataObjectImpl)
    COM_INTERFACE_ENTRY(IDataObject)
  END_COM_MAP()

  CObjectData m_objectData;

  STDMETHOD(GetData)(FORMATETC* /*pformatetcIn*/, STGMEDIUM* /*pmedium*/) override { return E_NOTIMPL; }
  STDMETHOD(GetDataHere)(FORMATETC* pformatetc, STGMEDIUM* pmedium) override {
    if (pformatetc == NULL || pmedium == NULL) return E_POINTER;
    if (!(pformatetc->tymed & TYMED_HGLOBAL) || pmedium->hGlobal == NULL) return DV_E_TYMED;
    CSnapInItem::Init();
    CComPtr<IStream> spStream;
    HRESULT hr = ::CreateStreamOnHGlobal(pmedium->hGlobal, FALSE, &spStream);
    if (FAILED(hr)) return hr;
    pmedium->tymed = TYMED_HGLOBAL;
    pmedium->pUnkForRelease = NULL;
    if (pformatetc->cfFormat == CSnapInItem::m_CCF_SNAPIN_GETOBJECTDATA)
      return spStream->Write(&m_objectData, sizeof(CObjectData), NULL);
    if (m_objectData.m_pItem == NULL) return E_UNEXPECTED;
    return m_objectData.m_pItem->FillData(pformatetc->cfFormat, spStream);
  }
  STDMETHOD(QueryGetData)(FORMATETC* /*pformatetc*/) override { return E_NOTIMPL; }
  STDMETHOD(GetCanonicalFormatEtc)(FORMATETC* /*pformatectIn*/, FORMATETC* /*pformatetcOut*/) override {
    return E_NOTIMPL;
  }
  STDMETHOD(SetData)(FORMATETC* /*pformatetc*/, STGMEDIUM* /*pmedium*/, BOOL /*fRelease*/) override {
    return E_NOTIMPL;
  }
  STDMETHOD(EnumFormatEtc)(DWORD /*dwDirection*/, IEnumFORMATETC** /*ppenumFormatEtc*/) override {
    return E_NOTIMPL;
  }
  STDMETHOD(DAdvise)(FORMATETC*, DWORD, IAdviseSink*, DWORD*) override { return OLE_E_ADVISENOTSUPPORTED; }
  STDMETHOD(DUnadvise)(DWORD) override { return OLE_E_ADVISENOTSUPPORTED; }
  STDMETHOD(EnumDAdvise)(IEnumSTATDATA**) override { return OLE_E_ADVISENOTSUPPORTED; }
};

// What a component data (m_nType 1) and a component (2) share: the
// control bar and its toolbars, made once and kept by resource ID.
class CSnapInObjectRootBase {
public:
  CComPtr<IControlbar> m_spControlbar;
  CSimpleMap<UINT, IUnknown*> m_toolbarMap;
  const int m_nType;

  explicit CSnapInObjectRootBase(int n = 0) : m_nType(n) {}
  virtual ~CSnapInObjectRootBase() { ReleaseToolbars(); }

  void ReleaseToolbars() {
    for (int i = 0; i < m_toolbarMap.GetSize(); i++) {
      IUnknown* p = m_toolbarMap.GetValueAt(i);
      if (p != NULL) p->Release();
    }
    m_toolbarMap.RemoveAll();
  }
  HRESULT GetDataClass(IDataObject* pDataObject, CSnapInItem** ppItem, DATA_OBJECT_TYPES* pType) {
    return CSnapInItem::GetDataClass(pDataObject, ppItem, pType);
  }
};

template <int n, class ComponentData>
class CSnapInObjectRoot : public CSnapInObjectRootBase {
public:
  CSnapInObjectRoot() : CSnapInObjectRootBase(n), m_pComponentData(NULL) {}
  ComponentData* m_pComponentData;

  // A component finds the nodes as its component data does (which may be
  // an extension's).
  HRESULT GetDataClass(IDataObject* pDataObject, CSnapInItem** ppItem, DATA_OBJECT_TYPES* pType) {
    if (n == 2 && m_pComponentData != NULL)
      return m_pComponentData->GetDataClass(pDataObject, ppItem, pType);
    return CSnapInItem::GetDataClass(pDataObject, ppItem, pType);
  }
};

// A toolbar of a node: its resource ID, then (once made) its buttons'
// commands and strings.
struct CSnapInToolbarInfo {
  void CleanUp() {
    for (UINT i = 0; i < m_nButtonCount; i++) {
      if (m_pStrToolTip != NULL) delete[] m_pStrToolTip[i];
      if (m_pStrButtonText != NULL) delete[] m_pStrButtonText[i];
    }
    delete[] m_pStrToolTip;
    delete[] m_pStrButtonText;
    delete[] m_pnButtonID;
    m_pStrToolTip = m_pStrButtonText = NULL;
    m_pnButtonID = NULL;
    m_nButtonCount = 0;
  }
  OLECHAR** m_pStrToolTip;
  OLECHAR** m_pStrButtonText;
  UINT* m_pnButtonID;
  UINT m_idToolbar;
  UINT m_nButtonCount;
};

#define SNAPINMENUID(id) \
 public:                 \
  static UINT GetMenuID() { return id; }

#define BEGIN_SNAPINTOOLBARID_MAP(theClass)    \
 public:                                       \
  static ATL::CSnapInToolbarInfo* GetToolbarInfo() { \
    static ATL::CSnapInToolbarInfo m_toolbarInfo[] = {
#define SNAPINTOOLBARID_ENTRY(id) {NULL, NULL, NULL, id, 0},
#define END_SNAPINTOOLBARID_MAP() \
  {NULL, NULL, NULL, 0, 0}        \
  }                               \
  ;                               \
  return m_toolbarInfo;           \
  }

// Commands: a handler is HRESULT f(bool& bHandled, CSnapInObjectRootBase*),
// or for a range HRESULT f(UINT nID, bool& bHandled, CSnapInObjectRootBase*).
#define BEGIN_SNAPINCOMMAND_MAP(theClass, bIsExtension)                                  \
  HRESULT ProcessCommand(UINT nID, bool& bHandled, ATL::CSnapInObjectRootBase* pObj, \
                         DATA_OBJECT_TYPES type) {                                       \
    (void)pObj;                                                                          \
    (void)type;                                                                          \
    (void)nID;                                                                           \
    HRESULT hr = S_OK;                                                                   \
    bHandled = true;
#define SNAPINCOMMAND_ENTRY(id, func) \
  if ((UINT)(id) == nID) {            \
    hr = func(bHandled, pObj);        \
    if (bHandled) return hr;          \
    bHandled = true;                  \
  }
#define SNAPINCOMMAND_RANGE_ENTRY(id1, id2, func)        \
  if ((UINT)(id1) <= nID && nID <= (UINT)(id2)) {        \
    hr = func(nID, bHandled, pObj);                      \
    if (bHandled) return hr;                             \
    bHandled = true;                                     \
  }
#define CHAIN_SNAPINCOMMAND_MAP(theChainClass)                       \
  {                                                                  \
    hr = theChainClass::ProcessCommand(nID, bHandled, pObj, type);   \
    if (bHandled) return hr;                                         \
    bHandled = true;                                                 \
  }
#define END_SNAPINCOMMAND_MAP() \
  bHandled = false;             \
  return hr;                    \
  }

// ---------------------------------------------------------------------
// Nodes.

template <class T, BOOL bIsExtension = FALSE>
class ATL_NO_VTABLE CSnapInItemImpl : public CSnapInItem {
public:
  SCOPEDATAITEM m_scopeDataItem;
  RESULTDATAITEM m_resultDataItem;
  CComBSTR m_bstrDisplayName;
  // For an extension, the data object of the node it extends.
  IDataObject* m_pDataObject;

  CSnapInItemImpl() : m_pDataObject(NULL) {
    memset(&m_scopeDataItem, 0, sizeof(m_scopeDataItem));
    memset(&m_resultDataItem, 0, sizeof(m_resultDataItem));
  }

  // What a node without these has: no menu, toolbars or commands.
  static UINT GetMenuID() { return 0; }
  static CSnapInToolbarInfo* GetToolbarInfo() { return NULL; }
  HRESULT ProcessCommand(UINT, bool& bHandled, CSnapInObjectRootBase*, DATA_OBJECT_TYPES) {
    bHandled = false;
    return S_OK;
  }
  void SetMenuInsertionFlags(bool /*bBeforeInsertion*/, long* /*pInsertionAllowed*/) {}
  void UpdateMenuState(UINT /*id*/, LPTSTR /*pBuf*/, UINT* /*flags*/) {}
  void SetToolbarButtonInfo(UINT /*id*/, BYTE* /*pfsState*/, BYTE* /*pfsType*/) {}
  BOOL UpdateToolbarButton(UINT /*id*/, BYTE fsState) { return fsState == ENABLED; }

  void* GetNodeType() { return (void*)T::m_NODETYPE; }
  void* GetSZNodeType() { return (void*)T::m_SZNODETYPE; }
  void* GetDisplayName() { return (void*)T::m_SZDISPLAY_NAME; }
  void* GetSnapInCLSID() { return (void*)T::m_SNAPIN_CLASSID; }
  // An extension's node for a data object: the one in the map, unless T
  // has others.
  CSnapInItem* GetExtNodeObject(IDataObject* /*pDataObject*/, CSnapInItem* pDefault) { return pDefault; }
  void InitDataClass(IDataObject* pDataObject, CSnapInItem* /*pDefault*/) override { m_pDataObject = pDataObject; }

  STDMETHOD(Notify)(MMC_NOTIFY_TYPE, LPARAM, LPARAM, IComponentData*, IComponent*, DATA_OBJECT_TYPES) override {
    return E_NOTIMPL;
  }
  STDMETHOD(GetScopePaneInfo)(SCOPEDATAITEM* pScopeDataItem) override {
    if (pScopeDataItem == NULL) return E_POINTER;
    UINT mask = pScopeDataItem->mask;
    if (mask & SDI_STR) pScopeDataItem->displayname = m_bstrDisplayName != NULL ? (LPOLESTR)m_bstrDisplayName : (LPOLESTR)L"";
    if (mask & SDI_IMAGE) pScopeDataItem->nImage = m_scopeDataItem.nImage;
    if (mask & SDI_OPENIMAGE) pScopeDataItem->nOpenImage = m_scopeDataItem.nOpenImage;
    if (mask & SDI_PARAM) pScopeDataItem->lParam = m_scopeDataItem.lParam;
    if (mask & SDI_STATE) pScopeDataItem->nState = m_scopeDataItem.nState;
    if (mask & SDI_CHILDREN) pScopeDataItem->cChildren = m_scopeDataItem.cChildren;
    return S_OK;
  }
  STDMETHOD(GetResultViewType)(LPOLESTR* ppViewType, long* pViewOptions) override {
    if (ppViewType == NULL || pViewOptions == NULL) return E_POINTER;
    *ppViewType = NULL;
    *pViewOptions = MMC_VIEW_OPTIONS_NONE;
    return S_FALSE;
  }
  STDMETHOD(GetResultPaneInfo)(RESULTDATAITEM* pResultDataItem) override {
    if (pResultDataItem == NULL) return E_POINTER;
    if (pResultDataItem->bScopeItem) {
      if (pResultDataItem->mask & RDI_STR) pResultDataItem->str = _ColumnText(pResultDataItem->nCol);
      if (pResultDataItem->mask & RDI_IMAGE) pResultDataItem->nImage = m_scopeDataItem.nImage;
      if (pResultDataItem->mask & RDI_PARAM) pResultDataItem->lParam = m_scopeDataItem.lParam;
      return S_OK;
    }
    if (pResultDataItem->mask & RDI_STR) pResultDataItem->str = _ColumnText(pResultDataItem->nCol);
    if (pResultDataItem->mask & RDI_IMAGE) pResultDataItem->nImage = m_resultDataItem.nImage;
    if (pResultDataItem->mask & RDI_PARAM) pResultDataItem->lParam = m_resultDataItem.lParam;
    if (pResultDataItem->mask & RDI_INDEX) pResultDataItem->nIndex = m_resultDataItem.nIndex;
    return S_OK;
  }
  // Column text: the display name in the first, T's GetResultPaneColInfo
  // for the others.
  LPOLESTR GetResultPaneColInfo(int nCol) {
    if (nCol == 0) return m_bstrDisplayName != NULL ? (LPOLESTR)m_bstrDisplayName : (LPOLESTR)L"";
    return (LPOLESTR)L"";
  }

  STDMETHOD(AddMenuItems)(LPCONTEXTMENUCALLBACK piCallback, long* pInsertionAllowed,
                          DATA_OBJECT_TYPES /*type*/) override {
    if (piCallback == NULL || pInsertionAllowed == NULL) return E_POINTER;
    T* pT = static_cast<T*>(this);
    UINT menuID = pT->GetMenuID();
    if (menuID == 0) return S_OK;
    HMENU hMenu = ::LoadMenu(_AtlBaseModule.GetResourceInstance(), MAKEINTRESOURCE(menuID));
    if (hMenu == NULL) return S_OK;
    pT->SetMenuInsertionFlags(true, pInsertionAllowed);
    static const struct {
      long lInsertionPointID;
      long lAllowed;
    } kPoints[] = {{(long)CCM_INSERTIONPOINTID_PRIMARY_TOP, CCM_INSERTIONALLOWED_TOP},
                   {(long)CCM_INSERTIONPOINTID_PRIMARY_NEW, CCM_INSERTIONALLOWED_NEW},
                   {(long)CCM_INSERTIONPOINTID_PRIMARY_TASK, CCM_INSERTIONALLOWED_TASK},
                   {(long)CCM_INSERTIONPOINTID_PRIMARY_VIEW, CCM_INSERTIONALLOWED_VIEW}};
    HRESULT hr = S_OK;
    int nPopups = ::GetMenuItemCount(hMenu);
    for (int i = 0; i < 4 && i < nPopups && SUCCEEDED(hr); i++) {
      if (!(*pInsertionAllowed & kPoints[i].lAllowed)) continue;
      HMENU hSub = ::GetSubMenu(hMenu, i);
      if (hSub == NULL) continue;
      int nItems = ::GetMenuItemCount(hSub);
      for (int j = 0; j < nItems && SUCCEEDED(hr); j++) {
        UINT nID = ::GetMenuItemID(hSub, j);
        UINT nState = ::GetMenuState(hSub, (UINT)j, MF_BYPOSITION);
        TCHAR szText[256] = _T("");
        ::GetMenuString(hSub, (UINT)j, szText, 256, MF_BYPOSITION);
        CONTEXTMENUITEM item;
        memset(&item, 0, sizeof(item));
        item.lInsertionPointID = kPoints[i].lInsertionPointID;
        if (nState & MF_SEPARATOR) {
          item.fSpecialFlags = CCM_SPECIAL_SEPARATOR;
          item.fFlags = MF_SEPARATOR;
          hr = piCallback->AddItem(&item);
          continue;
        }
        UINT uFlags = nState & (MF_GRAYED | MF_DISABLED | MF_CHECKED | MF_MENUBARBREAK | MF_MENUBREAK);
        pT->UpdateMenuState(nID, szText, &uFlags);
        CStringW strName(szText), strStatus;
        int nNl = strName.Find(L'\n');
        if (nNl >= 0) {
          strStatus = strName.Mid(nNl + 1);
          strName = strName.Left(nNl);
        }
        item.strName = (LPWSTR)(LPCWSTR)strName;
        item.strStatusBarText = (LPWSTR)(LPCWSTR)strStatus;
        item.lCommandID = (LONG)nID;
        item.fFlags = (LONG)uFlags;
        hr = piCallback->AddItem(&item);
      }
    }
    pT->SetMenuInsertionFlags(false, pInsertionAllowed);
    ::DestroyMenu(hMenu);
    return hr;
  }
  STDMETHOD(Command)(long lCommandID, CSnapInObjectRootBase* pObj, DATA_OBJECT_TYPES type) override {
    bool bHandled = false;
    HRESULT hr = static_cast<T*>(this)->ProcessCommand((UINT)lCommandID, bHandled, pObj, type);
    return bHandled ? hr : S_FALSE;
  }
  STDMETHOD(CreatePropertyPages)(LPPROPERTYSHEETCALLBACK, LONG_PTR, IUnknown*, DATA_OBJECT_TYPES) override {
    return E_UNEXPECTED;
  }
  STDMETHOD(QueryPagesFor)(DATA_OBJECT_TYPES) override { return S_FALSE; }

  // The node's toolbars, made on the control bar the first time.
  STDMETHOD(SetControlbar)(IControlbar* pControlbar, IExtendControlbar* pExtendControlbar,
                           CSimpleMap<UINT, IUnknown*>* pToolbarMap) override {
    if (pControlbar == NULL || pToolbarMap == NULL) return E_POINTER;
    T* pT = static_cast<T*>(this);
    CSnapInToolbarInfo* pInfo = pT->GetToolbarInfo();
    if (pInfo == NULL) return S_OK;
    HRESULT hr = S_OK;
    for (; pInfo->m_idToolbar != 0 && SUCCEEDED(hr); pInfo++) {
      if (pToolbarMap->Lookup(pInfo->m_idToolbar) != NULL) continue;
      CComPtr<IUnknown> spUnk;
      hr = pControlbar->Create(TOOLBAR, pExtendControlbar, &spUnk);
      if (FAILED(hr)) break;
      CComPtr<IToolbar> spToolbar;
      hr = spUnk->QueryInterface(IID_IToolbar, (void**)&spToolbar);
      if (FAILED(hr)) break;
      hr = _MakeToolbar(pT, pInfo, spToolbar);
      if (FAILED(hr)) break;
      pToolbarMap->Add(pInfo->m_idToolbar, spToolbar.p);
      spToolbar.p->AddRef();
    }
    return hr;
  }
  // A node selected attaches its toolbars and sets their buttons; a
  // button clicked is a command.
  STDMETHOD(ControlbarNotify)(IControlbar* pControlbar, IExtendControlbar* /*pExtendControlbar*/,
                              CSimpleMap<UINT, IUnknown*>* pToolbarMap, MMC_NOTIFY_TYPE event, LPARAM arg,
                              LPARAM param, CSnapInObjectRootBase* pObj, DATA_OBJECT_TYPES type) override {
    T* pT = static_cast<T*>(this);
    if (event == MMCN_BTN_CLICK) {
      bool bHandled = false;
      HRESULT hr = pT->ProcessCommand((UINT)param, bHandled, pObj, type);
      return bHandled ? hr : S_FALSE;
    }
    if (event != MMCN_SELECT || pControlbar == NULL || pToolbarMap == NULL) return S_OK;
    BOOL bSelect = (BOOL)HIWORD(arg);
    CSnapInToolbarInfo* pInfo = pT->GetToolbarInfo();
    if (pInfo == NULL) return S_OK;
    for (; pInfo->m_idToolbar != 0; pInfo++) {
      IUnknown* pUnk = pToolbarMap->Lookup(pInfo->m_idToolbar);
      if (pUnk == NULL) continue;
      if (!bSelect) {
        pControlbar->Detach(pUnk);
        continue;
      }
      pControlbar->Attach(TOOLBAR, pUnk);
      CComPtr<IToolbar> spToolbar;
      if (FAILED(pUnk->QueryInterface(IID_IToolbar, (void**)&spToolbar))) continue;
      static const MMC_BUTTON_STATE kStates[] = {ENABLED, CHECKED, HIDDEN, INDETERMINATE, BUTTONPRESSED};
      for (UINT i = 0; i < pInfo->m_nButtonCount; i++) {
        UINT id = pInfo->m_pnButtonID[i];
        if (id == 0) continue;
        for (MMC_BUTTON_STATE st : kStates)
          spToolbar->SetButtonState((int)id, st, pT->UpdateToolbarButton(id, (BYTE)st));
      }
    }
    return S_OK;
  }
  STDMETHOD(GetScopeData)(SCOPEDATAITEM** pScopeDataItem) override {
    if (pScopeDataItem == NULL) return E_POINTER;
    *pScopeDataItem = &m_scopeDataItem;
    return S_OK;
  }
  STDMETHOD(GetResultData)(RESULTDATAITEM** pResultDataItem) override {
    if (pResultDataItem == NULL) return E_POINTER;
    *pResultDataItem = &m_resultDataItem;
    return S_OK;
  }
  STDMETHOD(FillData)(CLIPFORMAT cf, LPSTREAM pStream) override {
    if (pStream == NULL) return E_POINTER;
    CSnapInItem::Init();
    T* pT = static_cast<T*>(this);
    if (cf == m_CCF_NODETYPE) return pStream->Write(pT->GetNodeType(), sizeof(GUID), NULL);
    if (cf == m_CCF_SZNODETYPE) {
      const OLECHAR* psz = (const OLECHAR*)pT->GetSZNodeType();
      return pStream->Write(psz, (ULONG)((ocslen(psz) + 1) * sizeof(OLECHAR)), NULL);
    }
    if (cf == m_CCF_DISPLAY_NAME) {
      const OLECHAR* psz = (const OLECHAR*)pT->GetDisplayName();
      return pStream->Write(psz, (ULONG)((ocslen(psz) + 1) * sizeof(OLECHAR)), NULL);
    }
    if (cf == m_CCF_SNAPIN_CLASSID) return pStream->Write(pT->GetSnapInCLSID(), sizeof(GUID), NULL);
    return DV_E_FORMATETC;
  }
  HRESULT STDMETHODCALLTYPE GetDataObject(IDataObject** pDataObj, DATA_OBJECT_TYPES type) override {
    if (pDataObj == NULL) return E_POINTER;
    *pDataObj = NULL;
    CComObject<CSnapInDataObjectImpl>* pData = NULL;
    HRESULT hr = CComObject<CSnapInDataObjectImpl>::CreateInstance(&pData);
    if (FAILED(hr)) return hr;
    pData->m_objectData.m_pItem = this;
    pData->m_objectData.m_type = type;
    return pData->QueryInterface(IID_IDataObject, (void**)pDataObj);
  }

protected:
  static size_t ocslen(const OLECHAR* psz) {
    size_t n = 0;
    if (psz != NULL)
      while (psz[n] != 0) n++;
    return n;
  }
  LPOLESTR _ColumnText(int nCol) { return static_cast<T*>(this)->GetResultPaneColInfo(nCol); }

  // The buttons of an RT_TOOLBAR resource, their bitmap of the same ID and
  // their strings.
  static HRESULT _MakeToolbar(T* pT, CSnapInToolbarInfo* pInfo, IToolbar* pToolbar) {
    HINSTANCE hInst = _AtlBaseModule.GetResourceInstance();
    HRSRC hRsrc = ::FindResource(hInst, MAKEINTRESOURCE(pInfo->m_idToolbar), RT_TOOLBAR);
    if (hRsrc == NULL) return E_FAIL;
    HGLOBAL hGlobal = ::LoadResource(hInst, hRsrc);
    const WORD* pData = hGlobal != NULL ? (const WORD*)::LockResource(hGlobal) : NULL;
    if (pData == NULL || pData[0] != 1) return E_FAIL;
    int cx = pData[1], cy = pData[2];
    UINT nItems = pData[3];
    const WORD* pItems = pData + 4;
    pInfo->CleanUp();
    pInfo->m_nButtonCount = nItems;
    pInfo->m_pnButtonID = new UINT[nItems];
    pInfo->m_pStrButtonText = new OLECHAR*[nItems];
    pInfo->m_pStrToolTip = new OLECHAR*[nItems];
    int nImages = 0;
    for (UINT i = 0; i < nItems; i++)
      if (pItems[i] != 0) nImages++;
    HBITMAP hbm = ::LoadBitmap(hInst, MAKEINTRESOURCE(pInfo->m_idToolbar));
    HRESULT hr = S_OK;
    if (hbm != NULL) {
      hr = pToolbar->AddBitmap(nImages, hbm, cx, cy, RGB(192, 192, 192));
      ::DeleteObject(hbm);
      if (FAILED(hr)) return hr;
    }
    MMCBUTTON* pButtons = new MMCBUTTON[nItems > 0 ? nItems : 1];
    int nImage = 0;
    for (UINT i = 0; i < nItems; i++) {
      UINT id = pItems[i];
      pInfo->m_pnButtonID[i] = id;
      pInfo->m_pStrButtonText[i] = NULL;
      pInfo->m_pStrToolTip[i] = NULL;
      MMCBUTTON& b = pButtons[i];
      memset(&b, 0, sizeof(b));
      if (id == 0) {
        b.fsType = TBSTYLE_SEP;
        b.lpButtonText = b.lpTooltipText = (LPOLESTR)L"";
        continue;
      }
      b.nBitmap = nImage++;
      b.idCommand = (int)id;
      b.fsState = TBSTATE_ENABLED;
      b.fsType = TBSTYLE_BUTTON;
      TCHAR szText[256] = _T("");
      ::LoadString(hInst, id, szText, 256);
      CStringW strText(szText), strTip;
      int nNl = strText.Find(L'\n');
      if (nNl >= 0) {
        strTip = strText.Mid(nNl + 1);
        strText = strText.Left(nNl);
      }
      pInfo->m_pStrButtonText[i] = _Dup(strText);
      pInfo->m_pStrToolTip[i] = _Dup(strTip);
      b.lpButtonText = pInfo->m_pStrButtonText[i];
      b.lpTooltipText = pInfo->m_pStrToolTip[i];
      pT->SetToolbarButtonInfo(id, &b.fsState, &b.fsType);
    }
    hr = pToolbar->AddButtons((int)nItems, pButtons);
    delete[] pButtons;
    return hr;
  }
  static OLECHAR* _Dup(const CStringW& s) {
    OLECHAR* p = new OLECHAR[(size_t)s.GetLength() + 1];
    memcpy(p, (LPCWSTR)s, ((size_t)s.GetLength() + 1) * sizeof(OLECHAR));
    return p;
  }
};

// ---------------------------------------------------------------------
// The component data and components.

template <class T, class Component>
class ATL_NO_VTABLE IComponentDataImpl : public IComponentData {
public:
  CComPtr<IConsole> m_spConsole;
  CSnapInItem* m_pNode;

  IComponentDataImpl() : m_pNode(NULL) {}

  STDMETHOD(Initialize)(LPUNKNOWN pUnknown) override {
    if (pUnknown == NULL) return E_POINTER;
    CSnapInItem::Init();
    m_spConsole.Release();
    return pUnknown->QueryInterface(IID_IConsole, (void**)&m_spConsole);
  }
  STDMETHOD(CreateComponent)(LPCOMPONENT* ppComponent) override {
    if (ppComponent == NULL) return E_POINTER;
    *ppComponent = NULL;
    CComObject<Component>* pComponent = NULL;
    HRESULT hr = CComObject<Component>::CreateInstance(&pComponent);
    if (FAILED(hr)) return hr;
    pComponent->m_pComponentData = static_cast<T*>(this);
    return pComponent->QueryInterface(IID_IComponent, (void**)ppComponent);
  }
  STDMETHOD(Notify)(LPDATAOBJECT lpDataObject, MMC_NOTIFY_TYPE event, LPARAM arg, LPARAM param) override {
    if (lpDataObject == NULL) return E_POINTER;
    CSnapInItem* pItem = NULL;
    DATA_OBJECT_TYPES type = CCT_UNINITIALIZED;
    HRESULT hr = static_cast<T*>(this)->GetDataClass(lpDataObject, &pItem, &type);
    if (FAILED(hr) || pItem == NULL) return FAILED(hr) ? hr : E_UNEXPECTED;
    return pItem->Notify(event, arg, param, this, NULL, type);
  }
  STDMETHOD(Destroy)() override {
    m_spConsole.Release();
    static_cast<T*>(this)->ReleaseToolbars();
    return S_OK;
  }
  STDMETHOD(QueryDataObject)(MMC_COOKIE cookie, DATA_OBJECT_TYPES type, LPDATAOBJECT* ppDataObject) override {
    if (ppDataObject == NULL) return E_POINTER;
    *ppDataObject = NULL;
    CSnapInItem* pItem = cookie == 0 ? m_pNode : (CSnapInItem*)cookie;
    if (pItem == NULL) return E_UNEXPECTED;
    return pItem->GetDataObject(ppDataObject, type);
  }
  STDMETHOD(GetDisplayInfo)(SCOPEDATAITEM* pScopeDataItem) override {
    if (pScopeDataItem == NULL) return E_POINTER;
    CSnapInItem* pItem = pScopeDataItem->lParam != 0 ? (CSnapInItem*)pScopeDataItem->lParam : m_pNode;
    if (pItem == NULL) return E_UNEXPECTED;
    return pItem->GetScopePaneInfo(pScopeDataItem);
  }
  STDMETHOD(CompareObjects)(LPDATAOBJECT lpDataObjectA, LPDATAOBJECT lpDataObjectB) override {
    CSnapInItem *pA = NULL, *pB = NULL;
    T* pT = static_cast<T*>(this);
    if (FAILED(pT->GetDataClass(lpDataObjectA, &pA, NULL)) || FAILED(pT->GetDataClass(lpDataObjectB, &pB, NULL)))
      return E_UNEXPECTED;
    return pA == pB ? S_OK : S_FALSE;
  }
};

template <class T>
class ATL_NO_VTABLE IComponentImpl : public IComponent {
public:
  CComPtr<IConsole> m_spConsole;

  STDMETHOD(Initialize)(LPCONSOLE lpConsole) override {
    if (lpConsole == NULL) return E_POINTER;
    m_spConsole = lpConsole;
    return S_OK;
  }
  STDMETHOD(Notify)(LPDATAOBJECT lpDataObject, MMC_NOTIFY_TYPE event, LPARAM arg, LPARAM param) override {
    if (lpDataObject == NULL) return E_POINTER;
    CSnapInItem* pItem = NULL;
    DATA_OBJECT_TYPES type = CCT_UNINITIALIZED;
    HRESULT hr = static_cast<T*>(this)->GetDataClass(lpDataObject, &pItem, &type);
    if (FAILED(hr) || pItem == NULL) return FAILED(hr) ? hr : E_UNEXPECTED;
    return pItem->Notify(event, arg, param, NULL, this, type);
  }
  STDMETHOD(Destroy)(MMC_COOKIE /*cookie*/) override {
    m_spConsole.Release();
    static_cast<T*>(this)->ReleaseToolbars();
    return S_OK;
  }
  STDMETHOD(QueryDataObject)(MMC_COOKIE cookie, DATA_OBJECT_TYPES type, LPDATAOBJECT* ppDataObject) override {
    if (ppDataObject == NULL) return E_POINTER;
    *ppDataObject = NULL;
    CSnapInItem* pItem = _Item(cookie);
    if (pItem == NULL) return E_UNEXPECTED;
    return pItem->GetDataObject(ppDataObject, type);
  }
  STDMETHOD(GetResultViewType)(MMC_COOKIE cookie, LPOLESTR* ppViewType, long* pViewOptions) override {
    CSnapInItem* pItem = _Item(cookie);
    if (pItem == NULL) return E_UNEXPECTED;
    return pItem->GetResultViewType(ppViewType, pViewOptions);
  }
  STDMETHOD(GetDisplayInfo)(RESULTDATAITEM* pResultDataItem) override {
    if (pResultDataItem == NULL) return E_POINTER;
    CSnapInItem* pItem = _Item(pResultDataItem->lParam);
    if (pItem == NULL) return E_UNEXPECTED;
    return pItem->GetResultPaneInfo(pResultDataItem);
  }
  STDMETHOD(CompareObjects)(LPDATAOBJECT lpDataObjectA, LPDATAOBJECT lpDataObjectB) override {
    CSnapInItem *pA = NULL, *pB = NULL;
    T* pT = static_cast<T*>(this);
    if (FAILED(pT->GetDataClass(lpDataObjectA, &pA, NULL)) || FAILED(pT->GetDataClass(lpDataObjectB, &pB, NULL)))
      return E_UNEXPECTED;
    return pA == pB ? S_OK : S_FALSE;
  }

private:
  CSnapInItem* _Item(MMC_COOKIE cookie) {
    if (cookie != 0) return (CSnapInItem*)cookie;
    T* pT = static_cast<T*>(this);
    return pT->m_pComponentData != NULL ? pT->m_pComponentData->m_pNode : NULL;
  }
};

// ---------------------------------------------------------------------
// What the nodes add: menus, property pages and toolbars.

template <class T>
class ATL_NO_VTABLE IExtendContextMenuImpl : public IExtendContextMenu {
public:
  STDMETHOD(AddMenuItems)(LPDATAOBJECT pDataObject, LPCONTEXTMENUCALLBACK piCallback,
                          long* pInsertionAllowed) override {
    CSnapInItem* pItem = NULL;
    DATA_OBJECT_TYPES type = CCT_UNINITIALIZED;
    HRESULT hr = static_cast<T*>(this)->GetDataClass(pDataObject, &pItem, &type);
    if (FAILED(hr) || pItem == NULL) return FAILED(hr) ? hr : E_UNEXPECTED;
    return pItem->AddMenuItems(piCallback, pInsertionAllowed, type);
  }
  STDMETHOD(Command)(long lCommandID, LPDATAOBJECT pDataObject) override {
    CSnapInItem* pItem = NULL;
    DATA_OBJECT_TYPES type = CCT_UNINITIALIZED;
    T* pT = static_cast<T*>(this);
    HRESULT hr = pT->GetDataClass(pDataObject, &pItem, &type);
    if (FAILED(hr) || pItem == NULL) return FAILED(hr) ? hr : E_UNEXPECTED;
    return pItem->Command(lCommandID, static_cast<CSnapInObjectRootBase*>(pT), type);
  }
};

template <class T>
class ATL_NO_VTABLE IExtendPropertySheetImpl : public IExtendPropertySheet {
public:
  STDMETHOD(CreatePropertyPages)(LPPROPERTYSHEETCALLBACK lpProvider, LONG_PTR handle,
                                 LPDATAOBJECT pDataObject) override {
    CSnapInItem* pItem = NULL;
    DATA_OBJECT_TYPES type = CCT_UNINITIALIZED;
    T* pT = static_cast<T*>(this);
    HRESULT hr = pT->GetDataClass(pDataObject, &pItem, &type);
    if (FAILED(hr) || pItem == NULL) return FAILED(hr) ? hr : E_UNEXPECTED;
    return pItem->CreatePropertyPages(lpProvider, handle, static_cast<IExtendPropertySheet*>(this), type);
  }
  STDMETHOD(QueryPagesFor)(LPDATAOBJECT pDataObject) override {
    CSnapInItem* pItem = NULL;
    DATA_OBJECT_TYPES type = CCT_UNINITIALIZED;
    HRESULT hr = static_cast<T*>(this)->GetDataClass(pDataObject, &pItem, &type);
    if (FAILED(hr) || pItem == NULL) return FAILED(hr) ? hr : E_UNEXPECTED;
    return pItem->QueryPagesFor(type);
  }
};

template <class T>
class ATL_NO_VTABLE IExtendControlbarImpl : public IExtendControlbar {
public:
  STDMETHOD(SetControlbar)(LPCONTROLBAR pControlbar) override {
    T* pT = static_cast<T*>(this);
    if (pControlbar == NULL) pT->ReleaseToolbars();
    pT->m_spControlbar = pControlbar;
    return S_OK;
  }
  // The data object is param for MMCN_SELECT and arg for MMCN_BTN_CLICK.
  STDMETHOD(ControlbarNotify)(MMC_NOTIFY_TYPE event, LPARAM arg, LPARAM param) override {
    T* pT = static_cast<T*>(this);
    IDataObject* pDataObject = NULL;
    if (event == MMCN_SELECT)
      pDataObject = (IDataObject*)param;
    else if (event == MMCN_BTN_CLICK)
      pDataObject = (IDataObject*)arg;
    else
      return S_OK;
    CSnapInItem* pItem = NULL;
    DATA_OBJECT_TYPES type = CCT_UNINITIALIZED;
    HRESULT hr = pT->GetDataClass(pDataObject, &pItem, &type);
    if (FAILED(hr) || pItem == NULL) return FAILED(hr) ? hr : E_UNEXPECTED;
    if (pT->m_spControlbar != NULL) {
      hr = pItem->SetControlbar(pT->m_spControlbar, this, &pT->m_toolbarMap);
      if (FAILED(hr)) return hr;
    }
    return pItem->ControlbarNotify(pT->m_spControlbar, this, &pT->m_toolbarMap, event, arg, param,
                                   static_cast<CSnapInObjectRootBase*>(pT), type);
  }
};

// ---------------------------------------------------------------------
// Extensions: the nodes of another snap-in, found by their node type.
//
//   EXTENSION_SNAPIN_DATACLASS(CMyExtNode)
//   BEGIN_EXTENSION_SNAPIN_NODEINFO_MAP(CMyExtension)
//     EXTENSION_SNAPIN_NODEINFO_ENTRY(CMyExtNode)
//   END_EXTENSION_SNAPIN_NODEINFO_MAP()

#define EXTENSION_SNAPIN_DATACLASS(dataClass) dataClass m_##dataClass;

#define BEGIN_EXTENSION_SNAPIN_NODEINFO_MAP(classname)                                           \
  HRESULT GetDataClass(IDataObject* pDataObject, ATL::CSnapInItem** ppItem, DATA_OBJECT_TYPES* pType) { \
    if (ppItem == NULL) return E_POINTER;                                                       \
    *ppItem = NULL;                                                                             \
    GUID guidNode = GUID_NULL;                                                                  \
    bool bHaveNode = ATL::_AtlSnapInNodeType(pDataObject, &guidNode);

#define EXTENSION_SNAPIN_NODEINFO_ENTRY(dataClass)                                             \
  if (bHaveNode && InlineIsEqualGUID(guidNode, *(const GUID*)m_##dataClass.GetNodeType())) {    \
    *ppItem = m_##dataClass.GetExtNodeObject(pDataObject, &m_##dataClass);                      \
    if (*ppItem == NULL) return E_UNEXPECTED;                                                   \
    (*ppItem)->InitDataClass(pDataObject, &m_##dataClass);                                      \
    if (pType != NULL) *pType = CCT_SCOPE;                                                      \
    return S_OK;                                                                                \
  }

#define END_EXTENSION_SNAPIN_NODEINFO_MAP()                          \
  return ATL::CSnapInItem::GetDataClass(pDataObject, ppItem, pType); \
  }

// The node type in another snap-in's data object.
inline bool _AtlSnapInNodeType(IDataObject* pDataObject, GUID* pGuid) {
  if (pDataObject == NULL) return false;
  CSnapInItem::Init();
  STGMEDIUM stgmedium = {TYMED_HGLOBAL, {NULL}, NULL};
  FORMATETC formatetc = {CSnapInItem::m_CCF_NODETYPE, NULL, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
  stgmedium.hGlobal = ::GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, sizeof(GUID));
  if (stgmedium.hGlobal == NULL) return false;
  bool b = false;
  if (SUCCEEDED(pDataObject->GetDataHere(&formatetc, &stgmedium))) {
    GUID* p = (GUID*)::GlobalLock(stgmedium.hGlobal);
    if (p != NULL) {
      *pGuid = *p;
      ::GlobalUnlock(stgmedium.hGlobal);
      b = true;
    }
  }
  ::GlobalFree(stgmedium.hGlobal);
  return b;
}

// ---------------------------------------------------------------------
// Property pages of a node, for IPropertySheetCallback::AddPage.

template <class T, bool bAutoDelete = true>
class ATL_NO_VTABLE CSnapInPropertyPageImpl : public CDialogImplBase {
public:
  PROPSHEETPAGE m_psp;

  operator PROPSHEETPAGE*() { return &m_psp; }

  explicit CSnapInPropertyPageImpl(LPCTSTR lpszTitle = NULL) {
    memset(&m_psp, 0, sizeof(m_psp));
    m_psp.dwSize = sizeof(m_psp);
    m_psp.dwFlags = PSP_USECALLBACK;
    m_psp.hInstance = _AtlBaseModule.GetResourceInstance();
    m_psp.pszTemplate = MAKEINTRESOURCE(T::IDD);
    m_psp.pfnDlgProc = (DLGPROC)T::StartDialogProc;
    m_psp.pfnCallback = T::PropPageCallback;
    m_psp.lParam = (LPARAM)this;
    if (lpszTitle != NULL) {
      m_psp.pszTitle = lpszTitle;
      m_psp.dwFlags |= PSP_USETITLE;
    }
  }

  static UINT CALLBACK PropPageCallback(HWND /*hWnd*/, UINT uMsg, LPPROPSHEETPAGE ppsp) {
    if (uMsg == PSPCB_CREATE) {
      CDialogImplBase* pPage = (CDialogImplBase*)(T*)(CSnapInPropertyPageImpl*)ppsp->lParam;
      _AtlWinModule.AddCreateWndData(&pPage->m_thunk.cd, pPage);
    }
    if (bAutoDelete && uMsg == PSPCB_RELEASE) {
      T* pPage = (T*)(CSnapInPropertyPageImpl*)ppsp->lParam;
      delete pPage;
    }
    return 1;
  }

  HPROPSHEETPAGE Create() { return ::CreatePropertySheetPage(&m_psp); }
  BOOL EndDialog(int) { return FALSE; }

  void SetModified(BOOL bChanged = TRUE) {
    ::SendMessage(GetParent(), bChanged ? PSM_CHANGED : PSM_UNCHANGED, (WPARAM)m_hWnd, 0);
  }
  LRESULT QuerySiblings(WPARAM wParam, LPARAM lParam) {
    return ::SendMessage(GetParent(), PSM_QUERYSIBLINGS, wParam, lParam);
  }

  BEGIN_MSG_MAP(CSnapInPropertyPageImpl)
    MESSAGE_HANDLER(WM_NOTIFY, OnNotify)
  END_MSG_MAP()

  LRESULT OnNotify(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& bHandled) {
    NMHDR* pNMHDR = (NMHDR*)lParam;
    if (pNMHDR->hwndFrom != GetParent()) {
      bHandled = FALSE;
      return 1;
    }
    T* pT = static_cast<T*>(this);
    LRESULT lResult = 0;
    switch (pNMHDR->code) {
      case PSN_SETACTIVE: lResult = pT->OnSetActive() ? 0 : -1; break;
      case PSN_KILLACTIVE: lResult = !pT->OnKillActive(); break;
      case PSN_APPLY: lResult = pT->OnApply() ? PSNRET_NOERROR : PSNRET_INVALID_NOCHANGEPAGE; break;
      case PSN_RESET: pT->OnReset(); break;
      case PSN_QUERYCANCEL: lResult = !pT->OnQueryCancel(); break;
      case PSN_WIZNEXT: lResult = pT->OnWizardNext(); break;
      case PSN_WIZBACK: lResult = pT->OnWizardBack(); break;
      case PSN_WIZFINISH: lResult = !pT->OnWizardFinish(); break;
      case PSN_HELP: pT->OnHelp(); break;
      default: bHandled = FALSE; break;
    }
    return lResult;
  }

  BOOL OnSetActive() { return TRUE; }
  BOOL OnKillActive() { return TRUE; }
  BOOL OnApply() { return TRUE; }
  void OnReset() {}
  BOOL OnQueryCancel() { return TRUE; }
  int OnWizardBack() { return 0; }
  int OnWizardNext() { return 0; }
  BOOL OnWizardFinish() { return TRUE; }
  BOOL OnHelp() { return TRUE; }
};

}  // namespace ATL

#endif  // __ATLSNAP_H__
