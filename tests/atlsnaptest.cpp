// atlsnaptest.cpp - an MMC snap-in made with atlsnap.h, driven the way MMC
// drives one, through a console of the test's own:
//   - the component data: Initialize with the console, the root node's
//     data object and its clipboard formats (node type, display name,
//     class ID), GetDisplayInfo, CompareObjects, Notify reaching the node;
//   - a component made by CreateComponent: its data objects, the result
//     pane's text, the result view type, Notify;
//   - the context menu from the node's menu resource (the insertion points
//     allowed, the status bar text, a separator, the grayed item and a
//     checked one), and its commands through the command map, a range and
//     an unhandled one;
//   - the toolbar from the node's toolbar resource, its bitmap and button
//     strings, attached when the node is selected, its buttons' states,
//     and a button click as a command;
//   - a property page (CSnapInPropertyPageImpl) added through the property
//     sheet callback, shown in a property sheet and applied;
//   - an extension of the root node, found through its node type.
// Exits with 1 on a failure.
#include <atlbase.h>
#include <atlcom.h>
#include <atlstr.h>
#include <atlsnap.h>
#include <prsht.h>
#include <stdio.h>
#include <string>
#include <vector>
#include "atlsnaptest.h"

// The IIDs, which a snap-in gets from the SDK's MMC library. The test is
// both the snap-in and the console, so any values do.
#define TEST_IID(name, n) \
  EXTERN_C const IID IID_##name = {0x7a5e0000 + n, 0x1234, 0x4321, {0x9a, 0xbc, 0xde, 0xf0, 0x12, 0x34, 0x56, 0x78}};
TEST_IID(IComponentData, 1)
TEST_IID(IComponent, 2)
TEST_IID(IConsole, 3)
TEST_IID(IConsole2, 4)
TEST_IID(IConsoleNameSpace, 5)
TEST_IID(IConsoleNameSpace2, 6)
TEST_IID(IConsoleVerb, 7)
TEST_IID(IContextMenuCallback, 8)
TEST_IID(IContextMenuProvider, 9)
TEST_IID(IControlbar, 10)
TEST_IID(IExtendContextMenu, 11)
TEST_IID(IExtendControlbar, 12)
TEST_IID(IExtendPropertySheet, 13)
TEST_IID(IExtendPropertySheet2, 14)
TEST_IID(IHeaderCtrl, 15)
TEST_IID(IImageList, 16)
TEST_IID(IPropertySheetCallback, 17)
TEST_IID(IPropertySheetProvider, 18)
TEST_IID(IResultData, 19)
TEST_IID(IResultDataCompare, 20)
TEST_IID(ISnapinAbout, 21)
TEST_IID(ISnapinHelp, 22)
TEST_IID(IToolbar, 23)

CComModule _Module;

