// bindtest.cpp - data binding of hosted ActiveX controls. The program is
// the server of two controls and their container:
//   - a data source control (CDscCtrl) is an OLE DB data source
//     (DataSource) whose default data member is an in-memory table of the
//     test's own, with a row position, notifications and changes;
//   - bound controls (CBoundCtrl) have the stock Text property, an Age
//     property and a DataSource property;
//   - CWnd::BindDefaultProperty binds Text and Age to fields: the
//     properties follow the current row and changes made to the table,
//     and changes to the properties go to the table and to the other
//     controls bound to the same field; BindProperty gives DataSource
//     the data source; GetDSCCursor gives the rowset; unbinding and
//     destroying either side leaves nothing behind;
//   - a dialog template's DLGINIT record binds a control to the data
//     source control next to it, as the dialog editor writes it;
//   - an older data source control (CVBDscCtrl, IVBDSC) makes a cursor
//     (ICursorMove, ICursorUpdateARow) over the same rows, and tells of
//     its changes through INotifyDBEvents: the bindings work the same,
//     and each step of a change is an ON_DSCNOTIFY notification, which
//     can refuse it;
//   - ON_DSCNOTIFY hears of the OLE DB data source's row moving too.
#include <afxwin.h>
#include <afxdisp.h>
#include <afxctl.h>
#include <afxocc.h>
#include <olectl.h>
#include <stddef.h>
#include <oledb.h>
#include <oledberr.h>
#include <msdatsrc.h>
#include "bindtest.h"
#include "testutil.h"

#define IDC_DSC 2001
#define IDC_BOUND1 2002
#define IDC_BOUND2 2003
#define IDC_VBDSC 2004

#define DISPID_AGE 1
#define DISPID_DATASOURCE 2

// {7C0FFAB3-CD84-11D0-949A-00A0C91110ED}
static const IID kIID_DataSource = {
    0x7c0ffab3, 0xcd84, 0x11d0, {0x94, 0x9a, 0x00, 0xa0, 0xc9, 0x11, 0x10, 0xed}};

// {5B8E2C11-6A4F-4D2B-9E31-7A0C4F6B2D01}
static const IID IID_DDscCtrl = {
    0x5b8e2c11, 0x6a4f, 0x4d2b, {0x9e, 0x31, 0x7a, 0x0c, 0x4f, 0x6b, 0x2d, 0x01}};
static const IID IID_DDscCtrlEvents = {
    0x5b8e2c11, 0x6a4f, 0x4d2b, {0x9e, 0x31, 0x7a, 0x0c, 0x4f, 0x6b, 0x2d, 0x02}};
static const IID IID_DBoundCtrl = {
    0x5b8e2c11, 0x6a4f, 0x4d2b, {0x9e, 0x31, 0x7a, 0x0c, 0x4f, 0x6b, 0x2d, 0x03}};
static const IID IID_DBoundCtrlEvents = {
    0x5b8e2c11, 0x6a4f, 0x4d2b, {0x9e, 0x31, 0x7a, 0x0c, 0x4f, 0x6b, 0x2d, 0x04}};

static const DWORD kMiscStatus = OLEMISC_ACTIVATEWHENVISIBLE | OLEMISC_SETCLIENTSITEFIRST |
                                 OLEMISC_INSIDEOUT | OLEMISC_CANTLINKINSIDE |
                                 OLEMISC_RECOMPOSEONRESIZE;

// Whether the next data source gives a row position, or only a rowset.
static BOOL g_bRowPosition = TRUE;

// =====================================================================
// The table: a rowset with a row position, two columns (Name, Age) and
// three rows, whose row handles are 1 to 3.

class CFakeCP : public IConnectionPoint {
public:
  CFakeCP(IConnectionPointContainer* pCPC, REFIID iid) : m_pCPC(pCPC), m_iid(iid), m_pSink(NULL) {}
  STDMETHOD(QueryInterface)(REFIID iid, void** ppv) {
    if (::IsEqualIID(iid, IID_IUnknown) || ::IsEqualIID(iid, IID_IConnectionPoint)) {
      *ppv = this;
      AddRef();
      return S_OK;
    }
    *ppv = NULL;
    return E_NOINTERFACE;
  }
  STDMETHOD_(ULONG, AddRef)() { return m_pCPC->AddRef(); }
  STDMETHOD_(ULONG, Release)() { return m_pCPC->Release(); }
  STDMETHOD(GetConnectionInterface)(IID* piid) {
    *piid = m_iid;
    return S_OK;
  }
  STDMETHOD(GetConnectionPointContainer)(IConnectionPointContainer** pp) {
    *pp = m_pCPC;
    m_pCPC->AddRef();
    return S_OK;
  }
  STDMETHOD(Advise)(IUnknown* pUnk, DWORD* pdwCookie) {
    if (m_pSink != NULL) return CONNECT_E_ADVISELIMIT;
    if (FAILED(pUnk->QueryInterface(m_iid, (void**)&m_pSink))) return CONNECT_E_CANNOTCONNECT;
    *pdwCookie = 1;
    return S_OK;
  }
  STDMETHOD(Unadvise)(DWORD dwCookie) {
    if (dwCookie != 1 || m_pSink == NULL) return CONNECT_E_NOCONNECTION;
    IUnknown* p = m_pSink;
    m_pSink = NULL;
    p->Release();
    return S_OK;
  }
  STDMETHOD(EnumConnections)(IEnumConnections**) { return E_NOTIMPL; }

  IConnectionPointContainer* m_pCPC;
  IID m_iid;
  IUnknown* m_pSink;
};