static int g_nFailures;
#define CHECK(cond)                                  \
  do {                                               \
    if (!(cond)) {                                   \
      printf("FAIL line %d: %s\n", __LINE__, #cond); \
      g_nFailures++;                                 \
    }                                                \
  } while (0)

static const GUID kNodeType = {0x1d6c4e2a, 0x4b1f, 0x4a39, {0x8e, 0x21, 0x5c, 0x3a, 0x7f, 0x90, 0x12, 0x34}};
static const GUID kChildType = {0x1d6c4e2b, 0x4b1f, 0x4a39, {0x8e, 0x21, 0x5c, 0x3a, 0x7f, 0x90, 0x12, 0x34}};
static const CLSID kSnapinClsid = {0x1d6c4e2c, 0x4b1f, 0x4a39, {0x8e, 0x21, 0x5c, 0x3a, 0x7f, 0x90, 0x12, 0x34}};

// What the nodes saw.
static std::vector<std::string> g_events;
static void Event(const char* sz) { g_events.push_back(sz); }
static std::string Notified(MMC_NOTIFY_TYPE event, bool bData, bool bComponent, long arg) {
  char sz[64];
  snprintf(sz, sizeof(sz), "notify %x %s %s %ld", (unsigned)event, bData ? "data" : "-",
           bComponent ? "component" : "-", arg);
  return sz;
}

// ---------------------------------------------------------------------
// The property page.

class CNamePage : public CSnapInPropertyPageImpl<CNamePage> {
public:
  enum { IDD = IDD_PAGE };
  static int s_nApplied, s_nDeleted;
  CNamePage() : CSnapInPropertyPageImpl<CNamePage>(_T("Name")) {}
  ~CNamePage() { s_nDeleted++; }

  BEGIN_MSG_MAP(CNamePage)
    MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
    CHAIN_MSG_MAP(CSnapInPropertyPageImpl<CNamePage>)
  END_MSG_MAP()

  LRESULT OnInitDialog(UINT, WPARAM, LPARAM, BOOL&) {
    SetDlgItemText(IDC_NAME, _T("Root"));
    SetModified(TRUE);
    return TRUE;
  }
  BOOL OnApply() {
    s_nApplied++;
    return TRUE;
  }
};
int CNamePage::s_nApplied, CNamePage::s_nDeleted;

// ---------------------------------------------------------------------
// The nodes.

class CChildNode : public CSnapInItemImpl<CChildNode> {
public:
  static const GUID* m_NODETYPE;
  static const OLECHAR* m_SZNODETYPE;
  static const OLECHAR* m_SZDISPLAY_NAME;
  static const CLSID* m_SNAPIN_CLASSID;
  CChildNode() {
    m_bstrDisplayName = L"Child";
    m_resultDataItem.nImage = 7;
    m_resultDataItem.lParam = (LPARAM)this;
  }
  LPOLESTR GetResultPaneColInfo(int nCol) {
    return nCol == 0 ? (LPOLESTR)m_bstrDisplayName : (LPOLESTR)L"second column";
  }
};
const GUID* CChildNode::m_NODETYPE = &kChildType;
const OLECHAR* CChildNode::m_SZNODETYPE = L"{1D6C4E2B-4B1F-4A39-8E21-5C3A7F901234}";
const OLECHAR* CChildNode::m_SZDISPLAY_NAME = L"Child";
const CLSID* CChildNode::m_SNAPIN_CLASSID = &kSnapinClsid;

class CRootNode : public CSnapInItemImpl<CRootNode> {
public:
  static const GUID* m_NODETYPE;
  static const OLECHAR* m_SZNODETYPE;
  static const OLECHAR* m_SZDISPLAY_NAME;
  static const CLSID* m_SNAPIN_CLASSID;
  CChildNode m_child;
  long m_lLastRange;
  CSnapInObjectRootBase* m_pLastObj;

  CRootNode() : m_lLastRange(0), m_pLastObj(NULL) {
    m_bstrDisplayName = L"Root";
    m_scopeDataItem.nImage = 1;
    m_scopeDataItem.nOpenImage = 2;
    m_scopeDataItem.cChildren = 1;
    m_scopeDataItem.lParam = (LPARAM)this;
  }

  SNAPINMENUID(IDR_NODEMENU)
  BEGIN_SNAPINTOOLBARID_MAP(CRootNode)
    SNAPINTOOLBARID_ENTRY(IDR_NODETOOLBAR)
  END_SNAPINTOOLBARID_MAP()
  BEGIN_SNAPINCOMMAND_MAP(CRootNode, FALSE)
    SNAPINCOMMAND_ENTRY(ID_REFRESH, OnRefresh)
    SNAPINCOMMAND_RANGE_ENTRY(ID_NEWITEM, ID_BIG, OnRange)
  END_SNAPINCOMMAND_MAP()

  HRESULT OnRefresh(bool& /*bHandled*/, CSnapInObjectRootBase* pObj) {
    m_pLastObj = pObj;
    Event("refresh");
    return S_OK;
  }
  // The range leaves ID_BIG to no one.
  HRESULT OnRange(UINT nID, bool& bHandled, CSnapInObjectRootBase* /*pObj*/) {
    if (nID == ID_BIG) {
      bHandled = false;
      return S_OK;
    }
    m_lLastRange = (long)nID;
    return S_OK;
  }
  void UpdateMenuState(UINT id, LPTSTR /*pBuf*/, UINT* flags) {
    if (id == ID_TASK) *flags |= MF_CHECKED;
  }
  BOOL UpdateToolbarButton(UINT id, BYTE fsState) {
    if (fsState == ENABLED) return TRUE;
    if (fsState == CHECKED) return id == ID_TASK;
    return FALSE;
  }

  STDMETHOD(Notify)(MMC_NOTIFY_TYPE event, LPARAM arg, LPARAM /*param*/, IComponentData* pComponentData,
                    IComponent* pComponent, DATA_OBJECT_TYPES type) override {
    Event(Notified(event, pComponentData != NULL, pComponent != NULL, (long)arg).c_str());
    (void)type;
    return S_OK;
  }
  STDMETHOD(QueryPagesFor)(DATA_OBJECT_TYPES type) override { return type == CCT_SCOPE ? S_OK : S_FALSE; }
  STDMETHOD(CreatePropertyPages)(LPPROPERTYSHEETCALLBACK lpProvider, LONG_PTR /*handle*/, IUnknown* pUnk,
                                 DATA_OBJECT_TYPES /*type*/) override {
    CHECK(pUnk != NULL);
    CNamePage* pPage = new CNamePage;
    return lpProvider->AddPage(pPage->Create());
  }
};
const GUID* CRootNode::m_NODETYPE = &kNodeType;
const OLECHAR* CRootNode::m_SZNODETYPE = L"{1D6C4E2A-4B1F-4A39-8E21-5C3A7F901234}";
const OLECHAR* CRootNode::m_SZDISPLAY_NAME = L"Test Snap-in";
const CLSID* CRootNode::m_SNAPIN_CLASSID = &kSnapinClsid;

// ---------------------------------------------------------------------
// The snap-in.

class CTestSnapin;

class ATL_NO_VTABLE CTestComponent : public CComObjectRootEx<CComSingleThreadModel>,
                                     public CSnapInObjectRoot<2, CTestSnapin>,
                                     public IComponentImpl<CTestComponent>,
                                     public IExtendContextMenuImpl<CTestComponent>,
                                     public IExtendControlbarImpl<CTestComponent>,
                                     public IExtendPropertySheetImpl<CTestComponent> {
public:
  BEGIN_COM_MAP(CTestComponent)
    COM_INTERFACE_ENTRY(IComponent)
    COM_INTERFACE_ENTRY(IExtendContextMenu)
    COM_INTERFACE_ENTRY(IExtendControlbar)
    COM_INTERFACE_ENTRY(IExtendPropertySheet)
  END_COM_MAP()
};

class ATL_NO_VTABLE CTestSnapin : public CComObjectRootEx<CComSingleThreadModel>,
                                  public CSnapInObjectRoot<1, CTestSnapin>,
                                  public IComponentDataImpl<CTestSnapin, CTestComponent>,
                                  public IExtendContextMenuImpl<CTestSnapin>,
                                  public IExtendPropertySheetImpl<CTestSnapin> {
public:
  CRootNode m_root;
  CTestSnapin() { m_pNode = &m_root; }
  BEGIN_COM_MAP(CTestSnapin)
    COM_INTERFACE_ENTRY(IComponentData)
    COM_INTERFACE_ENTRY(IExtendContextMenu)
    COM_INTERFACE_ENTRY(IExtendPropertySheet)
  END_COM_MAP()
};

// An extension of the root node's type.
class CExtNode : public CSnapInItemImpl<CExtNode, TRUE> {
public:
  static const GUID* m_NODETYPE;
  static const OLECHAR* m_SZNODETYPE;
  static const OLECHAR* m_SZDISPLAY_NAME;
  static const CLSID* m_SNAPIN_CLASSID;
  SNAPINMENUID(IDR_EXTMENU)
  BEGIN_SNAPINCOMMAND_MAP(CExtNode, TRUE)
    SNAPINCOMMAND_ENTRY(ID_EXTCMD, OnExt)
  END_SNAPINCOMMAND_MAP()
  HRESULT OnExt(bool&, CSnapInObjectRootBase*) {
    Event("extension");
    return S_OK;
  }
};
const GUID* CExtNode::m_NODETYPE = &kNodeType;
const OLECHAR* CExtNode::m_SZNODETYPE = L"{1D6C4E2A-4B1F-4A39-8E21-5C3A7F901234}";
const OLECHAR* CExtNode::m_SZDISPLAY_NAME = L"Extension";
const CLSID* CExtNode::m_SNAPIN_CLASSID = &kSnapinClsid;

class ATL_NO_VTABLE CTestExtension : public CComObjectRootEx<CComSingleThreadModel>,
                                     public CSnapInObjectRoot<0, CTestExtension>,
                                     public IExtendContextMenuImpl<CTestExtension> {
public:
  EXTENSION_SNAPIN_DATACLASS(CExtNode)
  BEGIN_EXTENSION_SNAPIN_NODEINFO_MAP(CTestExtension)
    EXTENSION_SNAPIN_NODEINFO_ENTRY(CExtNode)
  END_EXTENSION_SNAPIN_NODEINFO_MAP()
  BEGIN_COM_MAP(CTestExtension)
    COM_INTERFACE_ENTRY(IExtendContextMenu)
  END_COM_MAP()
};

// ---------------------------------------------------------------------
// The console's side.

class ATL_NO_VTABLE CMockConsole : public CComObjectRootEx<CComSingleThreadModel>,
                                   public IConsole,
                                   public IConsoleNameSpace {
public:
  BEGIN_COM_MAP(CMockConsole)
    COM_INTERFACE_ENTRY(IConsole)
    COM_INTERFACE_ENTRY(IConsoleNameSpace)
  END_COM_MAP()
  STDMETHOD(SetHeader)(LPHEADERCTRL) override { return E_NOTIMPL; }
  STDMETHOD(SetToolbar)(LPTOOLBAR) override { return E_NOTIMPL; }
  STDMETHOD(QueryResultView)(LPUNKNOWN*) override { return E_NOTIMPL; }
  STDMETHOD(QueryScopeImageList)(LPIMAGELIST*) override { return E_NOTIMPL; }
  STDMETHOD(QueryResultImageList)(LPIMAGELIST*) override { return E_NOTIMPL; }
  STDMETHOD(UpdateAllViews)(LPDATAOBJECT, LPARAM, LONG_PTR) override { return E_NOTIMPL; }
  STDMETHOD(MessageBox)(LPCWSTR, LPCWSTR, UINT, int*) override { return E_NOTIMPL; }
  STDMETHOD(QueryConsoleVerb)(LPCONSOLEVERB*) override { return E_NOTIMPL; }
  STDMETHOD(SelectScopeItem)(HSCOPEITEM) override { return E_NOTIMPL; }
  STDMETHOD(GetMainWindow)(HWND* phwnd) override {
    *phwnd = NULL;
    return S_OK;
  }
  STDMETHOD(NewWindow)(HSCOPEITEM, unsigned long) override { return E_NOTIMPL; }
  STDMETHOD(InsertItem)(LPSCOPEDATAITEM) override { return E_NOTIMPL; }
  STDMETHOD(DeleteItem)(HSCOPEITEM, long) override { return E_NOTIMPL; }
  STDMETHOD(SetItem)(LPSCOPEDATAITEM) override { return E_NOTIMPL; }
  STDMETHOD(GetItem)(LPSCOPEDATAITEM) override { return E_NOTIMPL; }
  STDMETHOD(GetChildItem)(HSCOPEITEM, HSCOPEITEM*, MMC_COOKIE*) override { return E_NOTIMPL; }
  STDMETHOD(GetNextItem)(HSCOPEITEM, HSCOPEITEM*, MMC_COOKIE*) override { return E_NOTIMPL; }
  STDMETHOD(GetParentItem)(HSCOPEITEM, HSCOPEITEM*, MMC_COOKIE*) override { return E_NOTIMPL; }
};

struct MenuItem {
  CStringW strName, strStatus;
  long lID, lInsertion, fFlags, fSpecial;
};

class ATL_NO_VTABLE CMockMenu : public CComObjectRootEx<CComSingleThreadModel>, public IContextMenuCallback {
public:
  BEGIN_COM_MAP(CMockMenu)
    COM_INTERFACE_ENTRY(IContextMenuCallback)
  END_COM_MAP()
  std::vector<MenuItem> m_items;
  STDMETHOD(AddItem)(CONTEXTMENUITEM* p) override {
    MenuItem m = {p->strName, p->strStatusBarText, p->lCommandID, p->lInsertionPointID, p->fFlags,
                  p->fSpecialFlags};
    m_items.push_back(m);
    return S_OK;
  }
};

class ATL_NO_VTABLE CMockSheet : public CComObjectRootEx<CComSingleThreadModel>, public IPropertySheetCallback {
public:
  BEGIN_COM_MAP(CMockSheet)
    COM_INTERFACE_ENTRY(IPropertySheetCallback)
  END_COM_MAP()
  std::vector<HPROPSHEETPAGE> m_pages;
  STDMETHOD(AddPage)(HPROPSHEETPAGE h) override {
    if (h == NULL) return E_INVALIDARG;
    m_pages.push_back(h);
    return S_OK;
  }
  STDMETHOD(RemovePage)(HPROPSHEETPAGE) override { return E_NOTIMPL; }
};

class ATL_NO_VTABLE CMockToolbar : public CComObjectRootEx<CComSingleThreadModel>, public IToolbar {
public:
  BEGIN_COM_MAP(CMockToolbar)
    COM_INTERFACE_ENTRY(IToolbar)
  END_COM_MAP()
  int m_nImages = 0, m_cx = 0, m_cy = 0;
  struct Button {
    int nBitmap, idCommand;
    BYTE fsState, fsType;
    CStringW strText, strTip;
  };
  std::vector<Button> m_buttons;
  std::vector<std::pair<int, int> > m_set;  // (id, state) set TRUE
  STDMETHOD(AddBitmap)(int nImages, HBITMAP hbmp, int cx, int cy, COLORREF) override {
    if (hbmp == NULL) return E_INVALIDARG;
    m_nImages = nImages;
    m_cx = cx;
    m_cy = cy;
    return S_OK;
  }
  STDMETHOD(AddButtons)(int n, LPMMCBUTTON p) override {
    for (int i = 0; i < n; i++) {
      Button b = {p[i].nBitmap, p[i].idCommand, p[i].fsState, p[i].fsType, p[i].lpButtonText,
                  p[i].lpTooltipText};
      m_buttons.push_back(b);
    }
    return S_OK;
  }
  STDMETHOD(InsertButton)(int, LPMMCBUTTON) override { return E_NOTIMPL; }
  STDMETHOD(DeleteButton)(int) override { return E_NOTIMPL; }
  STDMETHOD(GetButtonState)(int, MMC_BUTTON_STATE, BOOL*) override { return E_NOTIMPL; }
  STDMETHOD(SetButtonState)(int id, MMC_BUTTON_STATE st, BOOL b) override {
    if (b) m_set.push_back(std::make_pair(id, (int)st));
    return S_OK;
  }
};

class ATL_NO_VTABLE CMockControlbar : public CComObjectRootEx<CComSingleThreadModel>, public IControlbar {
public:
  BEGIN_COM_MAP(CMockControlbar)
    COM_INTERFACE_ENTRY(IControlbar)
  END_COM_MAP()
  CMockToolbar* m_pToolbar = NULL;
  int m_nCreated = 0, m_nAttached = 0, m_nDetached = 0;
  CComPtr<IUnknown> m_spToolbar;
  STDMETHOD(Create)(MMC_CONTROL_TYPE nType, LPEXTENDCONTROLBAR pExtend, LPUNKNOWN* ppUnknown) override {
    if (nType != TOOLBAR || pExtend == NULL) return E_INVALIDARG;
    CComObject<CMockToolbar>* p = NULL;
    CComObject<CMockToolbar>::CreateInstance(&p);
    m_pToolbar = p;
    m_spToolbar = p->GetUnknown();
    m_nCreated++;
    return p->QueryInterface(IID_IUnknown, (void**)ppUnknown);
  }
  STDMETHOD(Attach)(MMC_CONTROL_TYPE, LPUNKNOWN) override {
    m_nAttached++;
    return S_OK;
  }
  STDMETHOD(Detach)(LPUNKNOWN) override {
    m_nDetached++;
    return S_OK;
  }
};

template <class T>
static CComPtr<T> Make(CComObject<T>** pp = NULL) {
  CComObject<T>* p = NULL;
  CComObject<T>::CreateInstance(&p);
  if (pp != NULL) *pp = p;
  CComPtr<T> sp(p);
  return sp;
}

// A clipboard format of a data object, as bytes.
static std::string FormatOf(IDataObject* pData, CLIPFORMAT cf) {
  FORMATETC fe = {cf, NULL, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
  STGMEDIUM stg = {TYMED_HGLOBAL, {NULL}, NULL};
  stg.hGlobal = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, 512);
  std::string s;
  if (SUCCEEDED(pData->GetDataHere(&fe, &stg))) {
    const char* p = (const char*)GlobalLock(stg.hGlobal);
    s.assign(p, 512);
    GlobalUnlock(stg.hGlobal);
  }
  GlobalFree(stg.hGlobal);
  return s;
}

static void TestSnapin() {
  CComObject<CTestSnapin>* pSnapin = NULL;
  CComPtr<IComponentData> spData;
  {
    CComPtr<CTestSnapin> sp = Make<CTestSnapin>(&pSnapin);
    sp->QueryInterface(IID_IComponentData, (void**)&spData);
  }
  CHECK(spData != NULL);
  CComPtr<IConsole> spConsole;
  Make<CMockConsole>()->QueryInterface(IID_IConsole, (void**)&spConsole);
  CHECK(spData->Initialize(NULL) == E_POINTER);
  CHECK(spData->Initialize(spConsole) == S_OK);
  CHECK(pSnapin->m_spConsole == spConsole);
  CHECK(CSnapInItem::m_CCF_NODETYPE != 0 && CSnapInItem::m_CCF_SNAPIN_GETOBJECTDATA != 0);

  // The root's data object, and what MMC reads of it.
  CComPtr<IDataObject> spRootData;
  CHECK(spData->QueryDataObject(0, CCT_SCOPE, &spRootData) == S_OK && spRootData != NULL);
  std::string s = FormatOf(spRootData, CSnapInItem::m_CCF_NODETYPE);
  CHECK(s.size() >= sizeof(GUID) && memcmp(s.data(), &kNodeType, sizeof(GUID)) == 0);
  s = FormatOf(spRootData, CSnapInItem::m_CCF_DISPLAY_NAME);
  CHECK(wcscmp((const wchar_t*)s.data(), L"Test Snap-in") == 0);
  s = FormatOf(spRootData, CSnapInItem::m_CCF_SZNODETYPE);
  CHECK(wcscmp((const wchar_t*)s.data(), L"{1D6C4E2A-4B1F-4A39-8E21-5C3A7F901234}") == 0);
  s = FormatOf(spRootData, CSnapInItem::m_CCF_SNAPIN_CLASSID);
  CHECK(memcmp(s.data(), &kSnapinClsid, sizeof(GUID)) == 0);
  CSnapInItem* pItem = NULL;
  DATA_OBJECT_TYPES type = CCT_UNINITIALIZED;
  CHECK(CSnapInItem::GetDataClass(spRootData, &pItem, &type) == S_OK);
  CHECK(pItem == &pSnapin->m_root && type == CCT_SCOPE);

  // The scope pane's text, through the cookie in lParam.
  SCOPEDATAITEM sdi;
  memset(&sdi, 0, sizeof(sdi));
  sdi.mask = SDI_STR | SDI_IMAGE | SDI_OPENIMAGE | SDI_CHILDREN;
  sdi.lParam = (LPARAM)&pSnapin->m_root;
  CHECK(spData->GetDisplayInfo(&sdi) == S_OK);
  CHECK(sdi.displayname != NULL && wcscmp(sdi.displayname, L"Root") == 0);
  CHECK(sdi.nImage == 1 && sdi.nOpenImage == 2 && sdi.cChildren == 1);

  // Notify, through the data object.
  g_events.clear();
  CHECK(spData->Notify(spRootData, MMCN_EXPAND, TRUE, 0) == S_OK);
  CHECK(g_events.size() == 1 && g_events[0] == Notified(MMCN_EXPAND, true, false, 1));
  CHECK(spData->Notify(NULL, MMCN_EXPAND, TRUE, 0) == E_POINTER);

  // Data objects of the same node compare equal.
  CComPtr<IDataObject> spRootData2, spChildData;
  spData->QueryDataObject((MMC_COOKIE)&pSnapin->m_root, CCT_RESULT, &spRootData2);
  spData->QueryDataObject((MMC_COOKIE)&pSnapin->m_root.m_child, CCT_RESULT, &spChildData);
  CHECK(spData->CompareObjects(spRootData, spRootData2) == S_OK);
  CHECK(spData->CompareObjects(spRootData, spChildData) == S_FALSE);

  // A component.
  CComPtr<IComponent> spComponent;
  CHECK(spData->CreateComponent(&spComponent) == S_OK && spComponent != NULL);
  CHECK(spComponent->Initialize(spConsole) == S_OK);
  CComPtr<IDataObject> spCompRoot;
  CHECK(spComponent->QueryDataObject(0, CCT_SCOPE, &spCompRoot) == S_OK);
  CHECK(CSnapInItem::GetDataClass(spCompRoot, &pItem, &type) == S_OK && pItem == &pSnapin->m_root);
  g_events.clear();
  CHECK(spComponent->Notify(spCompRoot, MMCN_SHOW, TRUE, 0) == S_OK);
  CHECK(g_events.size() == 1 && g_events[0] == Notified(MMCN_SHOW, false, true, 1));
  RESULTDATAITEM rdi;
  memset(&rdi, 0, sizeof(rdi));
  rdi.mask = RDI_STR | RDI_IMAGE;
  rdi.lParam = (LPARAM)&pSnapin->m_root.m_child;
  rdi.nCol = 1;
  CHECK(spComponent->GetDisplayInfo(&rdi) == S_OK);
  CHECK(rdi.str != NULL && wcscmp(rdi.str, L"second column") == 0 && rdi.nImage == 7);
  rdi.nCol = 0;
  CHECK(spComponent->GetDisplayInfo(&rdi) == S_OK && wcscmp(rdi.str, L"Child") == 0);
  LPOLESTR pszView = (LPOLESTR)1;
  long lOptions = -1;
  CHECK(spComponent->GetResultViewType(0, &pszView, &lOptions) == S_FALSE);
  CHECK(pszView == NULL && lOptions == MMC_VIEW_OPTIONS_NONE);

  // The context menu, from the node's menu resource.
  CComObject<CMockMenu>* pMenu = NULL;
  CComPtr<CMockMenu> spMenu = Make<CMockMenu>(&pMenu);
  CComPtr<IExtendContextMenu> spMenuExt;
  spData->QueryInterface(IID_IExtendContextMenu, (void**)&spMenuExt);
  long lAllowed = CCM_INSERTIONALLOWED_TOP | CCM_INSERTIONALLOWED_TASK;
  CHECK(spMenuExt->AddMenuItems(spRootData, pMenu, &lAllowed) == S_OK);
  CHECK(pMenu->m_items.size() == 4);
  if (pMenu->m_items.size() == 4) {
    const std::vector<MenuItem>& m = pMenu->m_items;
    CHECK(m[0].strName == L"&Refresh" && m[0].strStatus == L"Refreshes the node");
    CHECK(m[0].lID == ID_REFRESH && m[0].lInsertion == (long)CCM_INSERTIONPOINTID_PRIMARY_TOP);
    CHECK(m[1].fSpecial == CCM_SPECIAL_SEPARATOR);
    CHECK(m[2].lID == ID_GRAY && (m[2].fFlags & MF_GRAYED));
    CHECK(m[3].lID == ID_TASK && m[3].lInsertion == (long)CCM_INSERTIONPOINTID_PRIMARY_TASK);
    CHECK((m[3].fFlags & MF_CHECKED) && m[3].strStatus.IsEmpty());
  }
  pMenu->m_items.clear();
  lAllowed = CCM_INSERTIONALLOWED_NEW | CCM_INSERTIONALLOWED_VIEW;
  CHECK(spMenuExt->AddMenuItems(spRootData, pMenu, &lAllowed) == S_OK);
  CHECK(pMenu->m_items.size() == 2 && pMenu->m_items[0].lID == ID_NEWITEM && pMenu->m_items[1].lID == ID_BIG);

  // Commands.
  g_events.clear();
  CHECK(spMenuExt->Command(ID_REFRESH, spRootData) == S_OK);
  CHECK(g_events.size() == 1 && g_events[0] == "refresh");
  CHECK(pSnapin->m_root.m_pLastObj == static_cast<CSnapInObjectRootBase*>(pSnapin));
  CHECK(pSnapin->m_root.m_pLastObj->m_nType == 1);
  CHECK(spMenuExt->Command(ID_TASK, spRootData) == S_OK && pSnapin->m_root.m_lLastRange == ID_TASK);
  CHECK(spMenuExt->Command(ID_BIG, spRootData) == S_FALSE);
  CHECK(spMenuExt->Command(12345, spRootData) == S_FALSE);
  CComPtr<IExtendContextMenu> spCompMenu;
  spComponent->QueryInterface(IID_IExtendContextMenu, (void**)&spCompMenu);
  CHECK(spCompMenu->Command(ID_REFRESH, spCompRoot) == S_OK);
  CHECK(pSnapin->m_root.m_pLastObj != NULL && pSnapin->m_root.m_pLastObj->m_nType == 2);

  // The toolbar: made when the node is first selected, attached, its
  // buttons set; a click is a command.
  CComObject<CMockControlbar>* pBar = NULL;
  CComPtr<CMockControlbar> spBar = Make<CMockControlbar>(&pBar);
  CComPtr<IExtendControlbar> spBarExt;
  spComponent->QueryInterface(IID_IExtendControlbar, (void**)&spBarExt);
  CHECK(spBarExt->SetControlbar(pBar) == S_OK);
  CHECK(spBarExt->ControlbarNotify(MMCN_SELECT, MAKELONG(TRUE, TRUE), (LPARAM)spCompRoot.p) == S_OK);
  CHECK(pBar->m_nCreated == 1 && pBar->m_nAttached == 1);
  CMockToolbar* pTb = pBar->m_pToolbar;
  CHECK(pTb != NULL);
  if (pTb != NULL) {
    CHECK(pTb->m_nImages == 2 && pTb->m_cx == 16 && pTb->m_cy == 15);
    CHECK(pTb->m_buttons.size() == 3);
    if (pTb->m_buttons.size() == 3) {
      CHECK(pTb->m_buttons[0].idCommand == ID_REFRESH && pTb->m_buttons[0].nBitmap == 0);
      CHECK(pTb->m_buttons[0].strText == L"Refresh" && pTb->m_buttons[0].strTip == L"Refresh now");
      CHECK(pTb->m_buttons[1].fsType == TBSTYLE_SEP);
      CHECK(pTb->m_buttons[2].idCommand == ID_TASK && pTb->m_buttons[2].nBitmap == 1);
      CHECK(pTb->m_buttons[2].strTip == L"Do the task");
    }
    bool bRefreshEnabled = false, bTaskChecked = false, bRefreshChecked = false;
    for (auto& p : pTb->m_set) {
      if (p.first == ID_REFRESH && p.second == ENABLED) bRefreshEnabled = true;
      if (p.first == ID_REFRESH && p.second == CHECKED) bRefreshChecked = true;
      if (p.first == ID_TASK && p.second == CHECKED) bTaskChecked = true;
    }
    CHECK(bRefreshEnabled && bTaskChecked && !bRefreshChecked);
  }
  // Selected again: the same toolbar.
  CHECK(spBarExt->ControlbarNotify(MMCN_SELECT, MAKELONG(TRUE, TRUE), (LPARAM)spCompRoot.p) == S_OK);
  CHECK(pBar->m_nCreated == 1 && pBar->m_nAttached == 2);
  CHECK(spBarExt->ControlbarNotify(MMCN_SELECT, MAKELONG(TRUE, FALSE), (LPARAM)spCompRoot.p) == S_OK);
  CHECK(pBar->m_nDetached == 1);
  g_events.clear();
  CHECK(spBarExt->ControlbarNotify(MMCN_BTN_CLICK, (LPARAM)spCompRoot.p, ID_REFRESH) == S_OK);
  CHECK(g_events.size() == 1 && g_events[0] == "refresh");
  CHECK(spBarExt->SetControlbar(NULL) == S_OK);

  // The property page: added, shown in a sheet and applied.
  CComPtr<IExtendPropertySheet> spSheetExt;
  spData->QueryInterface(IID_IExtendPropertySheet, (void**)&spSheetExt);
  CHECK(spSheetExt->QueryPagesFor(spRootData) == S_OK);
  CHECK(spSheetExt->QueryPagesFor(spRootData2) == S_FALSE);
  CComObject<CMockSheet>* pSheet = NULL;
  CComPtr<CMockSheet> spSheet = Make<CMockSheet>(&pSheet);
  CHECK(spSheetExt->CreatePropertyPages(pSheet, 0, spRootData) == S_OK);
  CHECK(pSheet->m_pages.size() == 1);
  if (pSheet->m_pages.size() == 1) {
    PROPSHEETHEADER psh;
    memset(&psh, 0, sizeof(psh));
    psh.dwSize = sizeof(psh);
    psh.dwFlags = PSH_MODELESS;
    psh.pszCaption = _T("Root Properties");
    psh.nPages = 1;
    psh.phpage = &pSheet->m_pages[0];
    HWND hSheet = (HWND)PropertySheet(&psh);
    CHECK(hSheet != NULL);
    if (hSheet != NULL) {
      HWND hPage = PropSheet_GetCurrentPageHwnd(hSheet);
      CHECK(hPage != NULL);
      TCHAR sz[32] = _T("");
      GetDlgItemText(hPage, IDC_NAME, sz, 32);
      CHECK(_tcscmp(sz, _T("Root")) == 0);
      // Applied as OK would: the page's OnApply.
      PropSheet_PressButton(hSheet, PSBTN_OK);
      MSG msg;
      for (int i = 0; i < 50 && PropSheet_GetCurrentPageHwnd(hSheet) != NULL; i++) {
        while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
          if (!PropSheet_IsDialogMessage(hSheet, &msg)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
          }
        }
        Sleep(10);
      }
      CHECK(CNamePage::s_nApplied == 1);
      DestroyWindow(hSheet);
      while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) DispatchMessage(&msg);
    }
    CHECK(CNamePage::s_nDeleted == 1);
  }

  // An extension of the root node.
  CComObject<CTestExtension>* pExt = NULL;
  CComPtr<CTestExtension> spExtObj = Make<CTestExtension>(&pExt);
  CComPtr<IExtendContextMenu> spExtMenu;
  pExt->QueryInterface(IID_IExtendContextMenu, (void**)&spExtMenu);
  pMenu->m_items.clear();
  lAllowed = CCM_INSERTIONALLOWED_TOP;
  CHECK(spExtMenu->AddMenuItems(spRootData, pMenu, &lAllowed) == S_OK);
  CHECK(pMenu->m_items.size() == 1 && pMenu->m_items[0].lID == ID_EXTCMD);
  CHECK(pExt->m_CExtNode.m_pDataObject == spRootData.p);
  g_events.clear();
  CHECK(spExtMenu->Command(ID_EXTCMD, spRootData) == S_OK);
  CHECK(g_events.size() == 1 && g_events[0] == "extension");

  CHECK(spComponent->Destroy(0) == S_OK);
  spComponent.Release();
  CHECK(spData->Destroy() == S_OK);
  CHECK(pSnapin->m_spConsole == NULL);
}

int main() {
  ::CoInitialize(NULL);
  INITCOMMONCONTROLSEX icc = {sizeof(icc), ICC_WIN95_CLASSES};
  InitCommonControlsEx(&icc);
  TestSnapin();
  CHECK(_Module.GetLockCount() == 0);
  ::CoUninitialize();
  if (g_nFailures == 0) printf("ALL PASSED\n");
  return g_nFailures != 0;
}