class CFakeTable : public IRowset,
                   public IAccessor,
                   public IColumnsInfo,
                   public IRowsetChange,
                   public IRowPosition,
                   public IConnectionPointContainer {
public:
  enum { kRows = 3, kColumns = 2 };
  static int s_nAlive;

  LONG m_cRef;
  VARIANT m_data[kRows][kColumns];
  HROW m_hRowPos;
  int m_nRowRefs;  // row references the consumer holds
  CPtrArray m_accessors;
  int m_nAccessors;  // live accessors
  int m_nSetData;
  BOOL m_bRowPosition;
  CFakeCP m_cpRowPos, m_cpNotify;

  CFakeTable()
      : m_cRef(1),
        m_hRowPos(1),
        m_nRowRefs(0),
        m_nAccessors(0),
        m_nSetData(0),
        m_bRowPosition(g_bRowPosition),
        m_cpRowPos(this, IID_IRowPositionChange),
        m_cpNotify(this, IID_IRowsetNotify) {
    s_nAlive++;
    static const WCHAR* names[kRows] = {L"Ann", L"Bob", L"Cy"};
    static const long ages[kRows] = {31, 42, 0};
    for (int r = 0; r < kRows; r++) {
      m_data[r][0].vt = VT_BSTR;
      m_data[r][0].bstrVal = ::SysAllocString(names[r]);
      ::VariantInit(&m_data[r][1]);
      if (r < 2) {
        m_data[r][1].vt = VT_I4;
        m_data[r][1].lVal = ages[r];
      } else {
        m_data[r][1].vt = VT_NULL;  // Cy's age is not known
      }
    }
  }
  virtual ~CFakeTable() {
    for (int r = 0; r < kRows; r++)
      for (int c = 0; c < kColumns; c++) ::VariantClear(&m_data[r][c]);
    for (int i = 0; i < m_accessors.GetSize(); i++) delete (DBBINDING*)m_accessors[i];
    s_nAlive--;
  }

  VARIANT& Field(HROW hRow, DBORDINAL nColumn) { return m_data[hRow - 1][nColumn - 1]; }
  CString Name(int nRow) { return CString(m_data[nRow][0].bstrVal); }
  long Age(int nRow) { return m_data[nRow][1].vt == VT_I4 ? m_data[nRow][1].lVal : -1; }

  // What another consumer does: the table changes the field and tells.
  void ChangeAge(HROW hRow, long nAge) {
    VARIANT& v = Field(hRow, 2);
    ::VariantClear(&v);
    v.vt = VT_I4;
    v.lVal = nAge;
    IRowsetNotify* pSink = (IRowsetNotify*)m_cpNotify.m_pSink;
    DBORDINAL col = 2;
    if (pSink != NULL)
      pSink->OnFieldChange(this, hRow, 1, &col, DBREASON_COLUMN_SET, DBEVENTPHASE_DIDEVENT, TRUE);
  }
  void MoveTo(HROW hRow) { SetRowPosition(DB_NULL_HCHAPTER, hRow, DBPOSITION_OK); }

  // IUnknown
  STDMETHOD(QueryInterface)(REFIID iid, void** ppv) {
    if (::IsEqualIID(iid, IID_IUnknown) || ::IsEqualIID(iid, IID_IRowset))
      *ppv = static_cast<IRowset*>(this);
    else if (::IsEqualIID(iid, IID_IAccessor))
      *ppv = static_cast<IAccessor*>(this);
    else if (::IsEqualIID(iid, IID_IColumnsInfo))
      *ppv = static_cast<IColumnsInfo*>(this);
    else if (::IsEqualIID(iid, IID_IRowsetChange))
      *ppv = static_cast<IRowsetChange*>(this);
    else if (::IsEqualIID(iid, IID_IRowPosition) && m_bRowPosition)
      *ppv = static_cast<IRowPosition*>(this);
    else if (::IsEqualIID(iid, IID_IConnectionPointContainer))
      *ppv = static_cast<IConnectionPointContainer*>(this);
    else {
      *ppv = NULL;
      return E_NOINTERFACE;
    }
    AddRef();
    return S_OK;
  }
  STDMETHOD_(ULONG, AddRef)() { return (ULONG)++m_cRef; }
  STDMETHOD_(ULONG, Release)() {
    LONG c = --m_cRef;
    if (c == 0) delete this;
    return (ULONG)c;
  }

  // IRowset
  STDMETHOD(AddRefRows)(DBCOUNTITEM cRows, const HROW[], DBREFCOUNT[], DBROWSTATUS[]) {
    m_nRowRefs += (int)cRows;
    return S_OK;
  }
  STDMETHOD(GetData)(HROW hRow, HACCESSOR hAccessor, void* pData) {
    if (hRow < 1 || hRow > kRows) return DB_E_BADROWHANDLE;
    DBBINDING* pBinding = GetBinding(hAccessor);
    if (pBinding == NULL) return DB_E_BADACCESSORHANDLE;
    VARIANT* pVar = (VARIANT*)((BYTE*)pData + pBinding->obValue);
    DBSTATUS* pStatus = (DBSTATUS*)((BYTE*)pData + pBinding->obStatus);
    VARIANT& v = Field(hRow, pBinding->iOrdinal);
    ::VariantInit(pVar);
    if (v.vt == VT_NULL) {
      *pStatus = DBSTATUS_S_ISNULL;
    } else {
      ::VariantCopy(pVar, &v);
      *pStatus = DBSTATUS_S_OK;
    }
    return S_OK;
  }
  STDMETHOD(GetNextRows)(HCHAPTER, DBROWOFFSET lOffset, DBROWCOUNT cRows, DBCOUNTITEM* pcGot,
                         HROW** prghRows) {
    // Enough for the first rows from the start.
    DBCOUNTITEM n = 0;
    for (DBROWCOUNT i = 0; i < cRows && lOffset + i < kRows; i++) (*prghRows)[n++] = lOffset + i + 1;
    *pcGot = n;
    m_nRowRefs += (int)n;
    return n == (DBCOUNTITEM)cRows ? S_OK : DB_S_ENDOFROWSET;
  }
  STDMETHOD(ReleaseRows)(DBCOUNTITEM cRows, const HROW[], DBROWOPTIONS[], DBREFCOUNT[],
                         DBROWSTATUS[]) {
    m_nRowRefs -= (int)cRows;
    return S_OK;
  }
  STDMETHOD(RestartPosition)(HCHAPTER) { return S_OK; }

  // IAccessor
  DBBINDING* GetBinding(HACCESSOR h) {
    if (h < 1 || h > (HACCESSOR)m_accessors.GetSize()) return NULL;
    return (DBBINDING*)m_accessors[h - 1];
  }
  STDMETHOD(AddRefAccessor)(HACCESSOR, DBREFCOUNT*) { return E_NOTIMPL; }
  STDMETHOD(CreateAccessor)(DBACCESSORFLAGS dwFlags, DBCOUNTITEM cBindings,
                            const DBBINDING rgBindings[], DBLENGTH, HACCESSOR* phAccessor,
                            DBBINDSTATUS rgStatus[]) {
    if (dwFlags != DBACCESSOR_ROWDATA || cBindings != 1 || rgBindings[0].wType != DBTYPE_VARIANT ||
        rgBindings[0].iOrdinal < 1 || rgBindings[0].iOrdinal > kColumns)
      return DB_E_ERRORSOCCURRED;
    if (rgStatus != NULL) rgStatus[0] = DBBINDSTATUS_OK;
    DBBINDING* p = new DBBINDING(rgBindings[0]);
    *phAccessor = (HACCESSOR)m_accessors.Add(p) + 1;
    m_nAccessors++;
    return S_OK;
  }
  STDMETHOD(GetBindings)(HACCESSOR, DBACCESSORFLAGS*, DBCOUNTITEM*, DBBINDING**) {
    return E_NOTIMPL;
  }
  STDMETHOD(ReleaseAccessor)(HACCESSOR h, DBREFCOUNT* pcRef) {
    if (GetBinding(h) == NULL) return DB_E_BADACCESSORHANDLE;
    m_nAccessors--;
    if (pcRef != NULL) *pcRef = 0;
    return S_OK;
  }

  // IColumnsInfo
  STDMETHOD(GetColumnInfo)(DBORDINAL* pcColumns, DBCOLUMNINFO** prgInfo, OLECHAR** ppStrings) {
    static const WCHAR names[] = L"Name\0Age\0";
    OLECHAR* pStrings = (OLECHAR*)::CoTaskMemAlloc(sizeof(names));
    memcpy(pStrings, names, sizeof(names));
    DBCOLUMNINFO* pInfo = (DBCOLUMNINFO*)::CoTaskMemAlloc(kColumns * sizeof(DBCOLUMNINFO));
    memset(pInfo, 0, kColumns * sizeof(DBCOLUMNINFO));
    pInfo[0].pwszName = pStrings;
    pInfo[0].iOrdinal = 1;
    pInfo[0].wType = DBTYPE_BSTR;
    pInfo[1].pwszName = pStrings + 5;
    pInfo[1].iOrdinal = 2;
    pInfo[1].wType = DBTYPE_I4;
    *pcColumns = kColumns;
    *prgInfo = pInfo;
    *ppStrings = pStrings;
    return S_OK;
  }
  STDMETHOD(MapColumnIDs)(DBORDINAL, const DBID[], DBORDINAL[]) { return E_NOTIMPL; }

  // IRowsetChange
  STDMETHOD(DeleteRows)(HCHAPTER, DBCOUNTITEM, const HROW[], DBROWSTATUS[]) { return E_NOTIMPL; }
  STDMETHOD(SetData)(HROW hRow, HACCESSOR hAccessor, void* pData) {
    if (hRow < 1 || hRow > kRows) return DB_E_BADROWHANDLE;
    DBBINDING* pBinding = GetBinding(hAccessor);
    if (pBinding == NULL) return DB_E_BADACCESSORHANDLE;
    m_nSetData++;
    VARIANT* pVar = (VARIANT*)((BYTE*)pData + pBinding->obValue);
    DBSTATUS status = *(DBSTATUS*)((BYTE*)pData + pBinding->obStatus);
    VARIANT& v = Field(hRow, pBinding->iOrdinal);
    ::VariantClear(&v);
    if (status == DBSTATUS_S_ISNULL)
      v.vt = VT_NULL;
    else
      ::VariantChangeType(&v, pVar, 0, pBinding->iOrdinal == 1 ? VT_BSTR : VT_I4);
    // The consumer that set it is told too, as every listener is.
    IRowsetNotify* pSink = (IRowsetNotify*)m_cpNotify.m_pSink;
    DBORDINAL col = pBinding->iOrdinal;
    if (pSink != NULL)
      pSink->OnFieldChange(this, hRow, 1, &col, DBREASON_COLUMN_SET, DBEVENTPHASE_DIDEVENT, TRUE);
    return S_OK;
  }
  STDMETHOD(InsertRow)(HCHAPTER, HACCESSOR, void*, HROW*) { return E_NOTIMPL; }

  // IRowPosition
  STDMETHOD(ClearRowPosition)() {
    m_hRowPos = DB_NULL_HROW;
    return S_OK;
  }
  STDMETHOD(GetRowPosition)(HCHAPTER* phChapter, HROW* phRow, DBPOSITIONFLAGS* pdwFlags) {
    *phChapter = DB_NULL_HCHAPTER;
    *phRow = m_hRowPos;
    *pdwFlags = m_hRowPos == DB_NULL_HROW ? DBPOSITION_NOROW : DBPOSITION_OK;
    if (m_hRowPos != DB_NULL_HROW) m_nRowRefs++;
    return S_OK;
  }
  STDMETHOD(GetRowset)(REFIID iid, IUnknown** pp) { return QueryInterface(iid, (void**)pp); }
  STDMETHOD(Initialize)(IUnknown*) { return S_OK; }
  STDMETHOD(SetRowPosition)(HCHAPTER, HROW hRow, DBPOSITIONFLAGS) {
    m_hRowPos = hRow;
    IRowPositionChange* pSink = (IRowPositionChange*)m_cpRowPos.m_pSink;
    if (pSink != NULL)
      pSink->OnRowPositionChange(DBREASON_ROWPOSITION_CHANGED, DBEVENTPHASE_DIDEVENT, TRUE);
    return S_OK;
  }

  // IConnectionPointContainer
  STDMETHOD(EnumConnectionPoints)(IEnumConnectionPoints**) { return E_NOTIMPL; }
  STDMETHOD(FindConnectionPoint)(REFIID iid, IConnectionPoint** pp) {
    CFakeCP* pCP = ::IsEqualIID(iid, IID_IRowsetNotify)                         ? &m_cpNotify
                   : ::IsEqualIID(iid, IID_IRowPositionChange) && m_bRowPosition ? &m_cpRowPos
                                                                                 : NULL;
    *pp = pCP;
    if (pCP == NULL) return CONNECT_E_NOCONNECTION;
    pCP->AddRef();
    return S_OK;
  }
};
int CFakeTable::s_nAlive = 0;

// =====================================================================
// The data source control

class CDscCtrl : public COleControl {
  DECLARE_DYNCREATE(CDscCtrl)
public:
  static CDscCtrl* s_pLast;
  static int s_nAlive;
  CFakeTable* m_pTable;
  int m_nGetDataMember;

  CDscCtrl() : m_pTable(new CFakeTable), m_nGetDataMember(0) {
    InitializeIIDs(&IID_DDscCtrl, &IID_DDscCtrlEvents);
    s_pLast = this;
    s_nAlive++;
  }
  virtual ~CDscCtrl() {
    m_pTable->Release();
    if (s_pLast == this) s_pLast = NULL;
    s_nAlive--;
  }
  virtual void OnDraw(CDC* pDC, const CRect& rcBounds, const CRect&) {
    pDC->FillSolidRect(rcBounds, RGB(200, 200, 200));
  }

  BEGIN_INTERFACE_PART(DataSrc, DataSource)
    STDMETHOD(getDataMember)(DataMember bstrDM, REFIID riid, IUnknown** ppunk);
    STDMETHOD(getDataMemberName)(long lIndex, DataMember* pbstrDM);
    STDMETHOD(getDataMemberCount)(long* plCount);
    STDMETHOD(addDataSourceListener)(DataSourceListener* pDSL);
    STDMETHOD(removeDataSourceListener)(DataSourceListener* pDSL);
  END_INTERFACE_PART(DataSrc)

  DECLARE_INTERFACE_MAP()
  DECLARE_OLECREATE_EX(CDscCtrl)
  DECLARE_OLECTLTYPE(CDscCtrl)
  DECLARE_DISPATCH_MAP()
  DECLARE_EVENT_MAP()
};
CDscCtrl* CDscCtrl::s_pLast = NULL;
int CDscCtrl::s_nAlive = 0;

IMPLEMENT_DYNCREATE(CDscCtrl, COleControl)
IMPLEMENT_OLECREATE_EX(CDscCtrl, "BindTest.Dsc.1", 0x5b8e2c11, 0x6a4f, 0x4d2b, 0x9e, 0x31, 0x7a,
                       0x0c, 0x4f, 0x6b, 0x2d, 0x00)
IMPLEMENT_OLECTLTYPE(CDscCtrl, 0, kMiscStatus)
BOOL CDscCtrl::CDscCtrlFactory::UpdateRegistry(BOOL) { return TRUE; }

BEGIN_DISPATCH_MAP(CDscCtrl, COleControl)
END_DISPATCH_MAP()
BEGIN_EVENT_MAP(CDscCtrl, COleControl)
END_EVENT_MAP()
BEGIN_INTERFACE_MAP(CDscCtrl, COleControl)
  INTERFACE_PART(CDscCtrl, kIID_DataSource, DataSrc)
END_INTERFACE_MAP()

STDMETHODIMP_(ULONG) CDscCtrl::XDataSrc::AddRef() {
  METHOD_PROLOGUE_EX_(CDscCtrl, DataSrc)
  return pThis->ExternalAddRef();
}
STDMETHODIMP_(ULONG) CDscCtrl::XDataSrc::Release() {
  METHOD_PROLOGUE_EX_(CDscCtrl, DataSrc)
  return pThis->ExternalRelease();
}
STDMETHODIMP CDscCtrl::XDataSrc::QueryInterface(REFIID iid, void** ppv) {
  METHOD_PROLOGUE_EX_(CDscCtrl, DataSrc)
  return (HRESULT)pThis->ExternalQueryInterface(&iid, ppv);
}
STDMETHODIMP CDscCtrl::XDataSrc::getDataMember(DataMember bstrDM, REFIID riid, IUnknown** ppunk) {
  METHOD_PROLOGUE_EX_(CDscCtrl, DataSrc)
  pThis->m_nGetDataMember++;
  *ppunk = NULL;
  if (bstrDM != NULL && bstrDM[0] != 0) return E_INVALIDARG;  // only the default member
  return pThis->m_pTable->QueryInterface(riid, (void**)ppunk);
}
STDMETHODIMP CDscCtrl::XDataSrc::getDataMemberName(long, DataMember*) { return E_NOTIMPL; }
STDMETHODIMP CDscCtrl::XDataSrc::getDataMemberCount(long* plCount) {
  *plCount = 1;
  return S_OK;
}
STDMETHODIMP CDscCtrl::XDataSrc::addDataSourceListener(DataSourceListener*) { return S_OK; }
STDMETHODIMP CDscCtrl::XDataSrc::removeDataSourceListener(DataSourceListener*) { return S_OK; }

// =====================================================================
// The older kind of data source control (CVBDscCtrl): IVBDSC makes a
// cursor over the same three rows, which tells of its changes through
// the control's INotifyDBEvents connection point.

class CVBDscCtrl;

static BOOL SameColumn(const DBCOLUMNID& a, const DBCOLUMNID& b) {
  if (a.dwKind != b.dwKind) return FALSE;
  if (a.dwKind != DBCOLKIND_NAME && !::IsEqualGUID(a.guid, b.guid)) return FALSE;
  if (a.dwKind == DBCOLKIND_GUID_NUMBER) return a.lNumber == b.lNumber;
  return ::lstrcmpW(a.lpdbsz, b.lpdbsz) == 0;
}

// {5B8E2C11-6A4F-4D2B-9E31-7A0C4F6B2D10}: the Name column's GUID.
static const GUID kGUID_Columns = {
    0x5b8e2c11, 0x6a4f, 0x4d2b, {0x9e, 0x31, 0x7a, 0x0c, 0x4f, 0x6b, 0x2d, 0x10}};

// The data columns: Name by GUID and name, Age by GUID and number.
static DBCOLUMNID DataColumn(int nCol) {
  DBCOLUMNID id;
  memset(&id, 0, sizeof(id));
  id.guid = kGUID_Columns;
  if (nCol == 0) {
    id.dwKind = DBCOLKIND_GUID_NAME;
    id.lpdbsz = (LPDBSTR)L"NameColumn";
  } else {
    id.dwKind = DBCOLKIND_GUID_NUMBER;
    id.lNumber = 2;
  }
  return id;
}

// The columns cursor: a row a column, with its column ID and name.
class CFakeColumns : public ICursor {
public:
  CFakeColumns() : m_cRef(1), m_nBindings(0), m_cbRow(0) {}
  STDMETHOD(QueryInterface)(REFIID iid, void** ppv) {
    if (::IsEqualIID(iid, IID_IUnknown) || ::IsEqualIID(iid, IID_ICursor)) {
      *ppv = static_cast<ICursor*>(this);
      AddRef();
      return S_OK;
    }
    *ppv = NULL;
    return E_NOINTERFACE;
  }
  STDMETHOD_(ULONG, AddRef)() { return ++m_cRef; }
  STDMETHOD_(ULONG, Release)() {
    ULONG c = --m_cRef;
    if (c == 0) delete this;
    return c;
  }
  STDMETHOD(GetColumnsCursor)(REFIID, IUnknown**, ULONG_PTR*) { return E_NOTIMPL; }
  STDMETHOD(SetBindings)(ULONG_PTR cCol, DBCOLUMNBINDING rg[], ULONG_PTR cbRow, DWORD) {
    if (cCol > 4) return E_INVALIDARG;
    m_nBindings = cCol;
    for (ULONG_PTR i = 0; i < cCol; i++) m_bindings[i] = rg[i];
    m_cbRow = cbRow;
    return S_OK;
  }
  STDMETHOD(GetBindings)(ULONG_PTR*, DBCOLUMNBINDING*[], ULONG_PTR*) { return E_NOTIMPL; }
  STDMETHOD(GetNextRows)(LARGE_INTEGER, DBFETCHROWS* pFetch) {
    if (!(pFetch->dwFlags & DBROWFETCH_CALLEEALLOCATES)) return E_INVALIDARG;
    static const char* const kNames[2] = {"Name", "Age"};
    BYTE* pData = (BYTE*)::CoTaskMemAlloc(m_cbRow * 2);
    BYTE* pVar = (BYTE*)::CoTaskMemAlloc(256);
    memset(pData, 0, m_cbRow * 2);
    BYTE* pNext = pVar;
    for (int nRow = 0; nRow < 2; nRow++) {
      BYTE* pRow = pData + m_cbRow * nRow;
      for (ULONG_PTR i = 0; i < m_nBindings; i++) {
        const DBCOLUMNBINDING& b = m_bindings[i];
        if (SameColumn(b.columnID, COLUMN_COLUMNID) && b.dwDataType == DBTYPE_COLUMNID) {
          DBCOLUMNID id = DataColumn(nRow);
          if (id.dwKind != DBCOLKIND_GUID_NUMBER) {  // its name goes with the variable data
            size_t cb = (::lstrlenW(id.lpdbsz) + 1) * sizeof(WCHAR);
            memcpy(pNext, id.lpdbsz, cb);
            id.lpdbsz = (LPDBSTR)pNext;
            pNext += cb;
          }
          memcpy(pRow + b.obData, &id, sizeof(id));
        } else if (SameColumn(b.columnID, COLUMN_NAME) && b.dwDataType == VT_LPSTR) {
          size_t cb = strlen(kNames[nRow]) + 1;
          memcpy(pNext, kNames[nRow], cb);
          *(LPSTR*)(pRow + b.obData) = (LPSTR)pNext;
          pNext += cb;
        } else {
          continue;
        }
        if (b.obInfo != DB_NOVALUE) *(DWORD*)(pRow + b.obInfo) = 0;
      }
    }
    pFetch->pData = pData;
    pFetch->pVarData = pVar;
    pFetch->cbVarData = 256;
    pFetch->cRowsReturned = 2;
    return S_OK;
  }
  STDMETHOD(Requery)() { return S_OK; }

  ULONG m_cRef;
  ULONG_PTR m_nBindings;
  DBCOLUMNBINDING m_bindings[4];
  ULONG_PTR m_cbRow;
};

class CFakeCursor : public ICursorMove, public ICursorUpdateARow {
public:
  enum { kRows = 3 };
  CFakeCursor()
      : m_cRef(1),
        m_pCtrl(NULL),
        m_nRow(0),
        m_nBindings(0),
        m_dwEditMode(DBEDITMODE_NONE),
        m_nUpdates(0),
        m_nSetColumns(0) {
    m_strNames[0] = _T("Ann");
    m_strNames[1] = _T("Bob");
    m_strNames[2] = _T("Cy");
    m_nAges[0] = 31;
    m_nAges[1] = 42;
    m_nAges[2] = -1;  // null
    s_nAlive++;
  }
  ~CFakeCursor() { s_nAlive--; }

  STDMETHOD(QueryInterface)(REFIID iid, void** ppv) {
    if (::IsEqualIID(iid, IID_IUnknown) || ::IsEqualIID(iid, IID_ICursor) ||
        ::IsEqualIID(iid, IID_ICursorMove))
      *ppv = static_cast<ICursorMove*>(this);
    else if (::IsEqualIID(iid, IID_ICursorUpdateARow))
      *ppv = static_cast<ICursorUpdateARow*>(this);
    else {
      *ppv = NULL;
      return E_NOINTERFACE;
    }
    AddRef();
    return S_OK;
  }
  STDMETHOD_(ULONG, AddRef)() { return ++m_cRef; }
  STDMETHOD_(ULONG, Release)() {
    ULONG c = --m_cRef;
    if (c == 0) delete this;
    return c;
  }

  // ICursor
  STDMETHOD(GetColumnsCursor)(REFIID riid, IUnknown** ppv, ULONG_PTR* pcRows) {
    CFakeColumns* p = new CFakeColumns;
    HRESULT hr = p->QueryInterface(riid, (void**)ppv);
    p->Release();
    if (pcRows != NULL) *pcRows = 2;
    return hr;
  }
  STDMETHOD(SetBindings)(ULONG_PTR cCol, DBCOLUMNBINDING rg[], ULONG_PTR, DWORD dwFlags) {
    if (dwFlags != DBCOLUMNBINDOPTS_REPLACE || cCol > 4) return E_INVALIDARG;
    m_nBindings = cCol;
    for (ULONG_PTR i = 0; i < cCol; i++) m_bindings[i] = rg[i];
    return S_OK;
  }
  STDMETHOD(GetBindings)(ULONG_PTR*, DBCOLUMNBINDING*[], ULONG_PTR*) { return E_NOTIMPL; }
  STDMETHOD(GetNextRows)(LARGE_INTEGER, DBFETCHROWS*) { return E_NOTIMPL; }
  STDMETHOD(Requery)() { return S_OK; }

  // ICursorMove: only to the current row, read as VARIANTs.
  STDMETHOD(Move)(ULONG cbBookmark, void* pBookmark, LARGE_INTEGER dlOffset,
                  DBFETCHROWS* pFetch) {
    if (cbBookmark != 1 || *(BYTE*)pBookmark != DBBMK_CURRENT || dlOffset.QuadPart != 0)
      return E_INVALIDARG;
    pFetch->cRowsReturned = 0;
    if (m_nRow < 0 || pFetch->cRowsRequested == 0) return S_OK;
    for (ULONG_PTR i = 0; i < m_nBindings; i++) {
      const DBCOLUMNBINDING& b = m_bindings[i];
      if (b.dwBinding != DBBINDING_VARIANT) return E_INVALIDARG;
      VARIANT* pvar = (VARIANT*)((BYTE*)pFetch->pData + b.obData);
      ::VariantInit(pvar);
      int nCol = ColumnOf(b.columnID);
      if (nCol < 0) return E_INVALIDARG;
      if (nCol == 0) {
        pvar->vt = VT_BSTR;
        pvar->bstrVal = m_strNames[m_nRow].AllocSysString();
      } else if (m_nAges[m_nRow] < 0) {
        pvar->vt = VT_NULL;
      } else {
        pvar->vt = VT_I4;
        pvar->lVal = m_nAges[m_nRow];
      }
      if (pvar->vt != VT_NULL && b.dwDataType != VT_VARIANT && b.dwDataType != pvar->vt &&
          FAILED(::VariantChangeType(pvar, pvar, 0, (VARTYPE)b.dwDataType)))
        return E_FAIL;
    }
    pFetch->cRowsReturned = 1;
    return S_OK;
  }
  STDMETHOD(GetBookmark)(DBCOLUMNID*, ULONG, ULONG*, void*) { return E_NOTIMPL; }
  STDMETHOD(Clone)(DWORD, REFIID, IUnknown**) { return E_NOTIMPL; }

  // ICursorUpdateARow
  STDMETHOD(BeginUpdate)(DWORD dwFlags) {
    if (m_dwEditMode != DBEDITMODE_NONE || dwFlags != DBROWACTION_UPDATE) return E_FAIL;
    m_dwEditMode = DBEDITMODE_UPDATE;
    m_strEditName = m_strNames[m_nRow];
    m_nEditAge = m_nAges[m_nRow];
    return S_OK;
  }
  STDMETHOD(SetColumn)(DBCOLUMNID* pcid, DBBINDPARAMS* pParams) {
    if (m_dwEditMode == DBEDITMODE_NONE || pParams->dwBinding != DBBINDING_VARIANT) return E_FAIL;
    m_nSetColumns++;
    int nCol = ColumnOf(*pcid);
    if (nCol < 0) return E_INVALIDARG;
    if (pParams->dwInfo == DB_NULL) {
      if (nCol == 1) m_nEditAge = -1;
      return S_OK;
    }
    COleVariant var(*(VARIANT*)pParams->pData);
    if (nCol == 0) {
      var.ChangeType(VT_BSTR);
      m_strEditName = var.bstrVal;
    } else {
      var.ChangeType(VT_I4);
      m_nEditAge = var.lVal;
    }
    return S_OK;
  }
  STDMETHOD(GetColumn)(DBCOLUMNID*, DBBINDPARAMS*, DWORD*) { return E_NOTIMPL; }
  STDMETHOD(GetEditMode)(DWORD* pdwState) {
    *pdwState = m_dwEditMode;
    return S_OK;
  }
  STDMETHOD(Update)(DBCOLUMNID*, ULONG*, void**) {
    if (m_dwEditMode == DBEDITMODE_NONE) return E_FAIL;
    m_dwEditMode = DBEDITMODE_NONE;
    m_nUpdates++;
    int nRow = m_nRow;
    CString strName = m_strEditName;
    long nAge = m_nEditAge;
    return Change(DBEVENT_CURRENT_ROW_DATA_CHANGED, DBREASON_MODIFIED, [&]() {
      m_strNames[nRow] = strName;
      m_nAges[nRow] = nAge;
    });
  }
  STDMETHOD(Cancel)() {
    m_dwEditMode = DBEDITMODE_NONE;
    return S_OK;
  }
  STDMETHOD(Delete)() { return E_NOTIMPL; }

  // The test's changes.
  HRESULT MoveTo(int nRow) {
    return Change(DBEVENT_CURRENT_ROW_CHANGED, DBREASON_MOVE, [&]() { m_nRow = nRow; });
  }
  HRESULT ChangeAge(int nRow, long nAge) {
    return Change(nRow == m_nRow ? DBEVENT_CURRENT_ROW_DATA_CHANGED
                                 : DBEVENT_NONCURRENT_ROW_DATA_CHANGED,
                  DBREASON_MODIFIED, [&]() { m_nAges[nRow] = nAge; });
  }

  int ColumnOf(const DBCOLUMNID& id) {
    for (int i = 0; i < 2; i++)
      if (SameColumn(id, DataColumn(i))) return i;
    return -1;
  }
  // Each step of a change, told to the control's sinks; any of them can
  // refuse it at OKToDo.
  template <class F>
  HRESULT Change(DWORD dwWhat, DWORD dwReason, F change);

  static int s_nAlive;
  ULONG m_cRef;
  CVBDscCtrl* m_pCtrl;  // NULL once the control goes
  int m_nRow;           // -1: none
  CString m_strNames[kRows];
  long m_nAges[kRows];
  ULONG_PTR m_nBindings;
  DBCOLUMNBINDING m_bindings[4];
  DWORD m_dwEditMode;
  CString m_strEditName;
  long m_nEditAge;
  int m_nUpdates, m_nSetColumns;
};
int CFakeCursor::s_nAlive = 0;

class CVBDscCtrl : public COleControl {
  DECLARE_DYNCREATE(CVBDscCtrl)
public:
  static CVBDscCtrl* s_pLast;
  static int s_nAlive;
  CFakeCursor* m_pCursor;
  int m_nCursors;

  CVBDscCtrl() : m_pCursor(new CFakeCursor), m_nCursors(0) {
    InitializeIIDs(&IID_DDscCtrl, &IID_DDscCtrlEvents);
    m_pCursor->m_pCtrl = this;
    s_pLast = this;
    s_nAlive++;
  }
  virtual ~CVBDscCtrl() {
    m_pCursor->m_pCtrl = NULL;
    m_pCursor->Release();
    if (s_pLast == this) s_pLast = NULL;
    s_nAlive--;
  }
  virtual void OnDraw(CDC* pDC, const CRect& rcBounds, const CRect&) {
    pDC->FillSolidRect(rcBounds, RGB(200, 200, 200));
  }

  BEGIN_INTERFACE_PART(VBDSC, IVBDSC)
    STDMETHOD(CancelUnload)(BOOL* pfCancel);
    STDMETHOD(Error)(DWORD dwErr, BOOL* pfShowError);
    STDMETHOD(CreateCursor)(ICursor** ppCursor);
  END_INTERFACE_PART(VBDSC)

  BEGIN_CONNECTION_PART(CVBDscCtrl, DBEventsCP)
    CONNECTION_IID(IID_INotifyDBEvents)
  END_CONNECTION_PART(DBEventsCP)

  DECLARE_INTERFACE_MAP()
  DECLARE_CONNECTION_MAP()
  DECLARE_OLECREATE_EX(CVBDscCtrl)
  DECLARE_OLECTLTYPE(CVBDscCtrl)
  DECLARE_DISPATCH_MAP()
  DECLARE_EVENT_MAP()
};
CVBDscCtrl* CVBDscCtrl::s_pLast = NULL;
int CVBDscCtrl::s_nAlive = 0;

IMPLEMENT_DYNCREATE(CVBDscCtrl, COleControl)
IMPLEMENT_OLECREATE_EX(CVBDscCtrl, "BindTest.VBDsc.1", 0x5b8e2c11, 0x6a4f, 0x4d2b, 0x9e, 0x31,
                       0x7a, 0x0c, 0x4f, 0x6b, 0x2d, 0x06)
IMPLEMENT_OLECTLTYPE(CVBDscCtrl, 0, kMiscStatus)
BOOL CVBDscCtrl::CVBDscCtrlFactory::UpdateRegistry(BOOL) { return TRUE; }

BEGIN_DISPATCH_MAP(CVBDscCtrl, COleControl)
END_DISPATCH_MAP()
BEGIN_EVENT_MAP(CVBDscCtrl, COleControl)
END_EVENT_MAP()
BEGIN_INTERFACE_MAP(CVBDscCtrl, COleControl)
  INTERFACE_PART(CVBDscCtrl, IID_IVBDSC, VBDSC)
END_INTERFACE_MAP()
BEGIN_CONNECTION_MAP(CVBDscCtrl, COleControl)
  CONNECTION_PART(CVBDscCtrl, IID_INotifyDBEvents, DBEventsCP)
END_CONNECTION_MAP()

STDMETHODIMP_(ULONG) CVBDscCtrl::XVBDSC::AddRef() {
  METHOD_PROLOGUE_EX_(CVBDscCtrl, VBDSC)
  return pThis->ExternalAddRef();
}
STDMETHODIMP_(ULONG) CVBDscCtrl::XVBDSC::Release() {
  METHOD_PROLOGUE_EX_(CVBDscCtrl, VBDSC)
  return pThis->ExternalRelease();
}
STDMETHODIMP CVBDscCtrl::XVBDSC::QueryInterface(REFIID iid, void** ppv) {
  METHOD_PROLOGUE_EX_(CVBDscCtrl, VBDSC)
  return (HRESULT)pThis->ExternalQueryInterface(&iid, ppv);
}
STDMETHODIMP CVBDscCtrl::XVBDSC::CancelUnload(BOOL* pfCancel) {
  *pfCancel = FALSE;
  return S_OK;
}
STDMETHODIMP CVBDscCtrl::XVBDSC::Error(DWORD, BOOL*) { return S_OK; }
STDMETHODIMP CVBDscCtrl::XVBDSC::CreateCursor(ICursor** ppCursor) {
  METHOD_PROLOGUE_EX_(CVBDscCtrl, VBDSC)
  pThis->m_nCursors++;
  *ppCursor = static_cast<ICursorMove*>(pThis->m_pCursor);
  pThis->m_pCursor->AddRef();
  return S_OK;
}

template <class F>
HRESULT CFakeCursor::Change(DWORD dwWhat, DWORD dwReason, F change) {
  DBNOTIFYREASON reason;
  memset(&reason, 0, sizeof(reason));
  reason.dwReason = dwReason;
  const CPtrArray* pSinks = m_pCtrl != NULL ? m_pCtrl->m_xDBEventsCP.GetConnections() : NULL;
  INT_PTR nSinks = pSinks != NULL ? pSinks->GetSize() : 0;
  for (INT_PTR i = 0; i < nSinks; i++) {
    INotifyDBEvents* pSink = (INotifyDBEvents*)pSinks->GetAt(i);
    if (pSink->OKToDo(dwWhat, 1, &reason) != S_OK) {
      for (INT_PTR j = 0; j <= i; j++)
        ((INotifyDBEvents*)pSinks->GetAt(j))->Cancelled(dwWhat, 1, &reason);
      return OCDB_S_OPERATIONCANCELLED;
    }
  }
  for (INT_PTR i = 0; i < nSinks; i++)
    ((INotifyDBEvents*)pSinks->GetAt(i))->AboutToDo(dwWhat, 1, &reason);
  change();
  for (INT_PTR i = 0; i < nSinks; i++)
    ((INotifyDBEvents*)pSinks->GetAt(i))->SyncAfter(dwWhat, 1, &reason);
  for (INT_PTR i = 0; i < nSinks; i++)
    ((INotifyDBEvents*)pSinks->GetAt(i))->DidEvent(dwWhat, 1, &reason);
  return S_OK;
}

// =====================================================================
// The bound control

class CBoundCtrl : public COleControl {
  DECLARE_DYNCREATE(CBoundCtrl)
public:
  static int s_nAlive;
  static CBoundCtrl* s_pLast;
  long m_nAge;
  int m_nAgeSets;
  IUnknown* m_pDataSource;

  CBoundCtrl() : m_nAge(-1), m_nAgeSets(0), m_pDataSource(NULL) {
    InitializeIIDs(&IID_DBoundCtrl, &IID_DBoundCtrlEvents);
    s_pLast = this;
    s_nAlive++;
  }
  virtual ~CBoundCtrl() {
    if (m_pDataSource != NULL) m_pDataSource->Release();
    if (s_pLast == this) s_pLast = NULL;
    s_nAlive--;
  }
  virtual void OnDraw(CDC* pDC, const CRect& rcBounds, const CRect&) {
    pDC->FillSolidRect(rcBounds, RGB(255, 255, 255));
  }
  // No saved properties, so an empty saved state loads.
  virtual void DoPropExchange(CPropExchange*) {}

  long GetAge() { return m_nAge; }
  void SetAge(long nAge) {
    if (!BoundPropertyRequestEdit(DISPID_AGE)) SetNotPermitted();
    m_nAge = nAge;
    m_nAgeSets++;
    BoundPropertyChanged(DISPID_AGE);
  }
  LPUNKNOWN GetDataSource() {
    if (m_pDataSource != NULL) m_pDataSource->AddRef();
    return m_pDataSource;
  }
  void SetDataSource(LPUNKNOWN pUnk) {
    if (pUnk != NULL) pUnk->AddRef();
    if (m_pDataSource != NULL) m_pDataSource->Release();
    m_pDataSource = pUnk;
  }

  DECLARE_OLECREATE_EX(CBoundCtrl)
  DECLARE_OLECTLTYPE(CBoundCtrl)
  DECLARE_DISPATCH_MAP()
  DECLARE_EVENT_MAP()
};
int CBoundCtrl::s_nAlive = 0;
CBoundCtrl* CBoundCtrl::s_pLast = NULL;

IMPLEMENT_DYNCREATE(CBoundCtrl, COleControl)
IMPLEMENT_OLECREATE_EX(CBoundCtrl, "BindTest.Bound.1", 0x5b8e2c11, 0x6a4f, 0x4d2b, 0x9e, 0x31,
                       0x7a, 0x0c, 0x4f, 0x6b, 0x2d, 0x05)
IMPLEMENT_OLECTLTYPE(CBoundCtrl, 0, kMiscStatus)
BOOL CBoundCtrl::CBoundCtrlFactory::UpdateRegistry(BOOL) { return TRUE; }

BEGIN_DISPATCH_MAP(CBoundCtrl, COleControl)
  DISP_PROPERTY_EX_ID(CBoundCtrl, "Age", DISPID_AGE, GetAge, SetAge, VT_I4)
  DISP_PROPERTY_EX_ID(CBoundCtrl, "DataSource", DISPID_DATASOURCE, GetDataSource, SetDataSource,
                      VT_UNKNOWN)
  DISP_STOCKPROP_TEXT()
END_DISPATCH_MAP()
BEGIN_EVENT_MAP(CBoundCtrl, COleControl)
END_EVENT_MAP()

// =====================================================================
// The container

class CMainFrame : public CFrameWnd {
public:
  CWnd m_dsc, m_bound1, m_bound2;
  CBoundCtrl* m_pCtl1;
  CBoundCtrl* m_pCtl2;
  int m_nRequestEdits, m_nAgeChanges;
  // The data source controls' notifications (ON_DSCNOTIFY_RANGE): how
  // many, the last, and the ones a move made.
  int m_nDSCNotifies;
  UINT m_nDSCID;
  DSCSTATE m_nDSCState;
  DSCREASON m_nDSCReason;
  CString m_strMoveSteps;
  BOOL m_bRefuseMoves;

  CMainFrame()
      : m_pCtl1(NULL),
        m_pCtl2(NULL),
        m_nRequestEdits(0),
        m_nAgeChanges(0),
        m_nDSCNotifies(0),
        m_nDSCID(0),
        m_nDSCState(dscNoState),
        m_nDSCReason(dscNoReason),
        m_bRefuseMoves(FALSE) {}

  BOOL OnDSCNotify(UINT nID, DSCSTATE nState, DSCREASON nReason, BOOL* pbAllow) {
    m_nDSCNotifies++;
    m_nDSCID = nID;
    m_nDSCState = nState;
    m_nDSCReason = nReason;
    if (nReason == dscMove) {
      CString str;
      str.Format(_T("%d"), (int)nState);
      m_strMoveSteps += str;
      if (m_bRefuseMoves && nState == dscOKToDo) *pbAllow = FALSE;
    }
    return TRUE;
  }

  BOOL OnRequestEdit(BOOL*) {  // allowed
    m_nRequestEdits++;
    return TRUE;
  }
  BOOL OnAgeChanged() {
    m_nAgeChanges++;
    return TRUE;
  }
  BOOL MakeControls();
  void RunTests();
  void TestSimple();
  void TestComplex();
  void TestUnbind();
  void TestDestroyOrder();
  void TestRowsetOnly();
  void TestDialog();
  void TestCursor();

  afx_msg int OnCreate(LPCREATESTRUCT lpcs) {
    if (CFrameWnd::OnCreate(lpcs) == -1) return -1;
    SetTimer(1, 100, NULL);
    return 0;
  }
  afx_msg void OnTimer(UINT_PTR) {
    KillTimer(1);
    RunTests();
    PostMessage(WM_CLOSE);
  }
  DECLARE_MESSAGE_MAP()
  DECLARE_EVENTSINK_MAP()
};

BEGIN_MESSAGE_MAP(CMainFrame, CFrameWnd)
  ON_WM_CREATE()
  ON_WM_TIMER()
END_MESSAGE_MAP()

BEGIN_EVENTSINK_MAP(CMainFrame, CFrameWnd)
  ON_PROPNOTIFY(CMainFrame, IDC_BOUND1, DISPID_AGE, OnRequestEdit, OnAgeChanged)
  ON_DSCNOTIFY_RANGE(CMainFrame, IDC_DSC, IDC_VBDSC, OnDSCNotify)
END_EVENTSINK_MAP()

static CString TextOf(CWnd& wnd) {
  CString str;
  wnd.GetProperty(DISPID_TEXT, VT_BSTR, &str);
  return str;
}
static long AgeOf(CWnd& wnd) {
  long n = 0;
  wnd.GetProperty(DISPID_AGE, VT_I4, &n);
  return n;
}

BOOL CMainFrame::MakeControls() {
  m_pCtl1 = m_pCtl2 = NULL;
  BOOL bOK = m_dsc.CreateControl(CDscCtrl::guid, NULL, WS_VISIBLE, CRect(10, 10, 60, 30), this,
                                 IDC_DSC);
  bOK = bOK && m_bound1.CreateControl(CBoundCtrl::guid, NULL, WS_VISIBLE, CRect(10, 40, 110, 60),
                                      this, IDC_BOUND1);
  if (bOK) m_pCtl1 = CBoundCtrl::s_pLast;
  bOK = bOK && m_bound2.CreateControl(CBoundCtrl::guid, NULL, WS_VISIBLE, CRect(10, 70, 110, 90),
                                      this, IDC_BOUND2);
  if (bOK) m_pCtl2 = CBoundCtrl::s_pLast;
  bOK = bOK && m_pCtl1 != NULL && m_pCtl2 != NULL && m_pCtl1 != m_pCtl2;
  CHECK(bOK);
  return bOK;
}

void CMainFrame::TestSimple() {
  TestLog("simple binding");
  if (!MakeControls()) return;
  CDscCtrl* pDsc = CDscCtrl::s_pLast;
  CFakeTable* pTable = pDsc->m_pTable;
  CBoundCtrl* pCtl1 = m_pCtl1;

  // Bound: the properties show the current row (the first).
  m_bound1.BindDefaultProperty(DISPID_TEXT, VT_BSTR, _T("Name"), &m_dsc);
  m_bound1.BindDefaultProperty(DISPID_AGE, VT_I4, _T("age"), &m_dsc);  // any case
  m_bound2.BindDefaultProperty(DISPID_TEXT, VT_BSTR, _T("Name"), &m_dsc);
  CHECK(pDsc->m_nGetDataMember == 1);  // one rowset for the data source control
  CHECK(TextOf(m_bound1) == _T("Ann") && AgeOf(m_bound1) == 31 && TextOf(m_bound2) == _T("Ann"));
  CHECK(pTable->m_nAccessors == 3 && pTable->m_nSetData == 0);
  CHECK(pTable->m_cpRowPos.m_pSink != NULL && pTable->m_cpNotify.m_pSink != NULL);

  // The data source control's rowset.
  IUnknown* pCursor = m_dsc.GetDSCCursor();
  CHECK(pCursor == static_cast<IRowset*>(pTable));

  // The current row moves, which ON_DSCNOTIFY hears of.
  m_nDSCNotifies = 0;
  pTable->MoveTo(2);
  CHECK(TextOf(m_bound1) == _T("Bob") && AgeOf(m_bound1) == 42 && TextOf(m_bound2) == _T("Bob"));
  CHECK(m_nDSCNotifies == 1 && m_nDSCID == IDC_DSC && m_nDSCState == dscDidEvent &&
        m_nDSCReason == dscMove);
  // A null field is the property type's empty value.
  pTable->MoveTo(3);
  CHECK(TextOf(m_bound1) == _T("Cy") && AgeOf(m_bound1) == 0);
  // No current row.
  pTable->ClearRowPosition();
  pTable->SetRowPosition(DB_NULL_HCHAPTER, DB_NULL_HROW, DBPOSITION_NOROW);
  CHECK(TextOf(m_bound1).IsEmpty() && AgeOf(m_bound1) == 0);
  pTable->MoveTo(1);
  CHECK(TextOf(m_bound1) == _T("Ann"));
  CHECK(pTable->m_nSetData == 0);  // setting the properties did not write them back

  // The control changes a bound property: the field and the other
  // control bound to it follow.
  pCtl1->SetText(_T("Anne"));
  CHECK(pTable->Name(0) == _T("Anne") && pTable->m_nSetData == 1);
  CHECK(TextOf(m_bound2) == _T("Anne") && TextOf(m_bound1) == _T("Anne"));
  int nRequests = m_nRequestEdits, nChanges = m_nAgeChanges;
  pCtl1->SetAge(33);
  CHECK(pTable->Age(0) == 33 && pTable->m_nSetData == 2);
  // ON_PROPNOTIFY still sees the change.
  CHECK(m_nRequestEdits == nRequests + 1 && m_nAgeChanges == nChanges + 1);
  // The other row is untouched.
  CHECK(pTable->Name(1) == _T("Bob") && pTable->Age(1) == 42);

  // The table changes a field.
  int nSets = pCtl1->m_nAgeSets;
  pTable->ChangeAge(1, 50);
  CHECK(AgeOf(m_bound1) == 50 && pCtl1->m_nAgeSets == nSets + 1);
  CHECK(pTable->m_nSetData == 2);

  // A field the table does not have binds nothing.
  m_bound2.BindDefaultProperty(DISPID_AGE, VT_I4, _T("Height"), &m_dsc);
  CHECK(pTable->m_nAccessors == 3 && AgeOf(m_bound2) == -1);
  // Binding again replaces the binding.
  m_bound1.BindDefaultProperty(DISPID_TEXT, VT_BSTR, _T("Name"), &m_dsc);
  CHECK(pTable->m_nAccessors == 3);
  CHECK(pTable->m_nRowRefs == 0);

  m_bound1.DestroyWindow();
  CHECK(pTable->m_nAccessors == 1);
  pTable->MoveTo(2);
  CHECK(TextOf(m_bound2) == _T("Bob"));
  m_bound2.DestroyWindow();
  CHECK(pTable->m_nAccessors == 0);
  pTable->AddRef();
  m_dsc.DestroyWindow();
  CHECK(pTable->m_cpRowPos.m_pSink == NULL && pTable->m_cpNotify.m_pSink == NULL);
  CHECK(pTable->m_nRowRefs == 0 && pTable->m_cRef == 1);
  pTable->Release();
  CHECK(CFakeTable::s_nAlive == 0 && CDscCtrl::s_nAlive == 0 && CBoundCtrl::s_nAlive == 0);
}

void CMainFrame::TestComplex() {
  TestLog("complex binding");
  if (!MakeControls()) return;
  CBoundCtrl* pCtl1 = m_pCtl1;
  m_bound1.BindProperty(DISPID_DATASOURCE, &m_dsc);
  IUnknown* pDataSource = NULL;
  m_dsc.GetControlUnknown()->QueryInterface(kIID_DataSource, (void**)&pDataSource);
  CHECK(pDataSource != NULL && pCtl1->m_pDataSource == pDataSource);
  if (pDataSource != NULL) pDataSource->Release();
  // The row moving does not touch it.
  CDscCtrl::s_pLast->m_pTable->MoveTo(2);
  CHECK(pCtl1->m_pDataSource == pDataSource);
  // Unbound, it holds nothing.
  m_bound1.BindProperty(DISPID_DATASOURCE, NULL);
  CHECK(pCtl1->m_pDataSource == NULL);
  m_bound1.BindProperty(DISPID_DATASOURCE, &m_dsc);
  CHECK(pCtl1->m_pDataSource == pDataSource);
  m_bound1.SetProperty(DISPID_DATASOURCE, VT_UNKNOWN, (IUnknown*)NULL);  // so the controls can go
  m_bound2.DestroyWindow();
  m_bound1.DestroyWindow();
  m_dsc.DestroyWindow();
  CHECK(CFakeTable::s_nAlive == 0 && CDscCtrl::s_nAlive == 0 && CBoundCtrl::s_nAlive == 0);
}

void CMainFrame::TestUnbind() {
  TestLog("unbinding");
  if (!MakeControls()) return;
  CFakeTable* pTable = CDscCtrl::s_pLast->m_pTable;
  m_bound1.BindDefaultProperty(DISPID_TEXT, VT_BSTR, _T("Name"), &m_dsc);
  CHECK(TextOf(m_bound1) == _T("Ann") && pTable->m_nAccessors == 1);
  m_bound1.BindDefaultProperty(DISPID_TEXT, VT_BSTR, NULL, NULL);
  CHECK(pTable->m_nAccessors == 0);
  pTable->MoveTo(2);
  CHECK(TextOf(m_bound1) == _T("Ann"));
  m_pCtl1->SetText(_T("Free"));
  CHECK(pTable->m_nSetData == 0 && pTable->Name(1) == _T("Bob"));
  m_bound1.DestroyWindow();
  m_bound2.DestroyWindow();
  m_dsc.DestroyWindow();
  CHECK(CFakeTable::s_nAlive == 0 && CDscCtrl::s_nAlive == 0 && CBoundCtrl::s_nAlive == 0);
}

void CMainFrame::TestDestroyOrder() {
  TestLog("data source control destroyed first");
  if (!MakeControls()) return;
  CBoundCtrl* pCtl1 = m_pCtl1;
  m_bound1.BindDefaultProperty(DISPID_TEXT, VT_BSTR, _T("Name"), &m_dsc);
  m_bound1.BindDefaultProperty(DISPID_AGE, VT_I4, _T("Age"), &m_dsc);
  CHECK(TextOf(m_bound1) == _T("Ann"));
  m_dsc.DestroyWindow();
  CHECK(CFakeTable::s_nAlive == 0 && CDscCtrl::s_nAlive == 0);
  CHECK(m_bound1.m_pCtrlSite != NULL && m_bound1.m_pCtrlSite->m_pBindings == NULL);
  // The control's changes go nowhere now.
  pCtl1->SetText(_T("Alone"));
  CHECK(TextOf(m_bound1) == _T("Alone"));
  m_bound1.DestroyWindow();
  m_bound2.DestroyWindow();
  CHECK(CBoundCtrl::s_nAlive == 0);
}

void CMainFrame::TestRowsetOnly() {
  TestLog("data source without a row position");
  g_bRowPosition = FALSE;
  BOOL bMade = MakeControls();
  g_bRowPosition = TRUE;
  if (!bMade) return;
  CFakeTable* pTable = CDscCtrl::s_pLast->m_pTable;
  m_bound1.BindDefaultProperty(DISPID_TEXT, VT_BSTR, _T("Name"), &m_dsc);
  m_bound1.BindDefaultProperty(DISPID_AGE, VT_I4, _T("Age"), &m_dsc);
  // The current row is the first.
  CHECK(TextOf(m_bound1) == _T("Ann") && AgeOf(m_bound1) == 31);
  CHECK(m_dsc.GetDSCCursor() == static_cast<IRowset*>(pTable));
  CHECK(pTable->m_cpRowPos.m_pSink == NULL && pTable->m_cpNotify.m_pSink != NULL);
  m_pCtl1->SetAge(32);
  CHECK(pTable->Age(0) == 32);
  pTable->ChangeAge(1, 35);
  CHECK(AgeOf(m_bound1) == 35);
  CHECK(pTable->m_nRowRefs == 1);  // the first row, which the binding keeps
  m_bound1.DestroyWindow();
  m_bound2.DestroyWindow();
  m_dsc.DestroyWindow();
  CHECK(CFakeTable::s_nAlive == 0 && CDscCtrl::s_nAlive == 0 && CBoundCtrl::s_nAlive == 0);
}

void CMainFrame::TestDialog() {
  TestLog("bindings from a dialog template");
  CDialog dlg;
  CBoundCtrl::s_pLast = NULL;
  CHECK(dlg.Create(IDD_BINDDLG, this));
  CBoundCtrl* pCtl = CBoundCtrl::s_pLast;
  CDscCtrl* pDsc = CDscCtrl::s_pLast;
  CHECK(pCtl != NULL && pDsc != NULL);
  if (pCtl != NULL && pDsc != NULL) {
    // Age follows the field, and DataSource holds the data source.
    CHECK(pCtl->m_nAge == 31);
    IUnknown* pDataSource = NULL;
    pDsc->GetControllingUnknown()->QueryInterface(kIID_DataSource, (void**)&pDataSource);
    CHECK(pDataSource != NULL && pCtl->m_pDataSource == pDataSource);
    if (pDataSource != NULL) pDataSource->Release();
    pDsc->m_pTable->MoveTo(2);
    CHECK(pCtl->m_nAge == 42);
    pCtl->SetAge(43);
    CHECK(pDsc->m_pTable->Age(1) == 43);
    pCtl->SetDataSource(NULL);  // so that the controls can go
  }
  dlg.DestroyWindow();
  CHECK(CFakeTable::s_nAlive == 0 && CDscCtrl::s_nAlive == 0 && CBoundCtrl::s_nAlive == 0);
}

// A data source control that makes a cursor (IVBDSC).
void CMainFrame::TestCursor() {
  TestLog("cursor data source control");
  CWnd vbdsc;
  CHECK(vbdsc.CreateControl(CVBDscCtrl::guid, NULL, WS_VISIBLE, CRect(10, 100, 60, 120), this,
                            IDC_VBDSC));
  CVBDscCtrl* pDsc = CVBDscCtrl::s_pLast;
  if (!MakeControls() || pDsc == NULL) return;
  CFakeCursor* pCursor = pDsc->m_pCursor;
  CBoundCtrl* pCtl1 = m_pCtl1;
  CHECK(pDsc->m_xDBEventsCP.GetConnections()->GetSize() == 1);  // its site's

  m_bound1.BindDefaultProperty(DISPID_TEXT, VT_BSTR, _T("Name"), &vbdsc);
  m_bound1.BindDefaultProperty(DISPID_AGE, VT_I4, _T("age"), &vbdsc);
  m_bound2.BindDefaultProperty(DISPID_TEXT, VT_BSTR, _T("Name"), &vbdsc);
  CHECK(pDsc->m_nCursors == 1);
  CHECK(TextOf(m_bound1) == _T("Ann") && AgeOf(m_bound1) == 31 && TextOf(m_bound2) == _T("Ann"));
  // The cursor, which BindProperty gives too.
  CHECK(vbdsc.GetDSCCursor() == static_cast<ICursor*>(static_cast<ICursorMove*>(pCursor)));
  m_bound2.BindProperty(DISPID_DATASOURCE, &vbdsc);
  CHECK(m_pCtl2->m_pDataSource == static_cast<ICursor*>(static_cast<ICursorMove*>(pCursor)));
  m_bound2.SetProperty(DISPID_DATASOURCE, VT_UNKNOWN, (IUnknown*)NULL);
  m_bound2.BindProperty(DISPID_DATASOURCE, NULL);

  // The current row moves: each step is an ON_DSCNOTIFY notification.
  m_nDSCNotifies = 0;
  m_strMoveSteps.Empty();
  CHECK(pCursor->MoveTo(1) == S_OK);
  CHECK(TextOf(m_bound1) == _T("Bob") && AgeOf(m_bound1) == 42 && TextOf(m_bound2) == _T("Bob"));
  CString strSteps;
  strSteps.Format(_T("%d%d%d%d"), dscOKToDo, dscAboutToDo, dscSyncAfter, dscDidEvent);
  CHECK(m_strMoveSteps == strSteps && m_nDSCID == IDC_VBDSC);
  // A null field is the property type's empty value.
  pCursor->MoveTo(2);
  CHECK(TextOf(m_bound1) == _T("Cy") && AgeOf(m_bound1) == 0);
  // The container refuses a move.
  m_bRefuseMoves = TRUE;
  m_strMoveSteps.Empty();
  CHECK(pCursor->MoveTo(0) == OCDB_S_OPERATIONCANCELLED);
  m_bRefuseMoves = FALSE;
  strSteps.Format(_T("%d%d"), dscOKToDo, dscCancelled);
  CHECK(m_strMoveSteps == strSteps && pCursor->m_nRow == 2 && TextOf(m_bound1) == _T("Cy"));
  pCursor->MoveTo(0);
  CHECK(TextOf(m_bound1) == _T("Ann"));
  CHECK(pCursor->m_nUpdates == 0);

  // The control changes a bound property: an update of the row, which
  // the other control bound to the field follows.
  pCtl1->SetText(_T("Anne"));
  CHECK(pCursor->m_strNames[0] == _T("Anne") && pCursor->m_nUpdates == 1);
  CHECK(pCursor->m_dwEditMode == DBEDITMODE_NONE && pCursor->m_nSetColumns == 1);
  CHECK(TextOf(m_bound2) == _T("Anne"));
  CHECK(m_nDSCReason == dscModify && m_nDSCState == dscDidEvent);
  pCtl1->SetAge(33);
  CHECK(pCursor->m_nAges[0] == 33 && pCursor->m_nUpdates == 2);
  CHECK(pCursor->m_strNames[1] == _T("Bob") && pCursor->m_nAges[1] == 42);

  // The cursor changes a field of the current row, and of another.
  int nSets = pCtl1->m_nAgeSets;
  pCursor->ChangeAge(0, 50);
  CHECK(AgeOf(m_bound1) == 50 && pCtl1->m_nAgeSets == nSets + 1);
  pCursor->ChangeAge(1, 51);
  CHECK(AgeOf(m_bound1) == 50 && pCtl1->m_nAgeSets == nSets + 1);
  CHECK(pCursor->m_nUpdates == 2);

  // A field the cursor does not have binds nothing.
  m_bound2.BindDefaultProperty(DISPID_AGE, VT_I4, _T("Height"), &vbdsc);
  CHECK(AgeOf(m_bound2) == -1);

  m_bound1.DestroyWindow();
  pCursor->MoveTo(1);
  CHECK(TextOf(m_bound2) == _T("Bob"));
  m_bound2.DestroyWindow();
  m_dsc.DestroyWindow();
  vbdsc.DestroyWindow();
  CHECK(CFakeCursor::s_nAlive == 0 && CVBDscCtrl::s_nAlive == 0 && CBoundCtrl::s_nAlive == 0 &&
        CDscCtrl::s_nAlive == 0);
}

void CMainFrame::RunTests() {
  TestSimple();
  TestComplex();
  TestUnbind();
  TestDestroyOrder();
  TestRowsetOnly();
  TestDialog();
  TestCursor();
}

// =====================================================================

class CTestApp : public COleControlModule {
public:
  virtual BOOL InitInstance() {
    TestOpenLog(_T("bindtest.log"));
    if (!COleControlModule::InitInstance()) return FALSE;
    if (!AfxOleInit()) {
      TestLog("AfxOleInit failed");
      return FALSE;
    }
    AfxEnableControlContainer();
    CHECK(COleObjectFactory::RegisterAll());
    CMainFrame* pFrame = new CMainFrame;
    if (!pFrame->Create(NULL, _T("bindtest"), WS_OVERLAPPEDWINDOW, CRect(0, 0, 300, 200)))
      return FALSE;
    m_pMainWnd = pFrame;
    pFrame->ShowWindow(SW_SHOW);
    pFrame->UpdateWindow();
    return TRUE;
  }
  virtual int ExitInstance() {
    COleObjectFactory::RevokeAll();
    CHECK(CFakeTable::s_nAlive == 0 && CDscCtrl::s_nAlive == 0 && CBoundCtrl::s_nAlive == 0);
    CHECK(CFakeCursor::s_nAlive == 0 && CVBDscCtrl::s_nAlive == 0);
    COleControlModule::ExitInstance();
    return TestFinish();
  }
};

CTestApp theApp;
